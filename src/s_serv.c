/*
 *  ircd-ratbox: A slightly useful ircd.
 *  s_serv.c: Server related functions.
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

#ifdef HAVE_LIBCRYPTO
#include <openssl/rsa.h>
#endif

#include "tools.h"
#include "s_serv.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "event.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "numeric.h"
#include "packet.h"
#include "res.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_stats.h"
#include "s_user.h"
#include "scache.h"
#include "send.h"
#include "client.h"
#include "memory.h"
#include "channel.h"		/* chcap_usage_counts stuff... */
#include "hook.h"
#include "msg.h"

#define MIN_CONN_FREQ 300

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

int MaxConnectionCount = 1;
int MaxClientCount = 1;
int refresh_user_links = 0;

static char buf[BUFSIZE];

/*
 * list of recognized server capabilities.  "TS" is not on the list
 * because all servers that we talk to already do TS, and the kludged
 * extra argument to "PASS" takes care of checking that.  -orabidoo
 */
struct Capability captab[] = {
/*  name     cap     */
	{ "QS",		CAP_QS },
	{ "EX",		CAP_EX },
	{ "CHW",	CAP_CHW},
	{ "IE", 	CAP_IE},
	{ "EOB",	CAP_EOB},
	{ "KLN",	CAP_KLN},
	{ "GLN",	CAP_GLN},
	{ "KNOCK",	CAP_KNOCK},
	{ "ZIP",	CAP_ZIP},
	{ "TBURST",	CAP_TBURST},
	{ "UNKLN",	CAP_UNKLN},
	{ "CLUSTER",	CAP_CLUSTER},
	{ "ENCAP",	CAP_ENCAP },
	{0, 0}
};

static CNCB serv_connect_callback;

#ifdef BROKEN_ZIPLINKS
void
slink_zipstats(unsigned int rpl, unsigned int len, unsigned char *data, struct Client *server_p)
{
	struct ZipStats zipstats;
	unsigned long in = 0, in_wire = 0, out = 0, out_wire = 0;
	int i = 0;

	s_assert(rpl == SLINKRPL_ZIPSTATS);
	s_assert(len == 16);
	s_assert(IsCapable(server_p, CAP_ZIP));

	/* Yes, it needs to be done this way, no we cannot let the compiler
	 * work with the pointer to the structure.  This works around a GCC
	 * bug on SPARC that affects all versions at the time of this writing.
	 * I will feed you to the creatures living in RMS's beard if you do
	 * not leave this as is, without being sure that you are not causing
	 * regression for most of our installed SPARC base.
	 * -jmallett, 04/27/2002
	 */
	memcpy(&zipstats, &server_p->localClient->zipstats, sizeof(struct ZipStats));

	in |= (data[i++] << 24);
	in |= (data[i++] << 16);
	in |= (data[i++] << 8);
	in |= (data[i++]);

	in_wire |= (data[i++] << 24);
	in_wire |= (data[i++] << 16);
	in_wire |= (data[i++] << 8);
	in_wire |= (data[i++]);

	out |= (data[i++] << 24);
	out |= (data[i++] << 16);
	out |= (data[i++] << 8);
	out |= (data[i++]);

	out_wire |= (data[i++] << 24);
	out_wire |= (data[i++] << 16);
	out_wire |= (data[i++] << 8);
	out_wire |= (data[i++]);

	/* This macro adds b to a if a plus b is not an overflow, and sets the
	 * value of a to b if it is.
	 * Add and Set if No Overflow.
	 */
#define	ASNO(a,b)	\
	a = (a + b >= a? a + b: b)

	ASNO(zipstats.in, in);
	ASNO(zipstats.out, out);
	ASNO(zipstats.in_wire, in_wire);
	ASNO(zipstats.out_wire, out_wire);

	if(zipstats.in > 0)
		zipstats.in_ratio =
			(((double) (zipstats.in - zipstats.in_wire) /
			  (double) zipstats.in) * 100.00);
	else
		zipstats.in_ratio = 0;

	if(zipstats.out > 0)
		zipstats.out_ratio =
			(((double) (zipstats.out - zipstats.out_wire) /
			  (double) zipstats.out) * 100.00);
	else
		zipstats.out_ratio = 0;

	memcpy(&server_p->localClient->zipstats, &zipstats, sizeof(struct ZipStats));
}
#endif

