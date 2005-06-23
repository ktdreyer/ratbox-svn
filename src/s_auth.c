/*
 *  ircd-ratbox: A slightly useful ircd.
 *  s_auth.c: Functions for querying a users ident.
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
 *  $Id$ */

/*
 * Changes:
 *   July 6, 1999 - Rewrote most of the code here. When a client connects
 *     to the server and passes initial socket validation checks, it
 *     is owned by this module (auth) which returns it to the rest of the
 *     server when dns and auth queries are finished. Until the client is
 *     released, the server does not know it exists and does not process
 *     any messages from it.
 *     --Bleep  Thomas Helvey <tomh@inxpress.net>
 */
#include "stdinc.h"
#include "tools.h"
#include "struct.h"
#include "s_auth.h"
#include "s_conf.h"
#include "client.h"
#include "event.h"
#include "irc_string.h"
#include "ircd_lib.h"
#include "ircd.h"
#include "numeric.h"
#include "commio.h"
#include "packet.h"
#include "s_log.h"
#include "s_stats.h"
#include "send.h"
#include "hook.h"
#include "balloc.h"
#include "linebuf.h"
#include "res.h"

/*
 * a bit different approach
 * this replaces the original sendheader macros
 */

static const char *HeaderMessages[] = {
	"NOTICE AUTH :*** Looking up your hostname...",
	"NOTICE AUTH :*** Found your hostname",
	"NOTICE AUTH :*** Couldn't look up your hostname",
	"NOTICE AUTH :*** Checking Ident",
	"NOTICE AUTH :*** Got Ident response",
	"NOTICE AUTH :*** No Ident response",
	"NOTICE AUTH :*** Your hostname is too long, ignoring hostname"
};

typedef enum
{
	REPORT_DO_DNS,
	REPORT_FIN_DNS,
	REPORT_FAIL_DNS,
	REPORT_DO_ID,
	REPORT_FIN_ID,
	REPORT_FAIL_ID,
	REPORT_HOST_TOOLONG
}
ReportType;

#define sendheader(c, r) sendto_one(c, POP_QUEUE, HeaderMessages[(r)])

static dlink_list auth_poll_list;
static BlockHeap *auth_heap;
static EVH timeout_auth_queries_event;

static buf_head_t auth_sendq;
static buf_head_t auth_recvq;

static PF read_auth_reply;
static int fork_ident_count = 0;
static int auth_ofd = -1;
static int auth_ifd = -1;
static pid_t auth_pid = -1;
static u_int16_t id;
#define IDTABLE 0x1000

static struct AuthRequest *authtable[IDTABLE];

static u_int16_t
assign_id(void)
{
	if(id < IDTABLE - 1)
		id++;
	else
		id = 1;
	return id;
}

static void
fork_ident(void)
{
	int ifd[2];
	int ofd[2];
	
	char fx[6];
	char fy[6];
	pid_t pid;
	int i;

	if(fork_ident_count > 10)
	{
		ilog(L_MAIN, "Ident daemon is spinning: %d\n", fork_ident_count);

	}
	fork_ident_count++;
	if(auth_ifd > 0)
		comm_close(auth_ifd);
	if(auth_ofd > 0)
		comm_close(auth_ofd);
	if(auth_pid > 0)
	{
		kill(auth_pid, SIGKILL);
	}
//	comm_socketpair(AF_UNIX, SOCK_STREAM, 0, fdx, "ident daemon");
	comm_pipe(ifd, "ident daemon - read");
	comm_pipe(ofd, "ident daemon - write");

	ircsnprintf(fx, sizeof(fx), "%d", ifd[1]); /*ident write*/
	ircsnprintf(fy, sizeof(fy), "%d", ofd[0]); /*ident read*/

	comm_set_nb(ifd[0]);
	comm_set_nb(ifd[1]);
	comm_set_nb(ifd[0]);
	comm_set_nb(ofd[1]);

	if(!(pid = fork()))
	{
		setenv("IFD", fy, 1);
		setenv("OFD", fx, 1);
		for(i = 0; i < HARD_FDLIMIT; i++)
		{
			if(i != ifd[1] && i != ofd[0])
				close(i);
		}
		execl(BINPATH "/ident", "-ircd ident daemon", NULL);
	}
	else if(pid == -1)
	{
		ilog(L_MAIN, "fork failed: %s", strerror(errno));
		comm_close(ifd[0]);
		comm_close(ifd[1]);
		comm_close(ofd[0]);
		comm_close(ofd[1]);
		return;
	}
	comm_close(ifd[1]);
	comm_close(ofd[0]);

	auth_ifd = ifd[0];
	auth_ofd = ofd[1];
	auth_pid = pid;
	return;
}

