/*
 *  ircd-ratbox: A slightly useful ircd.
 *  commio.c: Network/file related functions
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#define s_assert assert

#define MAXCONNECTIONS 255
#define MASTER_MAX 255

#include "../../include/setup.h"
#include "defs.h"
#include "mem.h"
#include "commio.h"                     
                     
#ifndef IN_LOOPBACKNET
#define IN_LOOPBACKNET        0x7f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif


static const char *comm_err_str[] = { "Comm OK", "Error during bind()",
	"Error during DNS lookup", "connect timeout",
	"Error during connect()",
	"Comm Error"
};

fde_t *fd_table = NULL;
struct timeval SystemTime;
#define CurrentTime SystemTime.tv_sec

static void fdlist_update_biggest(int fd, int opening);

/* Highest FD and number of open FDs .. */
int highest_fd = -1;		/* Its -1 because we haven't started yet -- adrian */
int number_fd = 0;


static void comm_connect_callback(int fd, int status);
static PF comm_connect_timeout;
static PF comm_connect_tryconnect;



void
set_time(void)
{
	gettimeofday(&SystemTime, NULL);
}
/*
 * get_sockerr - get the error value from the socket or the current errno
 *
 * Get the *real* error from the socket (well try to anyway..).
 * This may only work when SO_DEBUG is enabled but its worth the
 * gamble anyway.
 */
int
comm_get_sockerr(int fd)
{
	int errtmp = errno;
#ifdef SO_ERROR
	int err = 0;
	socklen_t len = sizeof(err);

	if(-1 < fd && !getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *) &err, (socklen_t *) & len))
	{
		if(err)
			errtmp = err;
	}
	errno = errtmp;
#endif
	return errtmp;
}

/*
 * set_sock_buffers - set send and receive buffers for socket
 * 
 * inputs	- fd file descriptor
 * 		- size to set
 * output       - returns true (1) if successful, false (0) otherwise
 * side effects -
 */
int
comm_set_buffers(int fd, int size)
{
	if(setsockopt
	   (fd, SOL_SOCKET, SO_RCVBUF, (char *) &size, sizeof(size))
	   || setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &size, sizeof(size)))
		return 0;
	return 1;
}

/*
 * set_non_blocking - Set the client connection into non-blocking mode. 
 *
 * inputs	- fd to set into non blocking mode
 * output	- 1 if successful 0 if not
 * side effects - use POSIX compliant non blocking and
 *                be done with it.
 */
int
comm_set_nb(int fd)
{
	int nonb = 0;
	int res;

	nonb |= O_NONBLOCK;
	res = fcntl(fd, F_GETFL, 0);
	if(-1 == res || fcntl(fd, F_SETFL, res | nonb) == -1)
		return 0;

	fd_table[fd].flags.nonblocking = 1;
	return 1;
}


/*
 * stolen from squid - its a neat (but overused! :) routine which we
 * can use to see whether we can ignore this errno or not. It is
 * generally useful for non-blocking network IO related errnos.
 *     -- adrian
 */
int
ignoreErrno(int ierrno)
{
	switch (ierrno)
	{
	case EINPROGRESS:
	case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
	case EAGAIN:
#endif
	case EALREADY:
	case EINTR:
#ifdef ERESTART
	case ERESTART:
#endif
		return 1;
	default:
		return 0;
	}
}


/*
 * comm_settimeout() - set the socket timeout
 *
 * Set the timeout for the fd
 */
void
comm_settimeout(int fd, time_t timeout, PF * callback, void *cbdata)
{
	fde_t *F;
	s_assert(fd >= 0);
	F = &fd_table[fd];
	s_assert(F->flags.open);

	F->timeout = CurrentTime + (timeout / 1000);
	F->timeout_handler = callback;
	F->timeout_data = cbdata;
}


/*
 * comm_setflush() - set a flush function
 *
 * A flush function is simply a function called if found during
 * comm_timeouts(). Its basically a second timeout, except in this case
 * I'm too lazy to implement multiple timeout functions! :-)
 * its kinda nice to have it seperate, since this is designed for
 * flush functions, and when comm_close() is implemented correctly
 * with close functions, we _actually_ don't call comm_close() here ..
 */
