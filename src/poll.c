/************************************************************************
 *
 * s_bsd_poll.c - code implementing a poll IO loop
 *   By Adrian Chadd <adrian@creative.net.au>
 *
 * Based upon:
 *
 *   IRC - Internet Relay Chat, src/s_bsd.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  $Id$
 */
#include "fdlist.h"
#include "s_bsd.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "irc_string.h"
#include "ircdauth.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "numeric.h"
#include "packet.h"
#include "res.h"
#include "restart.h"
#include "s_auth.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_stats.h"
#include "send.h"
#include "s_debug.h"
#include "s_bsd.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/param.h>    /* NOFILE */
#include <arpa/inet.h>

/*
 * Stuff for poll()
 */
#define __USE_XOPEN    /* XXXX had to add this define to make it compile -toby */
#include <sys/poll.h>
#define CONNECTFAST

#ifndef IN_LOOPBACKNET
#define IN_LOOPBACKNET        0x7f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

extern struct sockaddr_in vserv;               /* defined in s_conf.c */

static struct pollfd pollfds[MAXCONNECTIONS];

static void poll_update_pollfds(int, short, PF *);

/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
/* Private functions */

/*
 * set and clear entries in the pollfds[] array.
 */ 
static void
poll_update_pollfds(int fd, short event, PF * handler)
{  
    if (handler) {
        pollfds[fd].events |= event;
        pollfds[fd].fd = fd;
    } else {
        pollfds[fd].events &= ~event;
        if (pollfds[fd].events == 0) {
            pollfds[fd].fd = -1;
            pollfds[fd].revents = 0;
        }
    }
}


/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
/* Public functions */


/*
 * init_netio
 *
 * This is a needed exported function which will be called to initialise
 * the network loop code.
 */
void init_netio(void)
{
	/* Nothing to do here yet .. */
}

/*
 * comm_setselect
 *
 * This is a needed exported function which will be called to register
 * and deregister interest in a pending IO state for a given FD.
 */
void
comm_setselect(int fd, fdlist_t list, unsigned int type, PF * handler,
    void *client_data, time_t timeout)
{  
    fde_t *F = &fd_table[fd];
    assert(fd >= 0);
#ifdef NOTYET
    assert(F->flags.open);
    debug(5, 5) ("commSetSelect: FD %d type %d, %s\n", fd, type, handler ? "SET"
 : "CLEAR");
#endif
    F->list = list;
    if (type & COMM_SELECT_READ) {
        F->read_handler = handler;
        F->read_data = client_data;
        poll_update_pollfds(fd, POLLRDNORM, handler);
    }
    if (type & COMM_SELECT_WRITE) {
        F->write_handler = handler;
        F->write_data = client_data;
        poll_update_pollfds(fd, POLLWRNORM, handler);
    }
    if (timeout)
        F->timeout = CurrentTime + timeout;
}
 
/*
 * Check all connections for new connections and input data that is to be
 * processed. Also check for connections with data queued and whether we can
 * write it out.
 */

#if defined(POLLMSG) && defined(POLLIN) && defined(POLLRDNORM)
#define POLLREADFLAGS (POLLMSG | POLLIN | POLLRDNORM)
#else

# if defined(POLLIN) && defined(POLLRDNORM)
# define POLLREADFLAGS (POLLIN | POLLRDNORM)
# else

#  if defined(POLLIN)
#  define POLLREADFLAGS POLLIN
#  else

#   if defined(POLLRDNORM)
#    define POLLREADFLAGS POLLRDNORM
#   endif

#  endif

# endif

#endif

#if defined(POLLOUT) && defined(POLLWRNORM)
#define POLLWRITEFLAGS (POLLOUT | POLLWRNORM)
#else

# if defined(POLLOUT)
# define POLLWRITEFLAGS POLLOUT
# else

#  if defined(POLLWRNORM)
#  define POLLWRITEFLAGS POLLWRNORM
#  endif

# endif

#endif

#if defined(POLLERR) && defined(POLLHUP)
#define POLLERRORS (POLLERR | POLLHUP)
#else
#define POLLERRORS POLLERR
#endif

/*
 * comm_select
 *
 * Called to do the new-style IO, courtesy of of squid (like most of this
 * new IO code). This routine handles the stuff we've hidden in
 * comm_setselect and fd_table[] and calls callbacks for IO ready
 * events.
 */

int
comm_select(time_t delay)
{
    int num;
    int fd;
    PF *hdl;

    /* update current time */
    if ((CurrentTime = time(0)) == -1) {
        log(L_CRIT, "Clock Failure");
        restart("Clock failed");
    }   

    for (;;) {
        num = poll(pollfds, highest_fd + 1, delay * 1000);
        if (num >= 0)
            break;
        if (ignoreErrno(errno))
            continue;
        /* error! */
        return -1;
        /* NOTREACHED */
    }

    /* update current time again, eww.. */
    if ((CurrentTime = time(0)) == -1) {
        log(L_CRIT, "Clock Failure");
        restart("Clock failed");
    }   

    if (num == 0)
        return 0;

    /* XXX we *could* optimise by falling out after doing num fds ... */
    for (fd = 0; fd < highest_fd + 1; fd++) {
        fde_t *F;
	int revents;
	if (((revents = pollfds[fd].revents) == 0) ||
	    (pollfds[fd].fd) == -1)
	    continue;
        F = &fd_table[fd];
	if (revents & (POLLREADFLAGS | POLLERRORS)) {
	    hdl = F->read_handler;
	    poll_update_pollfds(fd, POLLRDNORM, NULL);
	    if (!hdl) {
		/* XXX Eek! This is another bad place! */
	    } else {
		hdl(fd, F->read_data);
            }
	}
	if (revents & (POLLWRITEFLAGS | POLLERRORS)) {
	    hdl = F->write_handler;
	    poll_update_pollfds(fd, POLLWRNORM, NULL);
	    if (!hdl) {
		/* XXX Eek! This is another bad place! */
	    } else {
		hdl(fd, F->write_data);
            }
	}
    }
    return 0;
}