void
ident_sigchld(void)
{
	int status;
	if(waitpid(auth_pid, &status, WNOHANG) == auth_pid)
	{
		fork_ident();
	}
}

void
restart_ident(void)
{
	fork_ident();
}


/*
 * init_auth()
 *
 * Initialise the auth code
 */
void
init_auth(void)
{
	/* This hook takes a struct Client for its argument */
	fork_ident();
	linebuf_newbuf(&auth_sendq);
	linebuf_newbuf(&auth_recvq);
	if(auth_pid < 0)
	{
		ilog(L_MAIN, "Unable to fork ident daemon");
	}
	memset(&auth_poll_list, 0, sizeof(auth_poll_list));
	eventAddIsh("timeout_auth_queries_event", timeout_auth_queries_event, NULL, 1);
	auth_heap = BlockHeapCreate(sizeof(struct AuthRequest), LCLIENT_HEAP_SIZE);

}

/*
 * make_auth_request - allocate a new auth request
 */
static struct AuthRequest *
make_auth_request(struct Client *client)
{
	struct AuthRequest *request = BlockHeapAlloc(auth_heap);
	client->localClient->auth_request = request;
	request->client = client;
	request->dns_query = 0;
	request->reqid = 0;
	request->timeout = CurrentTime + ConfigFileEntry.connect_timeout;
	return request;
}

/*
 * free_auth_request - cleanup auth request allocations
 */
static void
free_auth_request(struct AuthRequest *request)
{
	BlockHeapFree(auth_heap, request);
}

/*
 * release_auth_client - release auth client from auth system
 * this adds the client into the local client lists so it can be read by
 * the main io processing loop
 */
static void
release_auth_client(struct AuthRequest *auth)
{
	struct Client *client = auth->client;

	if(IsDNS(auth) || IsAuth(auth))
		return;

	if(auth->reqid > 0)
		authtable[auth->reqid] = NULL;

	client->localClient->auth_request = NULL;
	dlinkDelete(&auth->node, &auth_poll_list);
	free_auth_request(auth);
	if(client->localClient->fd > highest_fd)
		highest_fd = client->localClient->fd;

	/*
	 * When a client has auth'ed, we want to start reading what it sends
	 * us. This is what read_packet() does.
	 *     -- adrian
	 */
	client->localClient->allow_read = MAX_FLOOD;
	comm_setflush(client->localClient->fd, 1000, flood_recalc, client);
	dlinkAddTail(client, &client->node, &global_client_list);
	read_packet(client->localClient->fd, client);
}

/*
 * auth_dns_callback - called when resolver query finishes
 * if the query resulted in a successful search, hp will contain
 * a non-null pointer, otherwise hp will be null.
 * set the client on it's way to a connection completion, regardless
 * of success of failure
 */
static void
auth_dns_callback(const char *res, int status, int aftype, void *data)
{
	struct AuthRequest *auth = data;
	ClearDNS(auth);
	auth->dns_query = 0;
	/* The resolver won't return us anything > HOSTLEN */
	if(status == 1)
	{
		strlcpy(auth->client->host, res, sizeof(auth->client->host));
		sendheader(auth->client, REPORT_FIN_DNS);
	}
	else
	{
		if(!strcmp(res, "HOSTTOOLONG"))
		{
			sendheader(auth->client, REPORT_HOST_TOOLONG);
		}
		sendheader(auth->client, REPORT_FAIL_DNS);
	}
	release_auth_client(auth);

}

