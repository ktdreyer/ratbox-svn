/*
 *  ircd-ratbox: A slightly useful ircd.
 *  epoll.c: Linux epoll compatible network routines.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2001 Adrian Chadd <adrian@creative.net.au>
 *  Copyright (C) 2002 ircd-ratbox development team
 *  Copyright (C) 2002 Aaron Sethman <androsyn@ratbox.org>
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

#include "config.h"
#ifdef USE_EPOLL
#include "stdinc.h"

#include <sys/epoll.h>

#include "fdlist.h"
#include "s_bsd.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
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
#include "s_bsd.h"
#include "memory.h"


#define EPOLL_LENGTH 256

static int ep;			/* epoll file descriptor */

/*
 * init_netio
 *
 * This is a needed exported function which will be called to initialise
 * the network loop code.
 */
void
init_netio(void)
{
	ep = epoll_create(MAX_CLIENTS);
	if(ep < 0)
	{
		ilog(L_MAIN, "init_netio: Couldn't open epoll fd!\n");
		exit(115);	/* Whee! */
	}
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
	struct epoll_event ep_event;

	fde_t *F = &fd_table[fd];
	int old_flags = F->pflags;
	int op = 0;
	s_assert(fd >= 0);
	s_assert(F->flags.open);

	/* Update the list, even though we're not using it .. */
	F->list = list;

	if(type & COMM_SELECT_READ)
	{
		if(handler != NULL)
			F->pflags |= EPOLLIN | EPOLLHUP | EPOLLERR;
		else
			F->pflags &= ~(EPOLLIN | EPOLLHUP | EPOLLERR);

		F->read_handler = handler;
		F->read_data = client_data;
	}

	if(type & COMM_SELECT_WRITE)
	{
		if(handler != NULL)
			F->pflags = EPOLLOUT | EPOLLHUP | EPOLLERR;
		else
			F->pflags &= ~(EPOLLOUT | EPOLLHUP | EPOLLERR);
		F->write_handler = handler;
		F->write_data = client_data;
	}

	if(old_flags != 0)
	{
		if(F->pflags == 0)
			op = EPOLL_CTL_DEL;
		else
			op = EPOLL_CTL_MOD;
	}
	else
	{
		op = EPOLL_CTL_ADD;
	}


	if(timeout)
		F->timeout = CurrentTime + (timeout / 1000);

	/* No changes...do nothing */
	if(old_flags == F->pflags)
		return;

	ep_event.events = F->pflags;
	ep_event.data.ptr = F;
	if(epoll_ctl(ep, op, fd, &ep_event) != 0)
	{
		ilog(L_IOERROR, "comm_setselect(): epoll_ctl failed: %s", strerror(errno));
	}


}

/*
 * comm_select
 *
 * Called to do the new-style IO, courtesy of squid (like most of this
 * new IO code). This routine handles the stuff we've hidden in
 * comm_setselect and fd_table[] and calls callbacks for IO ready
 * events.
 */

int
comm_select(unsigned long delay)
{
	int num, i;
	static struct epoll_event pfd[EPOLL_LENGTH];

	num = epoll_wait(ep, pfd, EPOLL_LENGTH, delay);
	set_time();

	if(num < 0 && !ignoreErrno(errno))
	{
		return COMM_ERROR;
	}

	if(num == 0)
		return COMM_OK;

	for (i = 0; i < num; i++)
	{
		PF *hdl;
		fde_t *F = pfd[i].data.ptr;
		if(pfd[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR))
		{
			hdl = F->read_handler;
			if(hdl)
				hdl(F->fd, F->read_data);
			else
				ilog(L_IOERROR, "s_bsd_epoll.c: NULL read handler called");
		}

		if(F->flags.open == 0)
		{
			/* Read handler closed us..go on and do something useful */
			continue;
		}
		if(pfd[i].events & (EPOLLOUT | EPOLLHUP | EPOLLERR))
		{
			hdl = F->write_handler;
			if(hdl)
				hdl(F->fd, F->write_data);
			else
				ilog(L_IOERROR, "s_bsd_epoll.c: NULL write handler called");
		}
	}
	return COMM_OK;
}

#endif /* USE_EPOLL */
