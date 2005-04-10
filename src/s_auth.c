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
#include "sprintf_irc.h"
#include "ircd.h"
#include "numeric.h"
#include "commio.h"
#include "packet.h"
#include "s_log.h"
#include "s_stats.h"
#include "send.h"
#include "memory.h"
#include "hook.h"
#include "balloc.h"
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

#define sendheader(c, r) sendto_one(c, HeaderMessages[(r)])

static dlink_list auth_poll_list;
static BlockHeap *auth_heap;
static EVH timeout_auth_queries_event;

static PF read_auth_reply;

static int fork_ident_count = 0;
static int auth_fd = -1, auth_cfd = -1;
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
	int fdx[2], cfdx[2];
	char fx[5], fy[5];
	pid_t pid;
	int i;

	if(fork_ident_count > 10)
	{
		ilog(L_MAIN, "Ident daemon is spinning: %d\n", fork_ident_count);

	}
	fork_ident_count++;
	if(auth_fd > 0)
		comm_close(auth_fd);
	if(auth_cfd > 0)
		comm_close(auth_cfd);
	if(auth_pid > 0)
	{
		kill(auth_pid, SIGKILL);
	}
	socketpair(AF_UNIX, SOCK_DGRAM, 0, fdx);
	socketpair(AF_UNIX, SOCK_STREAM, 0, cfdx);

	ircsnprintf(fx, sizeof(fx), "%d", fdx[1]);
	ircsnprintf(fy, sizeof(fy), "%d", cfdx[1]);

	comm_set_nb(fdx[0]);
	comm_set_nb(fdx[1]);
	comm_set_nb(cfdx[0]);
	comm_set_nb(cfdx[1]);

	if(!(pid = fork()))
	{
		setenv("FD", fx, 1);
		setenv("CFD", fy, 1);
		close(fdx[0]);
		close(cfdx[0]);
		for(i = 0; i < HARD_FDLIMIT; i++)
		{
			if((i == fdx[1]) || (i == cfdx[1]))
				comm_set_nb(i);
			else
				close(i);
		}
		execl(BINPATH "/ident", "-ircd ident daemon", NULL);
	}
	else if(pid == -1)
	{
		ilog(L_MAIN, "fork failed: %s", strerror(errno));
		close(fdx[0]);
		close(fdx[1]);
		close(cfdx[0]);
		close(cfdx[1]);
		return;
	}

	comm_open(fdx[0], FD_SOCKET, "ident daemon data socket");
	comm_open(cfdx[0], FD_SOCKET, "ident daemon control socket");
	auth_fd = fdx[0];
	auth_cfd = cfdx[0];
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

	if(IsDNSPending(auth) || IsDoingAuth(auth))
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
	ClearDNSPending(auth);
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
	release_auth_client(auth);
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
	char reqbuf[512];
	int lport, rport;
	int af = 4;

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
		return;
	}

	mangle_mapped_sockaddr((struct sockaddr *) &localaddr);
	inetntop_sock((struct sockaddr *) &localaddr, myip, sizeof(myip));

#ifdef IPV6
	if(localaddr.ss_family == AF_INET6)
	{
		lport = ntohs(((struct sockaddr_in6 *) &localaddr)->sin6_port);
		af = 6;
	}
	else
#endif
		lport = ntohs(((struct sockaddr_in *) &localaddr)->sin_port);

#ifdef IPV6
	if(localaddr.ss_family == AF_INET6)
	{
		rport = ntohs(((struct sockaddr_in6 *) &remoteaddr)->sin6_port);
		af = 6;
	}
	else
#endif
		rport = ntohs(((struct sockaddr_in *) &remoteaddr)->sin_port);

	SetAuthPending(auth);

	auth->reqid = assign_id();
	authtable[auth->reqid] = auth;

	ircsnprintf(reqbuf, sizeof(reqbuf), "%x %s %d %s %u %u\n", auth->reqid, myip, af,
		    auth->client->sockhost, (unsigned int)rport, (unsigned int)lport);

	if(send(auth_fd, reqbuf, strlen(reqbuf), 0) <= 0)
	{
		fork_ident();
	}
	read_auth_reply(auth_fd, NULL);
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
	SetDNSPending(auth);

	if(ConfigFileEntry.disable_auth == 0)
	{
		start_auth_query(auth);
	}

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
			if(IsDoingAuth(auth))
			{
				auth_error(auth);
			}
			if(IsDNSPending(auth))
			{
				ClearDNSPending(auth);
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

static void
read_auth_reply(int fd, void *data)
{
	int len;
	struct AuthRequest *auth;
	u_int16_t id;
	char *q, *p;
	char buf[512];


	while(1)
	{
		len = recv(fd, buf, sizeof(buf), 0);

		if(len < 0)
		{
			if(ignoreErrno(errno))
			{
				comm_setselect(fd, FDLIST_IDLECLIENT, COMM_SELECT_READ, read_auth_reply, auth, 0);
				return;
			}
			fork_ident();
			return;
		}
		
		if(len == 0)
		{
			fork_ident();
			return;
		}

		q = strchr(buf, ' ');

		if(q == NULL)
			return;

		*q = '\0';
		q++;

		id = strtoul(buf, NULL, 16);
		auth = authtable[id];

		if(auth == NULL)
			continue; /* its gone away...oh well */
	
		p = strchr(q, '\n');

		if(p != NULL)
			*p = '\0';

		
		if(*q == '0')
		{
			strcpy(auth->client->username, "unknown");
			auth_error(auth);
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