/*
 * authsenderr - handle auth send errors
 */
static void
auth_error(struct AuthRequest *auth)
{
	ServerStats.is_abad++;

	if(auth->reqid > 0)
		authtable[auth->reqid] = NULL;
	ClearAuth(auth);
	sendheader(auth->client, REPORT_FAIL_ID);
}

static void
auth_write_sendq(int fd, void *unused)
{
	int retlen;
	if(linebuf_len(&auth_sendq) > 0)
	{
		while((retlen = linebuf_flush(auth_ofd, &auth_sendq)) > 0);
		if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
		{
			fork_ident();
		}
	}
	
	if(linebuf_len(&auth_sendq) > 0)
	{
		comm_setselect(auth_ofd, FDLIST_SERVICE, COMM_SELECT_WRITE, 
			       auth_write_sendq, NULL, 0);
	}
}

/*
 * start_auth_query - Flag the client to show that an attempt to 
 * contact the ident server on
 * the client's host.  The connect and subsequently the socket are all put
 * into 'non-blocking' mode.  Should the connect or any later phase of the
 * identifing process fail, it is aborted and the user is given a username
 * of "unknown".
 */
static void
start_auth_query(struct AuthRequest *auth)
{
	struct irc_sockaddr_storage localaddr;
	struct irc_sockaddr_storage remoteaddr;
	socklen_t locallen = sizeof(struct irc_sockaddr_storage);
	socklen_t remotelen = sizeof(struct irc_sockaddr_storage);
	char myip[HOSTIPLEN + 1];
	int lport, rport;

	if(IsAnyDead(auth->client))
		return;

	sendheader(auth->client, REPORT_DO_ID);
	/* 
	 * get the local address of the client and bind to that to
	 * make the auth request.  This used to be done only for
	 * ifdef VIRTUAL_HOST, but needs to be done for all clients
	 * since the ident request must originate from that same address--
	 * and machines with multiple IP addresses are common now
	 */
	memset(&localaddr, 0, locallen);

	if(getsockname(auth->client->localClient->fd, (struct sockaddr *) &localaddr, &locallen) ||
	   getpeername(auth->client->localClient->fd, (struct sockaddr *) &remoteaddr, &remotelen))
	{
		ilog(L_IOERROR, "auth get{sock,peer}name error for %s:%m", 
					log_client_name(auth->client, SHOW_IP));
		auth_error(auth);
		release_auth_client(auth);
		return;
	}

	mangle_mapped_sockaddr((struct sockaddr *) &localaddr);
	inetntop_sock((struct sockaddr *) &localaddr, myip, sizeof(myip));

#ifdef IPV6
	if(localaddr.ss_family == AF_INET6)
		lport = ntohs(((struct sockaddr_in6 *) &localaddr)->sin6_port);
	else
#endif
		lport = ntohs(((struct sockaddr_in *) &localaddr)->sin_port);

#ifdef IPV6
	if(localaddr.ss_family == AF_INET6)
		rport = ntohs(((struct sockaddr_in6 *) &remoteaddr)->sin6_port);
	else
#endif
		rport = ntohs(((struct sockaddr_in *) &remoteaddr)->sin_port);

	auth->reqid = assign_id();
	authtable[auth->reqid] = auth;

	linebuf_put(&auth_sendq, "%x %s %s %u %u", auth->reqid, myip, 
		    auth->client->sockhost, (unsigned int)rport, (unsigned int)lport); 


	auth_write_sendq(auth_ofd, NULL);

	read_auth_reply(auth_ifd, NULL);
	return;
}




/*
 * start_auth - starts auth (identd) and dns queries for a client
 */