void
comm_setflush(int fd, time_t timeout, PF * callback, void *cbdata)
{
	fde_t *F;
	s_assert(fd >= 0);
	F = &fd_table[fd];
	s_assert(F->flags.open);

	F->flush_timeout = CurrentTime + (timeout / 1000);
	F->flush_handler = callback;
	F->flush_data = cbdata;
}


/*
 * comm_checktimeouts() - check the socket timeouts
 *
 * All this routine does is call the given callback/cbdata, without closing
 * down the file descriptor. When close handlers have been implemented,
 * this will happen.
 */
void
comm_checktimeouts(void *notused)
{
	int fd;
	PF *hdl;
	void *data;
	fde_t *F;
	for (fd = 0; fd <= highest_fd; fd++)
	{
		F = &fd_table[fd];
		if(!F->flags.open)
			continue;
		if(F->flags.closing)
			continue;

		/* check flush functions */
		if(F->flush_handler &&
		   F->flush_timeout > 0 && F->flush_timeout < CurrentTime)
		{
			hdl = F->flush_handler;
			data = F->flush_data;
			comm_setflush(F->fd, 0, NULL, NULL);
			hdl(F->fd, data);
		}

		/* check timeouts */
		if(F->timeout_handler &&
		   F->timeout > 0 && F->timeout < CurrentTime)
		{
			/* Call timeout handler */
			hdl = F->timeout_handler;
			data = F->timeout_data;
			comm_settimeout(F->fd, 0, NULL, NULL);
			hdl(F->fd, F->timeout_data);
		}
	}
}

/*
 * void comm_connect_tcp(int fd, const char *host, u_short port,
 *                       struct sockaddr *clocal, int socklen,
 *                       CNCB *callback, void *data, int aftype, int timeout)
 * Input: An fd to connect with, a host and port to connect to,
 *        a local sockaddr to connect from + length(or NULL to use the
 *        default), a callback, the data to pass into the callback, the
 *        address family.
 * Output: None.
 * Side-effects: A non-blocking connection to the host is started, and
 *               if necessary, set up for selection. The callback given
 *               may be called now, or it may be called later.
 */
void
comm_connect_tcp(int fd, struct sockaddr *dest,
		 struct sockaddr *clocal, int socklen, CNCB * callback,
		 void *data, int timeout)
{
	fde_t *F;
	s_assert(fd >= 0);
	F = &fd_table[fd];
	F->flags.called_connect = 1;
	s_assert(callback);
	F->connect.callback = callback;
	F->connect.data = data;

	memcpy(&F->connect.hostaddr, dest, socklen);

	/* Note that we're using a passed sockaddr here. This is because
	 * generally you'll be bind()ing to a sockaddr grabbed from
	 * getsockname(), so this makes things easier.
	 * XXX If NULL is passed as local, we should later on bind() to the
	 * virtual host IP, for completeness.
	 *   -- adrian
	 */
	if((clocal != NULL) && (bind(F->fd, clocal, socklen) < 0))
	{
		/* Failure, call the callback with COMM_ERR_BIND */
		comm_connect_callback(F->fd, COMM_ERR_BIND);
		/* ... and quit */
		return;
	}

	/* We have a valid IP, so we just call tryconnect */
	/* Make sure we actually set the timeout here .. */
	comm_settimeout(F->fd, timeout * 1000, comm_connect_timeout, NULL);
	comm_connect_tryconnect(F->fd, NULL);
}

/*
 * comm_connect_callback() - call the callback, and continue with life
 */
static void
comm_connect_callback(int fd, int status)
{
	CNCB *hdl;
	fde_t *F = &fd_table[fd];
	/* This check is gross..but probably necessary */
	if(F->connect.callback == NULL)
		return;
	/* Clear the connect flag + handler */
	hdl = F->connect.callback;
	F->connect.callback = NULL;
	F->flags.called_connect = 0;

	/* Clear the timeout handler */
	comm_settimeout(F->fd, 0, NULL, NULL);

	/* Call the handler */
	hdl(F->fd, status, F->connect.data);
}


