/**
 * \defgroup backlogger Backlogger
 * This module manages tracking commodity levels for the DUBP process.
 * \{
 */

#include <pthread.h>        /* for pthread_create() */
#include <unistd.h>
#include <sys/queue.h>      /* for LIST_*() */
#include <stdio.h>
#include <stdlib.h>

#include <libnetfilter_queue/libnetfilter_queue.h>  /* for nfq_*() */

#include "dubp.h"
#include "fifo_queue.h"
#include "logger.h"
#include "ntable.h"
#include "commodity.h"
#include "neighbor.h"
#include "router.h"


static struct nfq_handle *h;    /**< Handle to netfilter queue library. */


/**
 * Initialize the backlogger thread.
 *
 * Open connection to libnetfilter and set up necessary queues to track commodities.
 *
 * \pre All commodities have been initialized and exist in dubpd.clist.  Each commodity_t element in dubpd.clist has 
 * 'uint32_t nfq_id' set and 'fifo_t *queue == NULL'
 *
 * \post Each commodity_t element in dubpd.clist has a valid fifo_t *queue that can be used in calls to functions in
 * fifo_queue.h
 */
static void backlogger_init() {

    elm_t *e;
    commodity_t *c;

    /** \todo determine if setpriorty() must be called to improve performance */

    /* Opening netfilter_queue library handle */
    h = nfq_open();
    if (!h) {
        DUBP_LOG_ERR("error during nfq_open()");
    }

    /* Unbind existing nf_queue handler for AF_INET (if any) */
    /** \todo extend to IPv6 handling */
    if (nfq_unbind_pf(h, AF_INET) < 0) {
        DUBP_LOG_ERR("Error during nfq_unbind_pf()");
    }

    /* Bind nfnetlink_queue as nf_queue handler for AF_INET */
    /** \todo extend to IPv6 handling */
    if (nfq_bind_pf(h, AF_INET) < 0) {
        DUBP_LOG_ERR("Error during nfq_bind_pf()");
    }

    /* iterate through list looking for matching element */
    for (e = LIST_FIRST(&dubpd.clist); e != NULL; e = LIST_NEXT(e, elms)) {
        c = e->data;
        c->queue = (fifo_t *)malloc(sizeof(fifo_t));
        fifo_init(c->queue);

        /* Bind this socket to queue c->nfq_id */
        c->queue->qh = nfq_create_queue(h, c->nfq_id, &fifo_add_packet, c->queue);
        if (!c->queue->qh) {
            DUBP_LOG_ERR("Error during nfq_create_queue()");
        }

        /* Set packet copy mode to NFQNL_COPY_META */
        if (nfq_set_mode(c->queue->qh, NFQNL_COPY_META, 0xffff) < 0) {
            DUBP_LOG_ERR("Can't set packet_copy mode");
        }
    }
}


/**
 * Update the backlogs on each commodity.  Update the backlog differential to each neighbor for each commodity.
 */
void backlogger_update() {

    elm_t *e, *f;
    neighbor_t *n, *nopt;
    commodity_t *c, *ctemp;
    uint32_t diffopt, difftemp;
    struct netaddr naddr;
    union netaddr_socket nsaddr;
    
    for(e = LIST_FIRST(&dubpd.clist); e != NULL; e = LIST_NEXT(e, elms)) {
            c = (commodity_t *)e->data;
            assert(c->queue);
            c->cdata.backlog = fifo_length(c->queue);
    }

    ntable_mutex_lock(&dubpd.ntable);

    /* convert my address into a netaddr for easy comparison */
    nsaddr.std = *dubpd.saddr; 
    netaddr_from_socket(&naddr, &nsaddr);

    /* for each of my commodities */
    for(e = LIST_FIRST(&dubpd.clist); e != NULL; e = LIST_NEXT(e, elms)) {
        c = (commodity_t *)e->data;  

        if (netaddr_cmp(&naddr, &(c->cdata.addr)) == 0) {
            /* the commodity is destined to me! ignore it */
            struct netaddr_str tempstr;
            DUBP_LOG_DBG("Ignoring commodity destined to: %s", netaddr_to_string(&tempstr, &c->cdata.addr));
            continue;
        }

        /* tie breaker */
        int num = 0;
        diffopt = 0;

        /* try to find this commodity in neighbor's clist */
        for(f = LIST_FIRST(&dubpd.ntable.nlist); f != NULL; f = LIST_NEXT(f, elms)) {
            n = (neighbor_t *)f->data;

            if ((ctemp = clist_find(&n->clist, c)) == NULL) {
                DUBP_LOG_ERR("Neighbor doesn't know about commodity that I know about");
            }

            if (!n->bidir) {
                /* I can hear the neighbor, but not sure if I can speak to the neighbor, skip him */
                continue;
            }

            if (netaddr_cmp(&n->addr,&ctemp->cdata.addr)) {
                /* The neighbor is the commodity's destination, send to him */
                /** \todo Fully consider the built-in assumption -> unicast commodities (single-destination) */
                nopt = n;
                break;
            }

            difftemp = c->cdata.backlog - ctemp->cdata.backlog;
            if (difftemp < diffopt) {
                /* we found a neighbor with smaller backlog differential, or less than zero, ignore it */
                continue;
            } else if (difftemp == diffopt) {
                /* we found a neighbor with equal backlog differential */
                num++;
            } else {
                /* we found a neighbor with larger backlog differential */
                num = 1;
            }
                
            /* we use the following test to determine if we have a new nexthop */
            if (((double)rand())/((double)RAND_MAX) >= ((double)(num-1))/((double)num)) {
                /* this results in uniformly choosing amongst an unknown number of ties */
                /* when num == 1, we always satisfy the test */
                nopt = n;
            }
        }

        /* by here, we have the best nexthop for commodity c, set it */
        /* convert commodity destination and nexthop addresses from netaddr to socket */
        union netaddr_socket nsaddr_dst, nsaddr_nh;
        netaddr_to_socket(&nsaddr_dst, &(c->cdata.addr));
        netaddr_to_socket(&nsaddr_nh, &(nopt->addr));
        router_route_update(&(nsaddr_dst.std), &(nsaddr_nh.std), dubpd.ipver, dubpd.if_index);
    }

    ntable_mutex_unlock(&dubpd.ntable);
}


/**
 * Loop endlessly and handle commodity packets.
 *
 * \param arg Unused.
 */
static void *backlogger_thread_main(void *arg __attribute__((unused)) ) {

    backlogger_init();
    router_init();
    int fd, rv;
    /** \todo determine if this is the correct way to allocate buffer */
    char buf[4096] __attribute__ ((aligned));

    fd = nfq_fd(h);

    while ((rv = recv(fd, buf, sizeof(buf),0)) && rv >=0) {
        /* main backlogger loop */
        nfq_handle_packet(h, buf, rv);
    }
    /** \todo clean up if while loop breaks? */

    return NULL;
}


/**
 * Create a new thread to handle continuous backlogger duties.
 */
void backlogger_thread_create() {

    /** \todo Check out pthread_attr options, currently set to NULL */
    if (pthread_create(&(dubpd.backlogger_tid), NULL, backlogger_thread_main, NULL) < 0) {
        DUBP_LOG_ERR("Unable to create backlogger thread");
    }

    /** \todo wait here until process stops? pthread_join(htdata.tid)? */
    /** \todo handle the case where hello writer thread stops  - encapsulate in while(1) - restart hello thread? */

}

/** \} */