void
collect_zipstats(void *unused)
{
#ifdef BROKEN_ZIPLINKS
	dlink_node *ptr;
	struct Client *target_p;

	DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = ptr->data;
		/* XXX - ZIP */
		if(IsCapable(target_p, CAP_ZIP))
		{
		}
	}
#endif
}

/*
 * hunt_server - Do the basic thing in delivering the message (command)
 *      across the relays to the specific server (server) for
 *      actions.
 *
 *      Note:   The command is a format string and *MUST* be
 *              of prefixed style (e.g. ":%s COMMAND %s ...").
 *              Command can have only max 8 parameters.
 *
 *      server  parv[server] is the parameter identifying the
 *              target server.
 *
 *      *WARNING*
 *              parv[server] is replaced with the pointer to the
 *              real servername from the matched client (I'm lazy
 *              now --msa).
 *
 *      returns: (see #defines)
 */
int
hunt_server(struct Client *client_p, struct Client *source_p,
	    const char *command, int server, int parc, const char *parv[])
{
	struct Client *target_p;
	int wilds;
	dlink_node *ptr;
	const char *old __unused = parv[server];
	char *new;

	/*
	 * Assume it's me, if no server
	 */
	if(parc <= server || EmptyString(parv[server]) ||
	   match(me.name, parv[server]) || match(parv[server], me.name) ||
	   (strcmp(parv[server], me.id) == 0))
		return (HUNTED_ISME);
	
	new = LOCAL_COPY(parv[server]);
	parv[server] = new;

	/*
	 * These are to pickup matches that would cause the following
	 * message to go in the wrong direction while doing quick fast
	 * non-matching lookups.
	 */
	if(MyClient(source_p))
		target_p = find_named_client(parv[server]);
	else
		target_p = find_client(parv[server]);

	if(target_p == NULL)
		if(target_p->from == source_p->from && !MyConnect(target_p))
			target_p = NULL;

	if(target_p == NULL && (target_p = find_server(parv[server])))
		if(target_p->from == source_p->from && !MyConnect(target_p))
			target_p = NULL;

	collapse(new);
	wilds = (strchr(parv[server], '?') || strchr(parv[server], '*'));

	/*
	 * Again, if there are no wild cards involved in the server
	 * name, use the hash lookup
	 */
	if(!target_p)
	{
		if(!wilds)
		{
			sendto_one_numeric(source_p, ERR_NOSUCHSERVER,
					   form_str(ERR_NOSUCHSERVER),
					   parv[server]);
			return (HUNTED_NOSUCH);
		}
		else
		{

			DLINK_FOREACH(ptr, global_client_list.head)
			{
				target_p = NULL;
				if(match(parv[server], ((struct Client *) (ptr->data))->name))
				{
					target_p = ptr->data;
					break;
				}
			}
		}
	}

	if(target_p)
	{
		if(!IsRegistered(target_p))
		{
			sendto_one_numeric(source_p, ERR_NOSUCHSERVER,
					   form_str(ERR_NOSUCHSERVER),
					   parv[server]);
			return HUNTED_NOSUCH;
		}

		if(IsMe(target_p) || MyClient(target_p))
			return HUNTED_ISME;

		parv[0] = get_id(source_p, target_p);
		parv[server] = get_id(target_p, target_p);

		sendto_one(target_p, command, parv[0],
			   parv[1], parv[2], parv[3], parv[4], parv[5], parv[6], parv[7], parv[8]);
		return (HUNTED_PASS);
	}

	sendto_one_numeric(source_p, ERR_NOSUCHSERVER,
			   form_str(ERR_NOSUCHSERVER), parv[server]);
	return (HUNTED_NOSUCH);
}

/*
 * try_connections - scan through configuration and try new connections.
 * Returns the calendar time when the next call to this
 * function should be made latest. (No harm done if this
 * is called earlier or later...)
 */
