/*
 *  ircd-ratbox: A slightly useful ircd.
 *  listener.c: Listens on a port.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
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

#include "stdinc.h"
#include "config.h"
#include "listener.h"
#include "client.h"
#include "fdlist.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_stats.h"
#include "send.h"
#include "memory.h"
#include "setup.h"

#ifdef HAVE_LIBCRYPTO
#include <openssl/bio.h>
#endif


#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

static PF accept_connection;

static struct Listener *ListenerPollList = NULL;

static struct Listener *
make_listener(struct sockaddr_storage *addr)
{
	struct Listener *listener = (struct Listener *) MyMalloc(sizeof(struct Listener));
	s_assert(0 != listener);

	listener->name = me.name;
	listener->fd = -1;
	memcpy(&listener->addr, addr, sizeof(struct sockaddr_storage));

	listener->next = NULL;
	return listener;
}

void
free_listener(struct Listener *listener)
{
	s_assert(NULL != listener);
	if(listener == NULL)
		return;
	/*
	 * remove from listener list
	 */
	if(listener == ListenerPollList)
		ListenerPollList = listener->next;
	else
	{
		struct Listener *prev = ListenerPollList;
		for (; prev; prev = prev->next)
		{
			if(listener == prev->next)
			{
				prev->next = listener->next;
				break;
			}
		}
	}

	/* free */
	MyFree(listener);
}

#define PORTNAMELEN 6		/* ":31337" */

/*
 * get_listener_name - return displayable listener name and port
 * returns "host.foo.org:6667" for a given listener
 */
const char *
get_listener_name(const struct Listener *listener)
{
	static char buf[HOSTLEN + HOSTLEN + PORTNAMELEN + 4];
	int port = 0;

	s_assert(NULL != listener);
	if(listener == NULL)
		return NULL;

#ifdef IPV6
	if(listener->addr.ss_family == AF_INET6)
		port = ntohs(((struct sockaddr_in6 *)&listener->addr)->sin6_port);
	else
#endif
		port = ntohs(((struct sockaddr_in *)&listener->addr)->sin_port);	

	ircsprintf(buf, "%s[%s/%u]", me.name, listener->name, port);
	return buf;
}

/*
 * show_ports - send port listing to a client
 * inputs       - pointer to client to show ports to
 * output       - none
 * side effects - show ports
 */
void
show_ports(struct Client *source_p)
{
	struct Listener *listener = 0;

	for (listener = ListenerPollList; listener; listener = listener->next)
	{
		sendto_one_numeric(source_p, RPL_STATSPLINE, 
				   form_str(RPL_STATSPLINE), 'P',
#ifdef IPV6
			   ntohs(listener->addr.ss_family == AF_INET ? ((struct sockaddr_in *)&listener->addr)->sin_port :
			   	 ((struct sockaddr_in6 *)&listener->addr)->sin6_port),
#else
			   ntohs(((struct sockaddr_in *)&listener->addr)->sin_port),
#endif
			   IsOperAdmin(source_p) ? listener->name : me.name,
			   listener->ref_count, (listener->active) ? "active" : "disabled");
	}
}

/*
 * inetport - create a listener socket in the AF_INET or AF_INET6 domain,
 * bind it to the port given in 'port' and listen to it
 * returns true (1) if successful false (0) on error.
 *
 * If the operating system has a define for SOMAXCONN, use it, otherwise
 * use RATBOX_SOMAXCONN
 */
#ifdef SOMAXCONN
#undef RATBOX_SOMAXCONN
#define RATBOX_SOMAXCONN SOMAXCONN
#endif