/*
 * comm_connect_timeout() - this gets called when the socket connection
 * times out. This *only* can be called once connect() is initially
 * called ..
 */
static void
comm_connect_timeout(int fd, void *notused)
{
	/* error! */
	comm_connect_callback(fd, COMM_ERR_TIMEOUT);
}

/* static void comm_connect_tryconnect(int fd, void *notused)
 * Input: The fd, the handler data(unused).
 * Output: None.
 * Side-effects: Try and connect with pending connect data for the FD. If
 *               we succeed or get a fatal error, call the callback.
 *               Otherwise, it is still blocking or something, so register
 *               to select for a write event on this FD.
 */
static void
comm_connect_tryconnect(int fd, void *notused)
{
	int retval;
	fde_t *F = &fd_table[fd];

	if(F->connect.callback == NULL)
		return;
	/* Try the connect() */
	retval = connect(fd,
			 (struct sockaddr *) &fd_table[fd].connect.hostaddr, 
						       GET_SS_LEN(fd_table[fd].connect.hostaddr));
	/* Error? */
	if(retval < 0)
	{
		/*
		 * If we get EISCONN, then we've already connect()ed the socket,
		 * which is a good thing.
		 *   -- adrian
		 */
		if(errno == EISCONN)
			comm_connect_callback(F->fd, COMM_OK);
		else if(ignoreErrno(errno))
			/* Ignore error? Reschedule */
			comm_setselect(F->fd, FDLIST_SERVER, COMM_SELECT_WRITE|COMM_SELECT_RETRY,
				       comm_connect_tryconnect, NULL, 0);
		else
			/* Error? Fail with COMM_ERR_CONNECT */
			comm_connect_callback(F->fd, COMM_ERR_CONNECT);
		return;
	}
	/* If we get here, we've suceeded, so call with COMM_OK */
	comm_connect_callback(F->fd, COMM_OK);
}

/*
 * comm_error_str() - return an error string for the given error condition
 */
const char *
comm_errstr(int error)
{
	if(error < 0 || error >= COMM_ERR_MAX)
		return "Invalid error number!";
	return comm_err_str[error];
}


/*
 * comm_socket() - open a socket
 *
 * This is a highly highly cut down version of squid's comm_open() which
 * for the most part emulates socket(), *EXCEPT* it fails if we're about
 * to run out of file descriptors.
 */
int
comm_socket(int family, int sock_type, int proto)
{
	int fd;
	/* First, make sure we aren't going to run out of file descriptors */
	if(number_fd >= MASTER_MAX)
	{
		errno = ENFILE;
		return -1;
	}

	/*
	 * Next, we try to open the socket. We *should* drop the reserved FD
	 * limit if/when we get an error, but we can deal with that later.
	 * XXX !!! -- adrian
	 */
	fd = socket(family, sock_type, proto);
	if(fd < 0)
		return -1;	/* errno will be passed through, yay.. */

#if defined(IPV6) && defined(IPV6_V6ONLY)
	/* 
	 * Make sure we can take both IPv4 and IPv6 connections
	 * on an AF_INET6 socket
	 */
	if(family == AF_INET6)
	{
		int off = 1;
		if(setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off)) == -1)
		{
//			ilog(L_IOERROR,
//			     "comm_socket: Could not set IPV6_V6ONLY option to 1 on FD %d: %s",
//			     fd, strerror(errno));
			close(fd);
			return -1;
		}
	}
#endif

	/* Set the socket non-blocking, and other wonderful bits */
	if(!comm_set_nb(fd))
	{
//		ilog(L_IOERROR, "comm_open: Couldn't set FD %d non blocking: %s", fd, strerror(errno));
		close(fd);
		return -1;
	}

	/* Next, update things in our fd tracking */
	comm_open(fd, FD_SOCKET);
	return fd;
}

#ifndef POLLRDNORM
#define POLLRDNORM POLLIN
#endif
#ifndef POLLWRNORM
#define POLLWRNORM POLLOUT
#endif

struct _pollfd_list
{
	struct pollfd pollfds[MAXCONNECTIONS];
	int maxindex;		/* highest FD number */
};