void
try_connections(void *unused)
{
	struct ConfItem *aconf;
	struct Client *client_p;
	int connecting = FALSE;
	int confrq;
	time_t next = 0;
	struct Class *cltmp;
	struct ConfItem *con_conf = NULL;

	for (aconf = ConfigItemList; aconf; aconf = aconf->next)
	{
		/*
		 * Also when already connecting! (update holdtimes) --SRB 
		 */
		if(!(aconf->status & CONF_SERVER) || aconf->port <= 0 ||
		   !(aconf->flags & CONF_FLAGS_ALLOW_AUTO_CONN))
			continue;
		cltmp = ClassPtr(aconf);
		/*
		 * Skip this entry if the use of it is still on hold until
		 * future. Otherwise handle this entry (and set it on hold
		 * until next time). Will reset only hold times, if already
		 * made one successfull connection... [this algorithm is
		 * a bit fuzzy... -- msa >;) ]
		 */
		if(aconf->hold > CurrentTime)
		{
			if(next > aconf->hold || next == 0)
				next = aconf->hold;
			continue;
		}

		if((confrq = get_con_freq(cltmp)) < MIN_CONN_FREQ)
			confrq = MIN_CONN_FREQ;

		aconf->hold = CurrentTime + confrq;
		/*
		 * Found a CONNECT config with port specified, scan clients
		 * and see if this server is already connected?
		 */
		client_p = find_server(aconf->name);

		if(!client_p && (CurrUsers(cltmp) < MaxUsers(cltmp)) && !connecting)
		{
			con_conf = aconf;
			/* We connect only one at time... */
			connecting = TRUE;
		}
		if((next > aconf->hold) || (next == 0))
			next = aconf->hold;
	}


	/* TODO: change this to set active flag to 0 when added to event! --Habeeb */
	if(GlobalSetOptions.autoconn == 0)
		return;

	if(connecting)
	{
		if(con_conf->next)	/* are we already last? */
		{
			struct ConfItem **pconf;
			for (pconf = &ConfigItemList; (aconf = *pconf); pconf = &(aconf->next))
				/* 
				 * put the current one at the end and
				 * make sure we try all connections
				 */
				if(aconf == con_conf)
					*pconf = aconf->next;
			(*pconf = con_conf)->next = 0;
		}

		if(con_conf->flags & CONF_FLAGS_ALLOW_AUTO_CONN)
		{
			/*
			 * We used to only print this if serv_connect() actually
			 * suceeded, but since comm_tcp_connect() can call the callback
			 * immediately if there is an error, we were getting error messages
			 * in the wrong order. SO, we just print out the activated line,
			 * and let serv_connect() / serv_connect_callback() print an
			 * error afterwards if it fails.
			 *   -- adrian
			 */
#ifndef HIDE_SERVERS_IPS
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Connection to %s[%s] activated.",
					     con_conf->name, con_conf->host);
#else
			sendto_realops_flags(UMODE_ALL, L_ALL,
			    		     "Connection to %s activated",
					     con_conf->name);
#endif

			serv_connect(con_conf, 0);
		}
	}
}

/*
 * send_capabilities
 *
 * inputs	- Client pointer to send to
 *		- int flag of capabilities that this server has
 * output	- NONE
 * side effects	- send the CAPAB line to a server  -orabidoo
 *
 */
void
send_capabilities(struct Client *client_p, struct ConfItem *aconf,
		  int cap_can_send)
{
	struct Capability *cap;
	char msgbuf[BUFSIZE];
	char *t;
	int tl;

	t = msgbuf;

	for (cap = captab; cap->name; ++cap)
	{
		if(cap->cap & cap_can_send)
		{
			tl = ircsprintf(t, "%s ", cap->name);
			t += tl;
		}
	}

	t--;
	*t = '\0';
	sendto_one(client_p, "CAPAB :%s", msgbuf);
}

/*
 * show_capabilities - show current server capabilities
 *
 * inputs       - pointer to an struct Client
 * output       - pointer to static string
 * side effects - build up string representing capabilities of server listed
 */
