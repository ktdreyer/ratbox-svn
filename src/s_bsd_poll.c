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
#include "s_bsd.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "fdlist.h"
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


void init_netio(void)
{
  init_resolver();
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
  struct Listener*     listener;
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
     * set resolver descriptor
     */
    if (ResolverFileDescriptor >= 0) {
      PFD_SETR(ResolverFileDescriptor);
      res_pfd = pfd;
    }
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
     * set listener descriptors
     */
    for (listener = ListenerPollList; listener; listener = listener->next) {
      assert(-1 < listener->fd);
#ifdef CONNECTFAST
      listener->index = nbr_pfds;
      PFD_SETR(listener->fd);
#else
     /* 
      * It is VERY bad if someone tries to send a lot
      * of clones to the server though, as mbuf's can't
      * be allocated quickly enough... - Comstud
      */
      listener->index = -1;
      if (CurrentTime > (listener->last_accept + 2)) {
        listener->index = nbr_pfds;
        PFD_SETR(listener->fd);
      }
      else if (delay2 > 2)
        delay2 = 2;
#endif
    }
    /*
     * set client descriptors
     */
    for (i = 0; i <= highest_fd; ++i) {
      if (!(GlobalFDList[i] & mask) || !(cptr = local[i]))
        continue;

     /*
      * anything that IsMe should NEVER be in the local client array
      */
      assert(!IsMe(cptr));
      if (DBufLength(&cptr->recvQ) && delay2 > 2)
        delay2 = 1;

      if (DBufLength(&cptr->recvQ) < 4088)
        PFD_SETR(i);
      
      if (DBufLength(&cptr->sendQ) || IsConnecting(cptr)
          || ((cptr->flags2 & FLAGS2_ZIP) && (cptr->zip->outcount > 0))
          )
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
   * check resolver descriptor
   */
  if (res_pfd && (res_pfd->revents & (POLLREADFLAGS | POLLERRORS))) {
    get_res();
    --nfds;
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
      if (0 == --nfds)
        break;
    }
  }
  /*
   * check listeners
   */
  for (listener = ListenerPollList; listener; listener = listener->next) {
    if (-1 == listener->index)
      continue;
    i = listener->index;
    if (poll_fdarray[i].revents) {
      accept_connection(listener);
      if (0 == --nfds)
        break;
    }
  }
  /*
   * i contains the next non-auth/non-listener index, since we put the 
   * resolver, auth and listener, file descriptors in poll_fdarray first, 
   * the very next one should be the start of the clients
   */
  pfd = &poll_fdarray[++i];
    
  for ( ; (i < nbr_pfds); i++, pfd++)
    {
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
            send_queued(cptr);
            if (!IsDead(cptr))
              continue;
          }
          else {
            /*
             * ...room for writing, empty some queue then...
             */
            send_queued(cptr);
            if (!IsDead(cptr))
              continue;
          }
          exit_client(cptr, cptr, &me, 
                     (cptr->flags & FLAGS_SENDQEX) ? 
                     "SendQ Exceeded" : strerror(get_sockerr(cptr->fd)));
          continue;
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