static int
inetport(struct Listener *listener)
{
	int fd;
	int opt = 1;

	/*
	 * At first, open a new socket
	 */
	
	fd = comm_open(listener->addr.ss_family, SOCK_STREAM, 0, "Listener socket");

#ifdef IPV6
	if(listener->addr.ss_family == AF_INET6)
	{
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&listener->addr;
		if(!IN6_ARE_ADDR_EQUAL(&in6->sin6_addr, &in6addr_any))
		{
			inetntop(AF_INET6, &in6->sin6_addr, listener->vhost, sizeof(listener->vhost));
			listener->name = listener->vhost;
		}
	} else
#endif
	{
		struct sockaddr_in *in = (struct sockaddr_in *)&listener->addr;
		if(in->sin_addr.s_addr != INADDR_ANY)
		{
			inetntop(AF_INET, &in->sin_addr, listener->vhost, sizeof(listener->vhost));
			listener->name = listener->vhost;
		}	
	}


	if(fd == -1)
	{
		report_error(L_ALL, "opening listener socket %s:%s",
			     get_listener_name(listener), errno);
		return 0;
	}
	else if((HARD_FDLIMIT - 10) < fd)
	{
		report_error(L_ALL,
			     "no more connections left for listener %s:%s",
			     get_listener_name(listener), errno);
		fd_close(fd);
		return 0;
	}
	/*
	 * XXX - we don't want to do all this crap for a listener
	 * set_sock_opts(listener);
	 */
	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)))
	{
		report_error(L_ALL,
			     "setting SO_REUSEADDR for listener %s:%s",
			     get_listener_name(listener), errno);
		fd_close(fd);
		return 0;
	}

	/*
	 * Bind a port to listen for new connections if port is non-null,
	 * else assume it is already open and try get something from it.
	 */

	if(bind(fd, (struct sockaddr *) &listener->addr, GET_SS_LEN(listener->addr)))
	{
		report_error(L_ALL, "binding listener socket %s:%s",
			     get_listener_name(listener), errno);
		fd_close(fd);
		return 0;
	}

	if(listen(fd, RATBOX_SOMAXCONN))
	{
		report_error(L_ALL, "listen failed for %s:%s", get_listener_name(listener), errno);
		fd_close(fd);
		return 0;
	}

	/*
	 * XXX - this should always work, performance will suck if it doesn't
	 */
	if(!set_non_blocking(fd))
		report_error(L_ALL, NONB_ERROR_MSG, get_listener_name(listener), errno);

	listener->fd = fd;

	/* Listen completion events are READ events .. */

	accept_connection(fd, listener);
	return 1;
}

static struct Listener *
find_listener(struct sockaddr_storage *addr)
{
	struct Listener *listener = NULL;
	struct Listener *last_closed = NULL;

	for (listener = ListenerPollList; listener; listener = listener->next)
	{
		if(addr->ss_family != listener->addr.ss_family)
			continue;
		
		switch(addr->ss_family)
		{
			case AF_INET:
			{
				struct sockaddr_in *in4 = (struct sockaddr_in *)addr;
				struct sockaddr_in *lin4 = (struct sockaddr_in *)&listener->addr;
				if(in4->sin_addr.s_addr == lin4->sin_addr.s_addr && 
					in4->sin_port == lin4->sin_port )
				{
					if(listener->fd == -1)
						last_closed = listener;
					else
						return(listener);
				}
				break;
			}
#ifdef IPV6
			case AF_INET6:
			{
				struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;
				struct sockaddr_in6 *lin6 =(struct sockaddr_in6 *)&listener->addr;
				if(IN6_ARE_ADDR_EQUAL(&in6->sin6_addr, &lin6->sin6_addr) &&
				  in6->sin6_port == lin6->sin6_port)
				{
					if(listener->fd == -1)
						last_closed = listener;
					else
						return(listener);
				}
				break;
				
			}
#endif

			default:
				break;
		}
	}
	return last_closed;
}


/*
 * add_listener- create a new listener
 * port - the port number to listen on
 * vhost_ip - if non-null must contain a valid IP address string in
 * the format "255.255.255.255"
 */
void
add_listener(int port, const char *vhost_ip, int family)
{
	struct Listener *listener;
	struct sockaddr_storage vaddr;

	/*
	 * if no port in conf line, don't bother
	 */
	if(port == 0)
		return;

	vaddr.ss_family = family;

	if(vhost_ip != NULL)
	{
		if(family == AF_INET)
		{
			if(inetpton(family, vhost_ip, &((struct sockaddr_in *)&vaddr)->sin_addr) <= 0)
				return;
		} else
		{
			if(inetpton(family, vhost_ip, &((struct sockaddr_in6 *)&vaddr)->sin6_addr) <= 0)
				return;
		
		}
	} else
	{
		switch(family)
		{
			case AF_INET:
				((struct sockaddr_in *)&vaddr)->sin_addr.s_addr = INADDR_ANY;
				break;
#ifdef IPV6
			case AF_INET6:
				memcpy(&((struct sockaddr_in6 *)&vaddr)->sin6_addr, &in6addr_any, sizeof(struct in6_addr));
				break;
			default:
				return;
#endif
		} 
	}
	switch(family)
	{
		case AF_INET:
			((struct sockaddr_in *)&vaddr)->sin_port = htons(port);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)&vaddr)->sin6_port = htons(port);
			break;
		default:
			break;
	}
	if((listener = find_listener(&vaddr)))
	{
		if(listener->fd > -1)
			return;
	}
	else
	{
		listener = make_listener(&vaddr);
		listener->next = ListenerPollList;
		ListenerPollList = listener;
	}

	listener->fd = -1;

	if(inetport(listener))
		listener->active = 1;
	else
		close_listener(listener);
}