const char *
show_capabilities(struct Client *target_p)
{
	static char msgbuf[BUFSIZE];
	struct Capability *cap;
	char *t;
	int tl;

	t = msgbuf;
	tl = ircsprintf(msgbuf, "TS ");
	t += tl;

	if(!target_p->localClient->caps)	/* short circuit if no caps */
	{
		msgbuf[2] = '\0';
		return msgbuf;
	}

	for (cap = captab; cap->cap; ++cap)
	{
		if(cap->cap & target_p->localClient->caps)
		{
			tl = ircsprintf(t, "%s ", cap->name);
			t += tl;
		}
	}

	t--;
	*t = '\0';

	return msgbuf;
}


/*
 * set_autoconn - set autoconnect mode
 *
 * inputs       - struct Client pointer to oper requesting change
 *              -
 * output       - none
 * side effects -
 */
void
set_autoconn(struct Client *source_p, char *parv0, char *name, int newval)
{
	struct ConfItem *aconf;

	if(name && (aconf = find_conf_by_name(name, CONF_SERVER)))
	{
		if(newval)
			aconf->flags |= CONF_FLAGS_ALLOW_AUTO_CONN;
		else
			aconf->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN;

		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s has changed AUTOCONN for %s to %i", parv0, name, newval);
		sendto_one(source_p,
			   ":%s NOTICE %s :AUTOCONN for %s is now set to %i",
			   me.name, parv0, name, newval);
	}
	else if(name)
	{
		sendto_one(source_p, ":%s NOTICE %s :Can't find %s", me.name, parv0, name);
	}
	else
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Please specify a server name!", me.name, parv0);
	}
}

/*
 * New server connection code
 * Based upon the stuff floating about in s_bsd.c
 *   -- adrian
 */

/*
 * serv_connect() - initiate a server connection
 *
 * inputs	- pointer to conf 
 *		- pointer to client doing the connet
 * output	-
 * side effects	-
 *
 * This code initiates a connection to a server. It first checks to make
 * sure the given server exists. If this is the case, it creates a socket,
 * creates a client, saves the socket information in the client, and
 * initiates a connection to the server through comm_connect_tcp(). The
 * completion of this goes through serv_completed_connection().
 *
 * We return 1 if the connection is attempted, since we don't know whether
 * it suceeded or not, and 0 if it fails in here somewhere.
 */
