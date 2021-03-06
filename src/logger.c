/**
 * The BackPressure Routing Daemon (bprd).
 *
 * Copyright (c) 2012 Jeffrey Wildman <jeffrey.wildman@gmail.com>
 * Copyright (c) 2012 Bradford Boyle <bradford.d.boyle@gmail.com>
 *
 * bprd is released under the MIT License.  You should have received
 * a copy of the MIT License with this program.  If not, see
 * <http://opensource.org/licenses/MIT>.
 */

/**
 * \defgroup logger Logger
 * This module provides logging capabilities via syslog.
 * \{
 */

#include "logger.h"

#include <pthread.h>    /* for pthread_* */
#include <stdarg.h>     /* for vsnprintf() */
#include <stdio.h>      /* for snprintf() */
#include <stdlib.h>     /* for exit() */
#include <syslog.h>


/**
 * \def BPRD_LOG_ERR(fmt,...) Logs an error message and exits.
 * \param fmt Printf-style format string.
 * \param ... Arguments corresponding to format string \a fmt.
 */


/**
 * \def BPRD_LOG_DBG(fmt,...) Logs a debug message.
 * \param fmt Printf-style format string.
 * \param ... Arguments corresponding to format string \a fmt.
 */


/**
 * Mapping of syslog priorities to 5-character string identifiers.
 */
static char *logger_prioritynames[] = {
    "EMERG",
    "ALERT",
    "CRTCL",
    "ERROR",
    "WRNNG",
    "NOTCE",
    "INFO",
    "DEBUG",
    NULL
};

#define LOGGER_MSGSTRLEN 256                    /**< Maximum message length sent to syslog. */
static char logger_msgstr[LOGGER_MSGSTRLEN];    /**< Storage for message sent to syslog. */
static pthread_mutex_t logger_mutex;            /**< Mutex controlling thread access to logger. */


/**
 * Initialize the logger.
 */
void logger_init() {
  
    pthread_mutex_init(&logger_mutex, NULL);
    openlog(NULL, LOG_PID | LOG_PERROR | LOG_NDELAY, LOG_USER);
}


/**
 * Cleanup the logger.
 */
void logger_cleanup() {

    closelog();
}


/**
 * Log a formatted message.  Exit if an error message.
 *
 * \note This function is thread-safe and blocks until logging request is satisfied.
 *
 * \param priority A syslog priority level.
 * \param file Name of file originating log message.
 * \param line Line number of file originating log message.
 * \param fmt Printf-style format string.
 * \param ... Arguments corresponding to format string \a fmt.
 */
void logger_log(int priority, const char *file, const int line, 
                const char *fmt, ...) {

    int n = 0, m = 0;

    pthread_mutex_lock(&logger_mutex);

    /* write first part of message */
    if (file) {
        n = snprintf(logger_msgstr, LOGGER_MSGSTRLEN, "%s %s:%d ", logger_prioritynames[LOG_PRI(priority)], file, line);
    } else {
        n = snprintf(logger_msgstr, LOGGER_MSGSTRLEN, "%s ", logger_prioritynames[LOG_PRI(priority)]);
    }

    /* if room, write second part of message */
    if (n < LOGGER_MSGSTRLEN) {
        va_list fmtargs;
        va_start(fmtargs, fmt);
        m = vsnprintf(logger_msgstr+n, LOGGER_MSGSTRLEN-n, fmt, fmtargs);
        va_end(fmtargs);
    }

    if (n + m < LOGGER_MSGSTRLEN) {
        /* no truncation occurred, good! */
        syslog(priority, "%s\n", logger_msgstr);
    } else {
        /* properly terminate message string before logging */
        snprintf(logger_msgstr+LOGGER_MSGSTRLEN-1,1,"%s","");
        syslog(priority, "%s\n", logger_msgstr);
        syslog(LOG_WARNING, 
               "%s %s:%d Previous log message truncated due to length\n", 
               logger_prioritynames[LOG_WARNING], file, line);
    }

    /* special handling of an error */
    if (LOG_PRI(priority) == LOG_ERR) {
        exit(1);
    }

    pthread_mutex_unlock(&logger_mutex);
}

/** \} */