typedef struct _pollfd_list pollfd_list_t;

pollfd_list_t pollfd_list;
static void poll_update_pollfds(int, short, PF *);
static unsigned long last_count = 0; 
static unsigned long empty_count = 0;
/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
/* Private functions */

/*
 * find a spare slot in the fd list. We can optimise this out later!
 *   -- adrian
 */
static inline int
poll_findslot(void)
{
	int i;
	for (i = 0; i < MAXCONNECTIONS; i++)
	{
		if(pollfd_list.pollfds[i].fd == -1)
		{
			/* MATCH!!#$*&$ */
			return i;
		}
	}
	s_assert(1 == 0);
	/* NOTREACHED */
	return -1;
}

/*
 * set and clear entries in the pollfds[] array.
 */
static void
poll_update_pollfds(int fd, short event, PF * handler)
{
	fde_t *F = &fd_table[fd];
	int comm_index;

	if(F->comm_index < 0)
	{
		F->comm_index = poll_findslot();
	}
	comm_index = F->comm_index;

	/* Update the events */
	if(handler)
	{
		F->list = FDLIST_IDLECLIENT;
		pollfd_list.pollfds[comm_index].events |= event;
		pollfd_list.pollfds[comm_index].fd = fd;
		/* update maxindex here */
		if(comm_index > pollfd_list.maxindex)
			pollfd_list.maxindex = comm_index;
	}
	else
	{
		if(comm_index >= 0)
		{
			pollfd_list.pollfds[comm_index].events &= ~event;
			if(pollfd_list.pollfds[comm_index].events == 0)
			{
				pollfd_list.pollfds[comm_index].fd = -1;
				pollfd_list.pollfds[comm_index].revents = 0;
				F->comm_index = -1;
				F->list = FDLIST_NONE;

				/* update pollfd_list.maxindex here */
				if(comm_index == pollfd_list.maxindex)
					while (pollfd_list.maxindex >= 0 &&
					       pollfd_list.pollfds[pollfd_list.maxindex].fd == -1)
						pollfd_list.maxindex--;
			}
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
void
init_netio(void)
{
	int fd;
	fdlist_init();
	for (fd = 0; fd < MAXCONNECTIONS; fd++)
	{
		pollfd_list.pollfds[fd].fd = -1;
	}
	pollfd_list.maxindex = 0;
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
	s_assert(fd >= 0);
	s_assert(F->flags.open);

	if(type & COMM_SELECT_READ)
	{
		F->read_handler = handler;
		F->read_data = client_data;
		poll_update_pollfds(fd, POLLRDNORM, handler);
	}
	if(type & COMM_SELECT_WRITE)
	{
		F->write_handler = handler;
		F->write_data = client_data;
		poll_update_pollfds(fd, POLLWRNORM, handler);
	}
	if(timeout)
		F->timeout = CurrentTime + (timeout / 1000);
}

static void
irc_sleep(unsigned long useconds)
{     
#ifdef HAVE_NANOSLEEP    
        struct timespec t;
        t.tv_sec = useconds / (unsigned long) 1000000;
        t.tv_nsec = (useconds % (unsigned long) 1000000) * 1000;
        nanosleep(&t, (struct timespec *) NULL);
#else    
        struct timeval t;        
        t.tv_sec = 0;    
        t.tv_usec = useconds;
        select(0, NULL, NULL, NULL, &t);
#endif
        return;
}

/* int comm_select_fdlist(unsigned long delay)
 * Input: The maximum time to delay.
 * Output: Returns -1 on error, 0 on success.
 * Side-effects: Deregisters future interest in IO and calls the handlers
 *               if an event occurs for an FD.
 * Comments: Check all connections for new connections and input data
 * that is to be processed. Also check for connections with data queued
 * and whether we can write it out.
 * Called to do the new-style IO, courtesy of squid (like most of this
 * new IO code). This routine handles the stuff we've hidden in
 * comm_setselect and fd_table[] and calls callbacks for IO ready
 * events.
 */
int
comm_select(unsigned long delay)
{
	int num;
	int fd;
	int ci;
	unsigned long ndelay;
	PF *hdl;

	if(last_count > 0)
	{
		empty_count = 0;
		ndelay = 0;
	}
	else {
		ndelay = ++empty_count * 15000 ;
		if(ndelay > delay * 1000)
			ndelay = delay * 1000;	
	}
	
	for (;;)
	{
		/* XXX kill that +1 later ! -- adrian */
		if(ndelay > 0)
			irc_sleep(ndelay); 
		last_count = num = poll(pollfd_list.pollfds, pollfd_list.maxindex + 1, 0);
		if(num >= 0)
			break;
		if(ignoreErrno(errno))
			continue;
		/* error! */
		set_time();
		return -1;
		/* NOTREACHED */
	}

	/* update current time again, eww.. */
	set_time();

	if(num == 0)
		return 0;
	/* XXX we *could* optimise by falling out after doing num fds ... */
	for (ci = 0; ci < pollfd_list.maxindex + 1; ci++)
	{
		fde_t *F;
		int revents;
		if(((revents = pollfd_list.pollfds[ci].revents) == 0) ||
		   (pollfd_list.pollfds[ci].fd) == -1)
			continue;
		fd = pollfd_list.pollfds[ci].fd;
		F = &fd_table[fd];
		if(revents & (POLLRDNORM | POLLIN | POLLHUP | POLLERR))
		{
			hdl = F->read_handler;
			F->read_handler = NULL;
			poll_update_pollfds(fd, POLLRDNORM, NULL);
			if(hdl)
				hdl(fd, F->read_data);
		}
		if(revents & (POLLWRNORM | POLLOUT | POLLHUP | POLLERR))
		{
			hdl = F->write_handler;
			F->write_handler = NULL;
			poll_update_pollfds(fd, POLLWRNORM, NULL);
			if(hdl)
				hdl(fd, F->write_data);
		}
	}
	return 0;
}


void
fdlist_init(void)
{
	static int initialized = 0;

	if(!initialized)
	{
		/* Since we're doing this once .. */
		fd_table = MyMalloc((MAXCONNECTIONS + 1) * sizeof(fde_t));
		initialized = 1;
	}
}

/* Called to open a given filedescriptor */
void
comm_open(int fd, unsigned int type)
{
	fde_t *F = &fd_table[fd];
	s_assert(fd >= 0);

	if(F->flags.open)
	{
		comm_close(fd);
	}
	s_assert(!F->flags.open);
	F->fd = fd;
	F->type = type;
	F->flags.open = 1;

	fdlist_update_biggest(fd, 1);
	F->comm_index = -1;
	F->list = FDLIST_NONE;
	number_fd++;
}


/* Called to close a given filedescriptor */
void
comm_close(int fd)
{
	fde_t *F = &fd_table[fd];
	s_assert(F->flags.open);
	/* All disk fd's MUST go through file_close() ! */
	s_assert(F->type != FD_FILE);
	if(F->type == FD_FILE)
	{
		s_assert(F->read_handler == NULL);
		s_assert(F->write_handler == NULL);
	}
	comm_setselect(F->fd, FDLIST_NONE, COMM_SELECT_WRITE | COMM_SELECT_READ, NULL, NULL, 0);
	comm_setflush(F->fd, 0, NULL, NULL);
	
	F->flags.open = 0;
	fdlist_update_biggest(fd, 0);
	number_fd--;
	memset(F, '\0', sizeof(fde_t));
	F->timeout = 0;
	/* Unlike squid, we're actually closing the FD here! -- adrian */
	close(fd);
}

static void
fdlist_update_biggest(int fd, int opening)
{
	if(fd < highest_fd)
		return;
	s_assert(fd < MAXCONNECTIONS);

	if(fd > highest_fd)
	{
		/*  
		 * s_assert that we are not closing a FD bigger than
		 * our known biggest FD
		 */
		s_assert(opening);
		highest_fd = fd;
		return;
	}
	/* if we are here, then fd == Biggest_FD */
	/*
	 * s_assert that we are closing the biggest FD; we can't be
	 * re-opening it
	 */
	s_assert(!opening);
	while (highest_fd >= 0 && !fd_table[highest_fd].flags.open)
		highest_fd--;
}