int
serv_connect(struct ConfItem *aconf, struct Client *by)
{
	struct Client *client_p;
	struct sockaddr_storage myipnum; 
	int fd;
	/* Make sure aconf is useful */
	s_assert(aconf != NULL);
	if(aconf == NULL)
		return 0;

	/* log */
	inetntop_sock(&aconf->ipnum, buf, sizeof(buf));
	ilog(L_NOTICE, "Connect to %s[%s] @%s", aconf->user, aconf->host, buf);

	/*
	 * Make sure this server isn't already connected
	 * Note: aconf should ALWAYS be a valid C: line
	 */
	if((client_p = find_server(aconf->name)))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Server %s already present from %s",
				     aconf->name, get_client_name(client_p, SHOW_IP));
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Server %s already present from %s",
				     aconf->name, get_client_name(client_p, MASK_IP));
		if(by && IsPerson(by) && !MyClient(by))
			sendto_one_notice(by, ":Server %s already present from %s",
					  aconf->name, get_client_name(client_p, MASK_IP));
		return 0;
	}

	/* create a socket for the server connection */
	if((fd = comm_open(aconf->ipnum.ss_family, SOCK_STREAM, 0, NULL)) < 0)
	{
		/* Eek, failure to create the socket */
		report_error(L_ALL, "opening stream socket to %s: %s", aconf->name, errno);
		return 0;
	}

	/* servernames are always guaranteed under HOSTLEN chars */
	fd_note(fd, "Server: %s", aconf->name);

	/* Create a local client */
	client_p = make_client(NULL);

	/* Copy in the server, hostname, fd */
	strlcpy(client_p->name, aconf->name, sizeof(client_p->name));
	strlcpy(client_p->host, aconf->host, sizeof(client_p->host));
	strlcpy(client_p->sockhost, buf, sizeof(client_p->sockhost));
	client_p->localClient->fd = fd;

	/*
	 * Set up the initial server evilness, ripped straight from
	 * connect_server(), so don't blame me for it being evil.
	 *   -- adrian
	 */

	if(!set_non_blocking(client_p->localClient->fd))
	{
		report_error(L_ADMIN, NONB_ERROR_MSG,
#ifdef HIDE_SERVERS_IPS
			     get_client_name(client_p, MASK_IP),
#else
			     get_client_name(client_p, SHOW_IP),
#endif
			     errno);
		report_error(L_OPER, NONB_ERROR_MSG, get_client_name(client_p, MASK_IP), errno);
	}

	if(!set_sock_buffers(client_p->localClient->fd, READBUF_SIZE))
	{
		report_error(L_ADMIN, SETBUF_ERROR_MSG,
#ifdef HIDE_SERVERS_IPS
			     get_client_name(client_p, MASK_IP),
#else
			     get_client_name(client_p, SHOW_IP),
#endif
			     errno);
		report_error(L_OPER, SETBUF_ERROR_MSG, get_client_name(client_p, MASK_IP), errno);
	}

	/*
	 * Attach config entries to client here rather than in
	 * serv_connect_callback(). This to avoid null pointer references.
	 */
	if(!attach_connect_block(client_p, aconf->name, aconf->host))
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Host %s is not enabled for connecting:no C/N-line",
				     aconf->name);
		if(by && IsPerson(by) && !MyClient(by))
			sendto_one_notice(by, ":Connect to host %s failed.",
					  client_p->name);
		detach_conf(client_p);
		free_client(client_p);
		return 0;
	}
	/*
	 * at this point we have a connection in progress and C/N lines
	 * attached to the client, the socket info should be saved in the
	 * client and it should either be resolved or have a valid address.
	 *
	 * The socket has been connected or connect is in progress.
	 */
	make_server(client_p);
	if(by && IsPerson(by))
	{
		strcpy(client_p->serv->by, by->name);
		if(client_p->serv->user)
			free_user(client_p->serv->user, NULL);
		client_p->serv->user = by->user;
		by->user->refcnt++;
	}
	else
	{
		strcpy(client_p->serv->by, "AutoConn.");
		if(client_p->serv->user)
			free_user(client_p->serv->user, NULL);
		client_p->serv->user = NULL;
	}
	client_p->serv->up = me.name;
	client_p->serv->upid = me.id;
	SetConnecting(client_p);
	dlinkAddTail(client_p, &client_p->node, &global_client_list);


	if(IsConfVhosted(aconf))
	{
		memcpy(&myipnum, &aconf->my_ipnum, sizeof(myipnum));
		((struct sockaddr_in *)&myipnum)->sin_port = 0;
		myipnum.ss_family = aconf->my_ipnum.ss_family;
				
	}
	else if(aconf->aftype == AF_INET && ServerInfo.specific_ipv4_vhost)
	{
		memcpy(&myipnum, &ServerInfo.ip, sizeof(myipnum));
		((struct sockaddr_in *)&myipnum)->sin_port = 0;
		myipnum.ss_family = aconf->my_ipnum.ss_family;
	}
	
#ifdef IPV6
	else if((aconf->aftype == AF_INET6) && ServerInfo.specific_ipv6_vhost)
	{
		memcpy(&myipnum, &ServerInfo.ip6, sizeof(myipnum));
		((struct sockaddr_in6 *)&myipnum)->sin6_port = 0;
		myipnum.ss_family = AF_INET6;
	}
#endif
	else
	{
		comm_connect_tcp(client_p->localClient->fd, aconf->host,
				 aconf->port, NULL, 0, serv_connect_callback, 
				 client_p, aconf->aftype, 
				 ConfigFileEntry.connect_timeout);
		 return 1;
	}

	comm_connect_tcp(client_p->localClient->fd, aconf->host,
			 aconf->port, (struct sockaddr *) &myipnum,
			 GET_SS_LEN(myipnum), serv_connect_callback, client_p,
			 myipnum.ss_family, ConfigFileEntry.connect_timeout);

	return 1;
}

/*
 * serv_connect_callback() - complete a server connection.
 * 
 * This routine is called after the server connection attempt has
 * completed. If unsucessful, an error is sent to ops and the client
 * is closed. If sucessful, it goes through the initialisation/check
 * procedures, the capabilities are sent, and the socket is then
 * marked for reading.
 */