/*
 * close_listener - close a single listener
 */
void
close_listener(struct Listener *listener)
{
	s_assert(listener != NULL);
	if(listener == NULL)
		return;
	if(listener->fd >= 0)
	{
		fd_close(listener->fd);
		listener->fd = -1;
	}

	listener->active = 0;

	if(listener->ref_count)
		return;

	free_listener(listener);
}

/*
 * close_listeners - close and free all listeners that are not being used
 */
void
close_listeners()
{
	struct Listener *listener;
	struct Listener *listener_next = 0;
	/*
	 * close all 'extra' listening ports we have
	 */
	for (listener = ListenerPollList; listener; listener = listener_next)
	{
		listener_next = listener->next;
		close_listener(listener);
	}
}

#define DLINE_WARNING "ERROR :You have been D-lined.\r\n"

static void
accept_connection(int pfd, void *data)
{
	static time_t last_oper_notice = 0;

	struct sockaddr_storage sai;
	int fd;
	int pe;
	struct Listener *listener = data;

	s_assert(listener != NULL);
	if(listener == NULL)
		return;
	/*
	 * There may be many reasons for error return, but
	 * in otherwise correctly working environment the
	 * probable cause is running out of file descriptors
	 * (EMFILE, ENFILE or others?). The man pages for
	 * accept don't seem to list these as possible,
	 * although it's obvious that it may happen here.
	 * Thus no specific errors are tested at this
	 * point, just assume that connections cannot
	 * be accepted until some old is closed first.
	 */

	fd = comm_accept(listener->fd, &sai);

	/* This needs to be done here, otherwise we break dlines */
	mangle_mapped_sockaddr(&sai);

	if(fd < 0)
	{
		/* Re-register a new IO request for the next accept .. */
		comm_setselect(listener->fd, FDLIST_SERVICE,
			       COMM_SELECT_READ, accept_connection, listener, 0);
		return;
	}
	/*
	 * check for connection limit
	 */
	if((MAXCONNECTIONS - 10) < fd)
	{
		++ServerStats->is_ref;
		/*
		 * slow down the whining to opers bit
		 */
		if((last_oper_notice + 20) <= CurrentTime)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "All connections in use. (%s)",
					     get_listener_name(listener));
			last_oper_notice = CurrentTime;
		}

		send(fd, "ERROR :All connections in use\r\n", 32, SEND_FLAGS);
		fd_close(fd);
		/* Re-register a new IO request for the next accept .. */
		comm_setselect(listener->fd, FDLIST_SERVICE,
			       COMM_SELECT_READ, accept_connection, listener, 0);
		return;
	}

	/* Do an initial check we aren't connecting too fast or with too many
	 * from this IP... */
	if((pe = conf_connect_allowed(&sai, sai.ss_family)) != 0)
	{
		ServerStats->is_ref++;

		/* XXX - this can only be BANNED_CLIENT? */
		switch (pe)
		{
		case BANNED_CLIENT:
			send(fd, DLINE_WARNING, sizeof(DLINE_WARNING) - 1, SEND_FLAGS);
			break;
		}

		fd_close(fd);

		/* Re-register a new IO request for the next accept .. */
		comm_setselect(listener->fd, FDLIST_SERVICE,
			       COMM_SELECT_READ, accept_connection, listener, 0);
		return;
	}

	ServerStats->is_ac++;
	add_connection(listener, fd, &sai);

	/* Re-register a new IO request for the next accept .. */
	comm_setselect(listener->fd, FDLIST_SERVICE, COMM_SELECT_READ,
		       accept_connection, listener, 0);
}
