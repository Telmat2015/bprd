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

#ifndef __PROCFILE_H
#define __PROCFILE_H

extern int procfile_write(const char *procfile, char *oldval, char newval);

#endif /* __PROCFILE_H */
