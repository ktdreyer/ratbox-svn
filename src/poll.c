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
#include "s_zip.h"
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

struct Client* local[MAXCONNECTIONS];
static struct pollfd pollfds[MAXCONNECTIONS];
static unsigned int npollfds = 0;

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
        if ((fd+1) > npollfds)
            npollfds = fd + 1;
    } else {
        pollfds[fd].events &= ~event;
        if (pollfds[fd].events == 0) {
            pollfds[fd].fd = -1;
            pollfds[fd].revents = 0;
        }
        while (pollfds[npollfds].fd == -1 && npollfds > 0)
            npollfds--;
        npollfds++; /* array start at 0, not 1, remember? */
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
comm_setselect(int fd, unsigned int type, PF * handler, void *client_data,
    time_t timeout)
{  
    fde_t *F = &fd_table[fd];
    assert(fd >= 0);
#ifdef NOTYET
    assert(F->flags.open);
    debug(5, 5) ("commSetSelect: FD %d type %d, %s\n", fd, type, handler ? "SET"
 : "CLEAR");
#endif
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

#define PFD_SETR(thisfd) do { CHECK_PFD(thisfd) \
                           pfd->events |= POLLREADFLAGS; } while (0)
#define PFD_SETW(thisfd) do { CHECK_PFD(thisfd) \
                           pfd->events |= POLLWRITEFLAGS; } while (0)
#define CHECK_PFD(thisfd)                     \
        if (pfd->fd != thisfd) {              \
                pfd = &poll_fdarray[nbr_pfds++];\
                poll_fdarray[nbr_pfds].fd = -1; \
                pfd->fd     = thisfd;           \
                pfd->events = 0;                \
        }

int read_message(time_t delay, unsigned char mask)
{
  struct Client*       cptr;
  int                  nfds;
  struct timeval       wait;

  static struct pollfd poll_fdarray[MAXCONNECTIONS];
  struct pollfd*       pfd = poll_fdarray;
  struct pollfd*       res_pfd = NULL;
  int                  nbr_pfds = 0;
  time_t               delay2 = delay;
  u_long               usec = 0;
  int                  res = 0;
  int                  length;
  int                  fd;
  struct AuthRequest*  auth;
  struct AuthRequest*  auth_next;
  int                  rr;
  int                  rw;
  int                  i;

  for ( ; ; ) {
    nbr_pfds = 0;
    pfd      = poll_fdarray;
    pfd->fd  = -1;
    res_pfd  = NULL;
    auth = 0;

    /*
     * set auth descriptors
     */
    for (auth = AuthPollList; auth; auth = auth->next) {
      assert(-1 < auth->fd);
      auth->index = nbr_pfds;
      if (IsAuthConnect(auth))
        PFD_SETW(auth->fd);
      else
        PFD_SETR(auth->fd);
    }
    /*
     * set client descriptors
     */
    for (i = 0; i <= highest_fd; ++i) {
      if (!(fd_table[i].mask & mask) || !(cptr = local[i]))
        continue;

     /*
      * anything that IsMe should NEVER be in the local client array
      */
      assert(!IsMe(cptr));
      if (DBufLength(&cptr->recvQ) && delay2 > 2)
        delay2 = 1;

      if (DBufLength(&cptr->recvQ) < 4088)
        PFD_SETR(i);
      
      if (IsConnecting(cptr))
        PFD_SETW(i);
    }

    wait.tv_sec = IRCD_MIN(delay2, delay);
    wait.tv_usec = usec;
    nfds = poll(poll_fdarray, nbr_pfds,
                wait.tv_sec * 1000 + wait.tv_usec / 1000);
    if ((CurrentTime = time(0)) == -1)
      {
        log(L_CRIT, "Clock Failure");
        restart("Clock failed");
      }   
    if (nfds == -1 && ((errno == EINTR) || (errno == EAGAIN)))
      return -1;
    else if (nfds >= 0)
      break;
    report_error("poll %s:%s", me.name, errno);
    res++;
    if (res > 5)
      restart("too many poll errors");
    sleep(10);
  }

  /*
   * check auth descriptors
   */
  for (auth = AuthPollList; auth; auth = auth_next) {
    auth_next = auth->next;
    i = auth->index;
    /*
     * check for any event, we only ask for one at a time
     */
    if (poll_fdarray[i].revents) { 
      if (IsAuthConnect(auth))
        send_auth_query(auth);
      else
        read_auth_reply(auth);
      poll_fdarray[i].fd = -1;
      if (0 == --nfds)
        break;
    }
  }
  /*
   * Since we're going to rip this code out anyway, the current "hack"
   * will suffice to keep things going. The only two things this loop
   * services now is the auth and client/server fds (resolver and
   * listener FDs are done through the new-style interface) . So,
   * we loop again through the poll_fdarray[] array from scratch again
   * and fd's that are not -1 are ready for us to do read-type events
   * with. The pollfd_array[i].fd = -1; in the auth fd loop stops us
   * from looking at the auth fds as client/server fds which would be
   * a Bad Thing(tm) right now. :-)    -- adrian
   */
  pfd = &poll_fdarray[0];
    
  for (i = 0; (i < nbr_pfds); i++, pfd++)
    {
      if (pfd->fd < 0)
          continue;
      fd = pfd->fd;                   
      rr = pfd->revents & POLLREADFLAGS;
      rw = pfd->revents & POLLWRITEFLAGS;
      if (pfd->revents & POLLERRORS)
        {
          if (pfd->events & POLLREADFLAGS)
            rr++;
          if (pfd->events & POLLWRITEFLAGS)
            rw++;
        }
      if (!(cptr = local[fd]))
        continue;

      if (rw)
        {
          if (IsConnecting(cptr)) {
            if (!completed_connection(cptr)) {
              exit_client(cptr, cptr, &me, "Lost C/N Line");
              continue;
            }
          }
          else {
            /*
             * ...room for writing, empty some queue then...
             */
            exit(111); /* XXX we shouldn't get here now! -- adrian */
          }
        }
      length = 1;     /* for fall through case */
      if (rr)
        length = read_packet(cptr);
      else if (PARSE_AS_CLIENT(cptr) && !NoNewLine(cptr))
        length = parse_client_queued(cptr);

      if (length > 0 || length == CLIENT_EXITED)
        continue;
      if (IsDead(cptr)) {
         exit_client(cptr, cptr, &me,
                      strerror(get_sockerr(cptr->fd)));
         continue;
      }
      error_exit_client(cptr, length);
      errno = 0;
    }
  return 0;
}


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
        num = poll(pollfds, npollfds, delay * 1000);
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
    for (fd = 0; fd < npollfds; fd++) {
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