void
start_auth(struct Client *client)
{
	struct AuthRequest *auth = 0;
	s_assert(0 != client);
	if(client == NULL)
		return;
	auth = make_auth_request(client);

	sendheader(client, REPORT_DO_DNS);

	dlinkAdd(auth, &auth->node, &auth_poll_list);

	/* Note that the order of things here are done for a good reason
	 * if you try to do start_auth_query before lookup_ip there is a 
	 * good chance that you'll end up with a double free on the auth
	 * and that is bad.  But you still must keep the SetDNSPending 
	 * before the call to start_auth_query, otherwise you'll have
	 * the same thing.  So think before you hack 
	 */
	SetDNS(auth);   /* set both at the same time to eliminate possible race conditions */
	SetAuth(auth);
	if(ConfigFileEntry.disable_auth == 0)
	{
		start_auth_query(auth);
	} else 
		ClearAuth(auth);

	auth->dns_query = lookup_ip(client->sockhost, client->localClient->ip.ss_family, auth_dns_callback, auth);
}

/*
 * timeout_auth_queries - timeout resolver and identd requests
 * allow clients through if requests failed
 */
static void
timeout_auth_queries_event(void *notused)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	struct AuthRequest *auth;

	DLINK_FOREACH_SAFE(ptr, next_ptr, auth_poll_list.head)
	{
		auth = ptr->data;

		if(auth->timeout < CurrentTime)
		{
			if(IsAuth(auth))
			{
				auth_error(auth);
			}
			if(IsDNS(auth))
			{
				ClearDNS(auth);
				cancel_lookup(auth->dns_query);
				auth->dns_query = 0;
				sendheader(auth->client, REPORT_FAIL_DNS);
			}

			auth->client->localClient->lasttime = CurrentTime;
			release_auth_client(auth);
		}
	}
	return;
}


void
delete_auth_queries(struct Client *target_p)
{
	struct AuthRequest *auth;
	if(target_p == NULL || target_p->localClient == NULL || target_p->localClient->auth_request == NULL)
		return;
	auth = target_p->localClient->auth_request;
	target_p->localClient->auth_request = NULL;

	if(auth->dns_query > 0)
	{
		cancel_lookup(auth->dns_query);
		auth->dns_query = 0;
	}

	if(auth->reqid > 0)
		authtable[auth->reqid] = NULL;

	dlinkDelete(&auth->node, &auth_poll_list);
	free_auth_request(auth);
}

/* read auth reply from ident daemon */

static char authBuf[READBUF_SIZE];

static void
parse_auth_reply(void)
{
	int len;
	struct AuthRequest *auth;	
	char *q, *p;
	while((len = linebuf_get(&auth_recvq, authBuf, sizeof(authBuf), 
			         LINEBUF_COMPLETE, LINEBUF_PARSED)) > 0)	
	{
		
		q = strchr(authBuf, ' ');

		if(q == NULL)
			continue;

		*q = '\0';
		q++;

		id = strtoul(authBuf, NULL, 16);
		auth = authtable[id];

		if(auth == NULL)
			continue; /* its gone away...oh well */
	
		p = strchr(q, '\n');

		if(p != NULL)
			*p = '\0';

		if(*q == '0')
		{
			auth_error(auth);
			release_auth_client(auth);
			continue;
		}

		strlcpy(auth->client->username, q, sizeof(auth->client->username));
		ClearAuth(auth);
		ServerStats.is_asuc++;
		sendheader(auth->client, REPORT_FIN_ID);
		SetGotId(auth->client);
		release_auth_client(auth);
	}


}

static void
read_auth_reply(int fd, void *data)
{
	int length;

	while((length = comm_read(auth_ifd, authBuf, sizeof(authBuf))) > 0)
	{
		linebuf_parse(&auth_recvq, authBuf, length, 0);
		parse_auth_reply();
	}

	if(length == 0)
		fork_ident();

	if(length == -1 && !ignoreErrno(errno))
		fork_ident();
	
	comm_setselect(auth_ifd, FDLIST_SERVICE, COMM_SELECT_READ, read_auth_reply, NULL, 0);	
}