static void
serv_connect_callback(int fd, int status, void *data)
{
	struct Client *client_p = data;
	struct ConfItem *aconf;

	/* First, make sure its a real client! */
	s_assert(client_p != NULL);
	s_assert(client_p->localClient->fd == fd);

	if(client_p == NULL)
		return;

	/* Next, for backward purposes, record the ip of the server */
#ifdef IPV6
	if(fd_table[fd].connect.hostaddr.ss_family == AF_INET6)
	{
		struct sockaddr_in6 *lip = (struct sockaddr_in6 *)&client_p->localClient->ip;
		struct sockaddr_in6 *hip = (struct sockaddr_in6 *)&fd_table[fd].connect.hostaddr;	
		memcpy(&lip->sin6_addr, &hip->sin6_addr, sizeof(struct in6_addr));
	} else
#else
	{
		struct sockaddr_in *lip = (struct sockaddr_in *)&client_p->localClient->ip;
		struct sockaddr_in *hip = (struct sockaddr_in *)&fd_table[fd].connect.hostaddr;	
		lip->sin_addr.s_addr = hip->sin_addr.s_addr;
	}	
#endif	
	
	/* Check the status */
	if(status != COMM_OK)
	{
		/* We have an error, so report it and quit */
		/* Admins get to see any IP, mere opers don't *sigh*
		 */
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Error connecting to %s[%s]: %s (%s)",
				     client_p->name, client_p->host,
				     comm_errstr(status), strerror(errno));
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Error connecting to %s: %s (%s)",
				     client_p->name, comm_errstr(status), strerror(errno));
		exit_client(client_p, client_p, &me, comm_errstr(status));
		return;
	}

	/* COMM_OK, so continue the connection procedure */
	/* Get the C/N lines */
	aconf = client_p->localClient->att_conf;

	if((aconf == NULL) || ((aconf->status & CONF_SERVER) == 0) ||
	   irccmp(aconf->name, client_p->name) || !match(aconf->name, client_p->name))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN, "Lost connect{} block for %s",
#ifdef HIDE_SERVERS_IPS
				     get_client_name(client_p, MASK_IP));
#else
				     get_client_name(client_p, HIDE_IP));
#endif
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Lost connect{} block for %s",
				     get_client_name(client_p, MASK_IP));
		exit_client(client_p, client_p, &me, "Lost connect{} block");
		return;
	}

	/* Next, send the initial handshake */
	SetHandshake(client_p);

	/*
	 * jdc -- Check and send spasswd, not passwd.
	 */
	if(!EmptyString(aconf->spasswd))
	{
		/* kludge, if we're not using TS6, dont ever send
		 * ourselves as being TS6 capable.
		 */
		if(ServerInfo.use_ts6)
			sendto_one(client_p, "PASS %s TS %d :%s", 
				   aconf->spasswd, TS_CURRENT, me.id);
		else
			sendto_one(client_p, "PASS %s :TS",
				   aconf->spasswd);
	}

	/*
	 * Pass my info to the new server
	 *
	 */

	send_capabilities(client_p, aconf, default_server_capabs
			  | ((aconf->flags & CONF_FLAGS_COMPRESSED) ? CAP_ZIP_SUPPORTED : 0));

	sendto_one(client_p, "SERVER %s 1 :%s%s",
		   me.name,
		   ConfigServerHide.hidden ? "(H) " : "", me.info);

	/* 
	 * If we've been marked dead because a send failed, just exit
	 * here now and save everyone the trouble of us ever existing.
	 */
	if(IsDeadorAborted(client_p))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "%s[%s] went dead during handshake",
				     client_p->name, client_p->host);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "%s went dead during handshake", client_p->name);
		exit_client(client_p, client_p, &me, "Went dead during handshake");
		return;
	}

	/* don't move to serv_list yet -- we haven't sent a burst! */

	/* If we get here, we're ok, so lets start reading some data */
	comm_setselect(fd, FDLIST_SERVER, COMM_SELECT_READ, read_packet, client_p, 0);
}

