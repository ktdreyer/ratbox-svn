/*
 *  ircd-ratbox: A slightly useful ircd.
 *  client.c: Controls clients.
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
#include "stdinc.h"
#include "tools.h"
#include "struct.h"
#include "client.h"
#include "channel.h"
#include "class.h"
#include "event.h"
#include "hash.h"
#include "irc_string.h"
#include "snprintf.h"
#include "ircd.h"
#include "s_gline.h"
#include "numeric.h"
#include "commio.h"
#include "packet.h"
#include "s_auth.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_stats.h"
#include "send.h"
#include "whowas.h"
#include "s_user.h"
#include "hash.h"
#include "hostmask.h"
#include "balloc.h"
#include "listener.h"
#include "hook.h"
#include "monitor.h"
#include "linebuf.h"
#include "parse.h"

#define DEBUG_EXITED_CLIENTS

static void check_pings_list(dlink_list * list);
static void check_unknowns_list(dlink_list * list);
static void free_exited_clients(void *unused);
static void exit_aborted_clients(void *unused);

static int exit_remote_client(struct Client *, struct Client *, struct Client *,const char *);
static int exit_remote_server(struct Client *, struct Client *, struct Client *,const char *);
static int exit_local_client(struct Client *, struct Client *, struct Client *,const char *);
static int exit_unknown_client(struct Client *, struct Client *, struct Client *,const char *);
static int exit_local_server(struct Client *, struct Client *, struct Client *,const char *);
static int qs_server(struct Client *, struct Client *, struct Client *, const char *comment);

static EVH check_pings;

static BlockHeap *client_heap = NULL;
static BlockHeap *lclient_heap = NULL;

static char current_uid[IDLEN];

dlink_list dead_list;
#ifdef DEBUG_EXITED_CLIENTS
static dlink_list dead_remote_list;
#endif

struct abort_client
{
 	dlink_node node;
  	struct Client *client;
  	char notice[REASONLEN];
};

static dlink_list abort_list;


/*
 * init_client
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	- initialize client free memory
 */
void
init_client(void)
{
	/*
	 * start off the check ping event ..  -- adrian
	 * Every 30 seconds is plenty -- db
	 */
	client_heap = BlockHeapCreate(sizeof(struct Client), CLIENT_HEAP_SIZE);
	lclient_heap = BlockHeapCreate(sizeof(struct LocalUser), LCLIENT_HEAP_SIZE);
	eventAddIsh("check_pings", check_pings, NULL, 30);
	eventAddIsh("free_exited_clients", &free_exited_clients, NULL, 4);
	eventAddIsh("exit_aborted_clients", exit_aborted_clients, NULL, 1);
}


/*
 * make_client - create a new Client struct and set it to initial state.
 *
 *      from == NULL,   create local client (a client connected
 *                      to a socket).
 *
 *      from,   create remote client (behind a socket
 *                      associated with the client defined by
 *                      'from'). ('from' is a local client!!).
 */
struct Client *
make_client(struct Client *from)
{
	struct Client *client_p = NULL;
	struct LocalUser *localClient;

	client_p = BlockHeapAlloc(client_heap);

	if(from == NULL)
	{
		client_p->from = client_p;	/* 'from' of local client is self! */

		localClient = (struct LocalUser *) BlockHeapAlloc(lclient_heap);
		SetMyConnect(client_p);
		client_p->localClient = localClient;

		client_p->localClient->lasttime = client_p->localClient->firsttime = CurrentTime;

		client_p->localClient->fd = -1;
		client_p->localClient->ctrlfd = -1;

		/* as good a place as any... */
		dlinkAdd(client_p, &client_p->localClient->tnode, &unknown_list);
	}
	else
	{			/* from is not NULL */
		client_p->localClient = NULL;
		client_p->from = from;	/* 'from' of local client is self! */
	}
	
	SetUnknown(client_p);
	strcpy(client_p->username, "unknown");

	return client_p;
}

static void
free_local_client(struct Client *client_p)
{
	s_assert(NULL != client_p);
	s_assert(&me != client_p);

	if(client_p->localClient == NULL)
		return;

	/*
	 * clean up extra sockets from P-lines which have been discarded.
	 */
	if(client_p->localClient->listener)
	{
		s_assert(0 < client_p->localClient->listener->ref_count);
		if(0 == --client_p->localClient->listener->ref_count
		   && !client_p->localClient->listener->active)
			free_listener(client_p->localClient->listener);
		client_p->localClient->listener = 0;
	}

	if(client_p->localClient->fd >= 0)
		comm_close(client_p->localClient->fd);

	if(client_p->localClient->passwd)
	{
		memset(client_p->localClient->passwd, 0,
			strlen(client_p->localClient->passwd));
		MyFree(client_p->localClient->passwd);
	}

	MyFree(client_p->localClient->fullcaps);
	MyFree(client_p->localClient->auth_oper);
	MyFree(client_p->localClient->response);
	MyFree(client_p->localClient->opername);

	BlockHeapFree(lclient_heap, client_p->localClient);
	client_p->localClient = NULL;
}

void
free_client(struct Client *client_p)
{
	s_assert(NULL != client_p);
	s_assert(&me != client_p);
	free_local_client(client_p);
	BlockHeapFree(client_heap, client_p);
}

/*
 * check_pings - go through the local client list and check activity
 * kill off stuff that should die
 *
 * inputs       - NOT USED (from event)
 * output       - next time_t when check_pings() should be called again
 * side effects - 
 *
 *
 * A PING can be sent to clients as necessary.
 *
 * Client/Server ping outs are handled.
 */

/*
 * Addon from adrian. We used to call this after nextping seconds,
 * however I've changed it to run once a second. This is only for
 * PING timeouts, not K/etc-line checks (thanks dianora!). Having it
 * run once a second makes life a lot easier - when a new client connects
 * and they need a ping in 4 seconds, if nextping was set to 20 seconds
 * we end up waiting 20 seconds. This is stupid. :-)
 * I will optimise (hah!) check_pings() once I've finished working on
 * tidying up other network IO evilnesses.
 *     -- adrian
 */

static void
check_pings(void *notused)
{
	check_pings_list(&lclient_list);
	check_pings_list(&serv_list);
	check_unknowns_list(&unknown_list);
}

/*
 * Check_pings_list()
 *
 * inputs	- pointer to list to check
 * output	- NONE
 * side effects	- 
 */
static void
check_pings_list(dlink_list * list)
{
	char scratch[32];	/* way too generous but... */
	struct Client *client_p;	/* current local client_p being examined */
	int ping = 0;		/* ping time value from client */
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, list->head)
	{
		client_p = ptr->data;

		/*
		 ** Note: No need to notify opers here. It's
		 ** already done when "FLAGS_DEADSOCKET" is set.
		 */
		if(!MyConnect(client_p) || IsDead(client_p))
			continue;

		if(IsPerson(client_p))
		{
			if(!IsExemptKline(client_p) &&
			   GlobalSetOptions.idletime &&
			   !IsOper(client_p) &&
			   !IsIdlelined(client_p) &&
			   ((CurrentTime - client_p->localClient->last) > GlobalSetOptions.idletime))
			{
				struct ConfItem *aconf;

				aconf = make_conf();
				aconf->status = CONF_KILL;

				DupString(aconf->host, client_p->host);
				DupString(aconf->passwd, "idle exceeder");
				DupString(aconf->user, client_p->username);
				aconf->port = 0;
				aconf->hold = CurrentTime + 60;
				add_temp_kline(aconf);
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Idle time limit exceeded for %s - temp k-lining",
						     get_client_name(client_p, HIDE_IP));

				exit_client(client_p, client_p, &me, aconf->passwd);
				continue;
			}
		}

		if(!IsRegistered(client_p))
			ping = ConfigFileEntry.connect_timeout;
		else
			ping = get_client_ping(client_p);

		if(ping < (CurrentTime - client_p->localClient->lasttime))
		{
			/*
			 * If the client/server hasnt talked to us in 2*ping seconds
			 * and it has a ping time, then close its connection.
			 */
			if(((CurrentTime - client_p->localClient->lasttime) >= (2 * ping)
			    && (client_p->flags & FLAGS_PINGSENT)))
			{
				if(IsAnyServer(client_p))
				{
					sendto_realops_flags(UMODE_ALL, L_ALL,
							     "No response from %s, closing link",
							     get_server_name(client_p, HIDE_IP));
					ilog(L_SERVER,
					     "No response from %s, closing link",
					     log_client_name(client_p, HIDE_IP));
				}
				(void) ircsnprintf(scratch, sizeof(scratch),
						  "Ping timeout: %d seconds",
						  (int) (CurrentTime - client_p->localClient->lasttime));

				exit_client(client_p, client_p, &me, scratch);
				continue;
			}
			else if((client_p->flags & FLAGS_PINGSENT) == 0)
			{
				/*
				 * if we havent PINGed the connection and we havent
				 * heard from it in a while, PING it to make sure
				 * it is still alive.
				 */
				client_p->flags |= FLAGS_PINGSENT;
				/* not nice but does the job */
				client_p->localClient->lasttime = CurrentTime - ping;
				sendto_one(client_p, POP_QUEUE, "PING :%s", me.name);
			}
		}
		/* ping_timeout: */

	}
}

/*
 * check_unknowns_list
 *
 * inputs	- pointer to list of unknown clients
 * output	- NONE
 * side effects	- unknown clients get marked for termination after n seconds
 */
static void
check_unknowns_list(dlink_list * list)
{
	dlink_node *ptr, *next_ptr;
	struct Client *client_p;

	DLINK_FOREACH_SAFE(ptr, next_ptr, list->head)
	{
		client_p = ptr->data;

		if(IsDead(client_p) || IsClosing(client_p))
			continue;

		/*
		 * Check UNKNOWN connections - if they have been in this state
		 * for > 30s, close them.
		 */

		if((CurrentTime - client_p->localClient->firsttime) > 30)
			exit_client(client_p, client_p, &me, "Connection timed out");
	}
}

void
notify_banned_client(struct Client *client_p, struct ConfItem *aconf, int ban)
{
	static const char conn_closed[] = "Connection closed";
	static const char d_lined[] = "D-lined";
	static const char k_lined[] = "K-lined";
	static const char g_lined[] = "G-lined";
	const char *reason = NULL;
	const char *exit_reason = conn_closed;

	if(ConfigFileEntry.kline_with_reason && !EmptyString(aconf->passwd))
	{
		reason = aconf->passwd;
		exit_reason = aconf->passwd;
	}
	else
	{
		switch (ban)
		{
		case NOTIFY_BANNED_DLINE:
			reason = d_lined;
			break;
		case NOTIFY_BANNED_GLINE:
			reason = g_lined;
			break;
		default:
			reason = k_lined;
			break;
		}
	}

	if(ban == NOTIFY_BANNED_DLINE && !IsPerson(client_p))
		sendto_one(client_p, POP_QUEUE, "NOTICE DLINE :*** You have been D-lined");
	else
		sendto_one(client_p, POP_QUEUE, form_str(ERR_YOUREBANNEDCREEP),
			   me.name, client_p->name, reason);
	
	exit_client(client_p, client_p, &me, 
			EmptyString(ConfigFileEntry.kline_reason) ? exit_reason :
			 ConfigFileEntry.kline_reason);
}

/*
 * check_banned_lines
 * inputs	- NONE
 * output	- NONE
 * side effects - Check all connections for a pending k/d/gline against the
 * 		  client, exit the client if found.
 */
void
check_banned_lines(void)
{
	struct Client *client_p;	/* current local client_p being examined */
	struct ConfItem *aconf = NULL;
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, lclient_list.head)
	{
		client_p = ptr->data;

		if(IsMe(client_p))
			continue;

		/* if there is a returned struct ConfItem then kill it */
		if((aconf = find_dline((struct sockaddr *)&client_p->localClient->ip, client_p->localClient->ip.ss_family)))
		{
			if(aconf->status & CONF_EXEMPTDLINE)
				continue;

			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "DLINE active for %s",
					     get_client_name(client_p, HIDE_IP));

			notify_banned_client(client_p, aconf, NOTIFY_BANNED_DLINE);
			continue;	/* and go examine next fd/client_p */
		}

		if(!IsPerson(client_p))
			continue;

		if((aconf = find_kline(client_p)) != NULL)
		{
			if(IsExemptKline(client_p))
			{
				sendto_realops_flags(UMODE_ALL, L_ALL,
						"KLINE over-ruled for %s, client is kline_exempt [%s@%s]",
						get_client_name(client_p, HIDE_IP),
						aconf->user, aconf->host);
				continue;
			}

			sendto_realops_flags(UMODE_ALL, L_ALL,
					"KLINE active for %s",
					get_client_name(client_p, HIDE_IP));
			notify_banned_client(client_p, aconf, NOTIFY_BANNED_KLINE);
			continue;
		}
		else if((aconf = find_gline(client_p)) != NULL)
		{
			if(IsExemptKline(client_p))
			{
				sendto_realops_flags(UMODE_ALL, L_ALL,
						"GLINE over-ruled for %s, client is kline_exempt [%s@%s]",
						get_client_name(client_p, HIDE_IP),
						aconf->user, aconf->host);
				continue;
			}

			if(IsExemptGline(client_p))
			{
				sendto_realops_flags(UMODE_ALL, L_ALL,
						"GLINE over-ruled for %s, client is gline_exempt [%s@%s]",
						get_client_name(client_p, HIDE_IP),
						aconf->user, aconf->host);
				continue;
			}

			sendto_realops_flags(UMODE_ALL, L_ALL,
					"GLINE active for %s",
					get_client_name(client_p, HIDE_IP));

			notify_banned_client(client_p, aconf, NOTIFY_BANNED_GLINE);
			continue;
		}
		else if((aconf = find_xline(client_p->info, 1)) != NULL)
		{
			if(IsExemptKline(client_p))
			{
				sendto_realops_flags(UMODE_ALL, L_ALL,
						"XLINE over-ruled for %s, client is kline_exempt [%s]",
						get_client_name(client_p, HIDE_IP),
						aconf->name);
				continue;
			}

			sendto_realops_flags(UMODE_ALL, L_ALL, "XLINE active for %s",
					get_client_name(client_p, HIDE_IP));

			(void) exit_client(client_p, client_p, &me, "Bad user info");
			continue;
		}
	}

	/* also check the unknowns list for new dlines */
	DLINK_FOREACH_SAFE(ptr, next_ptr, unknown_list.head)
	{
		client_p = ptr->data;

		if((aconf = find_dline((struct sockaddr *)&client_p->localClient->ip,client_p->localClient->ip.ss_family)))
		{
			if(aconf->status & CONF_EXEMPTDLINE)
				continue;

			notify_banned_client(client_p, aconf, NOTIFY_BANNED_DLINE);
		}
	}

}

/* check_klines_event()
 *
 * inputs	-
 * outputs	-
 * side effects - check_klines() is called, kline_queued unset
 */
void
check_klines_event(void *unused)
{
	kline_queued = 0;
	check_klines();
}

/* check_klines
 *
 * inputs       -
 * outputs      -
 * side effects - all clients will be checked for klines
 */
void
check_klines(void)
{
	struct Client *client_p;
	struct ConfItem *aconf;
	dlink_node *ptr;
	dlink_node *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, lclient_list.head)
	{
		client_p = ptr->data;

		if(IsMe(client_p) || !IsPerson(client_p))
			continue;

		if((aconf = find_kline(client_p)) != NULL)
		{
			if(IsExemptKline(client_p))
			{
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "KLINE over-ruled for %s, client is kline_exempt",
						     get_client_name(client_p, HIDE_IP));
				continue;
			}

			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "KLINE active for %s",
					     get_client_name(client_p, HIDE_IP));

			notify_banned_client(client_p, aconf, NOTIFY_BANNED_KLINE);
			continue;
		}
	}
}

/*
 * update_client_exit_stats
 *
 * input	- pointer to client
 * output	- NONE
 * side effects	- 
 */
static void
update_client_exit_stats(struct Client *client_p)
{
	if(IsServer(client_p))
	{
		sendto_realops_flags(UMODE_EXTERNAL, L_ALL,
				     "Server %s split from %s",
				     client_p->name, client_p->servptr->name);
		if(HasSentEob(client_p))
			eob_count--;
	}
	else if(IsClient(client_p))
	{
		--Count.total;
		if(IsOper(client_p))
			--Count.oper;
		if(IsInvisible(client_p))
			--Count.invisi;
	}

	if(splitchecking && !splitmode)
		check_splitmode(NULL);
}

/*
 * release_client_state
 *
 * input	- pointer to client to release
 * output	- NONE
 * side effects	- 
 */
static void
release_client_state(struct Client *client_p)
{
	if(client_p->user != NULL)
	{
		free_user(client_p->user, client_p);	/* try this here */
	}
	if(client_p->serv)
	{
		if(client_p->serv->user != NULL)
			free_user(client_p->serv->user, client_p);
		if(client_p->serv->fullcaps)
			MyFree(client_p->serv->fullcaps);
		MyFree(client_p->serv);
	}
}

/*
 * remove_client_from_list
 * inputs	- point to client to remove
 * output	- NONE
 * side effects - taken the code from ExitOneClient() for this
 *		  and placed it here. - avalon
 */
static void
remove_client_from_list(struct Client *client_p)
{
	s_assert(NULL != client_p);

	if(client_p == NULL)
		return;

	/* A client made with make_client()
	 * is on the unknown_list until removed.
	 * If it =does= happen to exit before its removed from that list
	 * and its =not= on the global_client_list, it will core here.
	 * short circuit that case now -db
	 */
	if(client_p->node.prev == NULL && client_p->node.next == NULL)
		return;

	dlinkDelete(&client_p->node, &global_client_list);

	update_client_exit_stats(client_p);
}


/*
 * find_person	- find person by (nick)name.
 * inputs	- pointer to name
 * output	- return client pointer
 * side effects -
 */
struct Client *
find_person(const char *name)
{
	struct Client *c2ptr;

	c2ptr = find_client(name);

	if(c2ptr && IsPerson(c2ptr))
		return (c2ptr);
	return (NULL);
}

struct Client *
find_named_person(const char *name)
{
	struct Client *c2ptr;

	c2ptr = find_named_client(name);

	if(c2ptr && IsPerson(c2ptr))
		return (c2ptr);
	return (NULL);
}


/*
 * find_chasing - find the client structure for a nick name (user) 
 *      using history mechanism if necessary. If the client is not found, 
 *      an error message (NO SUCH NICK) is generated. If the client was found
 *      through the history, chasing will be 1 and otherwise 0.
 */
struct Client *
find_chasing(struct Client *source_p, const char *user, int *chasing)
{
	struct Client *who;

	if(MyClient(source_p))
		who = find_named_person(user);
	else
		who = find_person(user);

	if(chasing)
		*chasing = 0;

	if(who || IsDigit(*user))
		return who;

	if(!(who = get_history(user, (long) KILLCHASETIMELIMIT)))
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOSUCHNICK,
				   form_str(ERR_NOSUCHNICK), user);
		return (NULL);
	}
	if(chasing)
		*chasing = 1;
	return who;
}

/*
 * get_client_name -  Return the name of the client
 *    for various tracking and
 *      admin purposes. The main purpose of this function is to
 *      return the "socket host" name of the client, if that
 *        differs from the advertised name (other than case).
 *        But, this can be used to any client structure.
 *
 * NOTE 1:
 *        Watch out the allocation of "nbuf", if either source_p->name
 *        or source_p->sockhost gets changed into pointers instead of
 *        directly allocated within the structure...
 *
 * NOTE 2:
 *        Function return either a pointer to the structure (source_p) or
 *        to internal buffer (nbuf). *NEVER* use the returned pointer
 *        to modify what it points!!!
 */

const char *
get_client_name(struct Client *client, int showip)
{
	static char nbuf[HOSTLEN * 2 + USERLEN + 5];

	s_assert(NULL != client);
	if(client == NULL)
		return NULL;

	if(MyConnect(client))
	{
		if(!irccmp(client->name, client->host))
			return client->name;

#ifdef HIDE_SPOOF_IPS
		if(showip == SHOW_IP && IsIPSpoof(client))
			showip = MASK_IP;
#endif

		/* And finally, let's get the host information, ip or name */
		switch (showip)
		{
		case SHOW_IP:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@%s]", 
				   client->name, client->username, 
				   client->sockhost);
			break;
		case MASK_IP:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@255.255.255.255]",
				   client->name, client->username);
			break;
		default:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@%s]",
				   client->name, client->username, client->host);
		}
		return nbuf;
	}

	/* As pointed out by Adel Mezibra 
	 * Neph|l|m@EFnet. Was missing a return here.
	 */
	return client->name;
}

const char *
get_server_name(struct Client *target_p, int showip)
{
	static char nbuf[HOSTLEN * 2 + USERLEN + 5];

	if(target_p == NULL)
		return NULL;

	if(!MyConnect(target_p) || !irccmp(target_p->name, target_p->host))
		return target_p->name;

#ifdef HIDE_SERVERS_IPS
	if(EmptyString(target_p->name))
	{
		ircsnprintf(nbuf, sizeof(nbuf), "[%s@255.255.255.255]",
				target_p->username);
		return nbuf;
	}
	else
		return target_p->name;
#endif

	switch (showip)
	{
		case SHOW_IP:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@%s]",
				target_p->name, target_p->username, 
				target_p->sockhost);
			break;

		case MASK_IP:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@255.255.255.255]",
				target_p->name, target_p->username);

		default:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@%s]",
				target_p->name, target_p->username,
				target_p->host);
	}

	return nbuf;
}
	
/* log_client_name()
 *
 * This version is the same as get_client_name, but doesnt contain the
 * code that will hide IPs always.  This should be used for logfiles.
 */
const char *
log_client_name(struct Client *target_p, int showip)
{
	static char nbuf[HOSTLEN * 2 + USERLEN + 5];

	if(target_p == NULL)
		return NULL;

	if(MyConnect(target_p))
	{
		if(irccmp(target_p->name, target_p->host) == 0)
			return target_p->name;

		switch (showip)
		{
		case SHOW_IP:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@%s]", target_p->name,
				   target_p->username, target_p->sockhost);
			break;

		case MASK_IP:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@255.255.255.255]",
				   target_p->name, target_p->username);

		default:
			ircsnprintf(nbuf, sizeof(nbuf), "%s[%s@%s]", target_p->name,
				   target_p->username, target_p->host);
		}

		return nbuf;
	}

	return target_p->name;
}

static void
free_exited_clients(void *unused)
{
	dlink_node *ptr, *next;
	struct Client *target_p;

	DLINK_FOREACH_SAFE(ptr, next, dead_list.head)
	{
		target_p = ptr->data;

#ifdef DEBUG_EXITED_CLIENTS
		{
			struct abort_client *abt;
			dlink_node *aptr;
			int found = 0;

			DLINK_FOREACH(aptr, abort_list.head)
			{
				abt = aptr->data;
				if(abt->client == target_p)
				{
					s_assert(0);
					sendto_realops_flags(UMODE_ALL, L_ALL, 
						"On abort_list: %s stat: %u flags: %u/%u handler: %c",
						target_p->name, (unsigned int) target_p->status,
						target_p->flags, target_p->flags2, target_p->handler);
					sendto_realops_flags(UMODE_ALL, L_ALL,
						"Please report this to the ratbox developers!");
					found++;
				}
			}

			if(found)
			{
				dlinkDestroy(ptr, &dead_list);
				continue;
			}
		}
#endif

		if(ptr->data == NULL)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Warning: null client on dead_list!");
			dlinkDestroy(ptr, &dead_list);
			continue;
		}
		release_client_state(target_p);
		free_client(target_p);
		dlinkDestroy(ptr, &dead_list);
	}

#ifdef DEBUG_EXITED_CLIENTS
	DLINK_FOREACH_SAFE(ptr, next, dead_remote_list.head)
	{
		target_p = ptr->data;

		if(ptr->data == NULL)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Warning: null client on dead_list!");
			dlinkDestroy(ptr, &dead_list);
			continue;
		}
		release_client_state(target_p);
		free_client(target_p);
		dlinkDestroy(ptr, &dead_remote_list);
	}
#endif
	
}

/*
** Recursively send QUITs and SQUITs for source_p and all its dependent clients
** and servers to those servers that need them.  A server needs the client
** QUITs if it can't figure them out from the SQUIT (ie pre-TS4) or if it
** isn't getting the SQUIT because of @#(*&@)# hostmasking.  With TS4, once
** a link gets a SQUIT, it doesn't need any QUIT/SQUITs for clients depending
** on that one -orabidoo
*/
static void
recurse_send_quits(struct Client *client_p, struct Client *source_p, 
		   struct Client *to, const char *comment,
		   const char *myname)
{
	struct Client *target_p;
	dlink_node *ptr, *ptr_next;
	/* If this server can handle quit storm (QS) removal
	 * of dependents, just send the SQUIT
	 */

	if(IsCapable(to, CAP_QS))
	{
		sendto_one(to, POP_QUEUE, "SQUIT %s :%s", 
			   get_id(source_p, to), me.name);
	}
	else
	{
		DLINK_FOREACH_SAFE(ptr, ptr_next, source_p->serv->users.head)
		{
			target_p = ptr->data;
			sendto_one(to, POP_QUEUE, ":%s QUIT :%s", target_p->name, comment);
		}
		DLINK_FOREACH_SAFE(ptr, ptr_next, source_p->serv->servers.head)
		{
			target_p = ptr->data;
			recurse_send_quits(client_p, target_p, to, comment, myname);
		}
		if(!match(myname, source_p->name))
			sendto_one(to, POP_QUEUE, "SQUIT %s :%s", source_p->name, me.name);
	}
}

/* 
** Remove all clients that depend on source_p; assumes all (S)QUITs have
** already been sent.  we make sure to exit a server's dependent clients 
** and servers before the server itself; exit_one_client takes care of 
** actually removing things off llists.   tweaked from +CSr31  -orabidoo
*/
/*
 * added sanity test code.... source_p->serv might be NULL...
 */
static void
recurse_remove_clients(struct Client *source_p, const char *comment)
{
	struct Client *target_p;
	dlink_node *ptr, *ptr_next;

	if(IsMe(source_p))
		return;

	if(source_p->serv == NULL)	/* oooops. uh this is actually a major bug */
		return;

	/* this is very ugly, but it saves cpu :P */
	if(ConfigFileEntry.nick_delay > 0)
	{
		DLINK_FOREACH_SAFE(ptr, ptr_next, source_p->serv->users.head)
		{
			target_p = ptr->data;
			target_p->flags |= FLAGS_KILLED;
			add_nd_entry(target_p->name);

			if(!IsDead(target_p) && !IsClosing(target_p))
				exit_remote_client(NULL, target_p, &me, comment);
		}
	}
	else
	{
		DLINK_FOREACH_SAFE(ptr, ptr_next, source_p->serv->users.head)
		{
			target_p = ptr->data;
			target_p->flags |= FLAGS_KILLED;

			if(!IsDead(target_p) && !IsClosing(target_p))
				exit_remote_client(NULL, target_p, &me, comment);
		}
	}	

	DLINK_FOREACH_SAFE(ptr, ptr_next, source_p->serv->servers.head)
	{
		target_p = ptr->data;
		recurse_remove_clients(target_p, comment);
		qs_server(NULL, target_p, &me, comment);
	}
}

/*
** Remove *everything* that depends on source_p, from all lists, and sending
** all necessary QUITs and SQUITs.  source_p itself is still on the lists,
** and its SQUITs have been sent except for the upstream one  -orabidoo
*/
static void
remove_dependents(struct Client *client_p,
		  struct Client *source_p,
		  struct Client *from, const char *comment, const char *comment1)
{
	struct Client *to;
	static char myname[HOSTLEN + 1];
	dlink_node *ptr, *next;

	DLINK_FOREACH_SAFE(ptr, next, serv_list.head)
	{
		to = ptr->data;

		if(IsMe(to) || to == source_p->from || 
		   (to == client_p && IsCapable(to, CAP_QS)))
			continue;

		/* MyConnect(source_p) is rotten at this point: if source_p
		 * was mine, ->from is NULL. 
		 */
		/* The WALLOPS isn't needed here as pointed out by
		 * comstud, since m_squit already does the notification.
		 */

		strlcpy(myname, me.name, sizeof(myname));
		recurse_send_quits(client_p, source_p, to, comment1, myname);
	}

	recurse_remove_clients(source_p, comment1);
}

void
exit_aborted_clients(void *unused)
{
	struct abort_client *abt;
 	dlink_node *ptr, *next;
 	DLINK_FOREACH_SAFE(ptr, next, abort_list.head)
 	{
 	 	abt = ptr->data;

#ifdef DEBUG_EXITED_CLIENTS
		{
			if(dlinkFind(abt->client, &dead_list))
			{
				s_assert(0);
				sendto_realops_flags(UMODE_ALL, L_ALL, 
					"On dead_list: %s stat: %u flags: %u/%u handler: %c",
					abt->client->name, (unsigned int) abt->client->status,
					abt->client->flags, abt->client->flags2, abt->client->handler);
				sendto_realops_flags(UMODE_ALL, L_ALL,
					"Please report this to the ratbox developers!");
				continue;
			}
		}
#endif

 		s_assert(*((unsigned long*)abt->client) != 0xdeadbeef); /* This is lame but its a debug thing */
 	 	dlinkDelete(ptr, &abort_list);

 	 	if(IsAnyServer(abt->client))
 	 	 	sendto_realops_flags(UMODE_ALL, L_ALL,
  	 	 	                     "Closing link to %s: %s",
   	 	 	                     get_server_name(abt->client, HIDE_IP), abt->notice);

		/* its no longer on abort list - we *must* remove
		 * FLAGS_CLOSING otherwise exit_client() will not run --fl
		 */
		abt->client->flags &= ~FLAGS_CLOSING;
 	 	exit_client(abt->client, abt->client, &me, abt->notice);
 	 	MyFree(abt);
 	}
}


/*
 * dead_link - Adds client to a list of clients that need an exit_client()
 *
 */
void
dead_link(struct Client *client_p)
{
	struct abort_client *abt;

	s_assert(!IsMe(client_p));
	if(IsDead(client_p) || IsClosing(client_p) || IsMe(client_p))
		return;

	abt = (struct abort_client *) MyMalloc(sizeof(struct abort_client));

	if(client_p->flags & FLAGS_SENDQEX)
		strlcpy(abt->notice, "Max SendQ exceeded", sizeof(abt->notice));
	else
		ircsnprintf(abt->notice, sizeof(abt->notice), "Write error: %s", strerror(errno));

    	abt->client = client_p;
	SetIOError(client_p);
	SetDead(client_p);
	SetClosing(client_p);
	dlinkAdd(abt, &abt->node, &abort_list);
}


/* This does the remove of the user from channels..local or remote */
static inline void
exit_generic_client(struct Client *client_p, struct Client *source_p, struct Client *from,
		   const char *comment)
{
	dlink_node *ptr, *next_ptr;

	sendto_common_channels_local(source_p, ":%s!%s@%s QUIT :%s",
				     source_p->name,
				     source_p->username, source_p->host, comment);

	remove_user_from_channels(source_p);

	/* Should not be in any channels now */
	s_assert(source_p->user->channel.head == NULL);

	/* Clean up invitefield */
	DLINK_FOREACH_SAFE(ptr, next_ptr, source_p->user->invited.head)
	{
		del_invite(ptr->data, source_p);
	}

	/* Clean up allow lists */
	del_all_accepts(source_p);

	add_history(source_p, 0);
	off_history(source_p);

	monitor_signoff(source_p);

	if(has_id(source_p))
		del_from_id_hash(source_p->id, source_p);

	del_from_hostname_hash(source_p->host, source_p);
	del_from_client_hash(source_p->name, source_p);
	remove_client_from_list(source_p);
}

/* 
 * Assumes IsPerson(source_p) && !MyConnect(source_p)
 */

static int
exit_remote_client(struct Client *client_p, struct Client *source_p, struct Client *from,
		   const char *comment)
{
	exit_generic_client(client_p, source_p, from, comment);
	
	if(source_p->servptr && source_p->servptr->serv)
	{
		dlinkDelete(&source_p->lnode, &source_p->servptr->serv->users);
	}

	if((source_p->flags & FLAGS_KILLED) == 0)
	{
		sendto_server(client_p, NULL, CAP_TS6, NOCAPS,
			      ":%s QUIT :%s", use_id(source_p), comment);
		sendto_server(client_p, NULL, NOCAPS, CAP_TS6,
			      ":%s QUIT :%s", source_p->name, comment);
	}

	SetDead(source_p);
#ifdef DEBUG_EXITED_CLIENTS
	dlinkAddAlloc(source_p, &dead_remote_list);
#else
	dlinkAddAlloc(source_p, &dead_list);
#endif
	return(CLIENT_EXITED);
}

/*
 * This assumes IsUnknown(source_p) == TRUE and MyConnect(source_p) == TRUE
 */

static int
exit_unknown_client(struct Client *client_p, struct Client *source_p, struct Client *from,
		  const char *comment)
{
	delete_auth_queries(source_p);
	linebuf_donebuf(&client_p->localClient->buf_recvq);
	dlinkDelete(&source_p->localClient->tnode, &unknown_list);

	if(!IsIOError(source_p))
		sendto_one(source_p, POP_QUEUE, "ERROR :Closing Link: 127.0.0.1 (%s)", comment);

	close_connection(source_p);

	del_from_hostname_hash(source_p->host, source_p);
	del_from_client_hash(source_p->name, source_p);
	remove_client_from_list(source_p);
	SetDead(source_p);
	dlinkAddAlloc(source_p, &dead_list);

	/* Note that we don't need to add unknowns to the dead_list */
	return(CLIENT_EXITED);
}

static int
exit_remote_server(struct Client *client_p, struct Client *source_p, struct Client *from, 
		  const char *comment)
{
	static char comment1[(HOSTLEN*2)+2];
	struct Client *target_p;
	
	if((source_p->serv) && (source_p->serv->up))
		 strcpy(comment1, source_p->serv->up);
	else
		strcpy(comment1, "<Unknown>");
	
	strcat(comment1, " ");
	strcat(comment1, source_p->name);							        		                		                                                                      		
	if(source_p->serv != NULL)
		remove_dependents(client_p, source_p, from, comment, comment1);

	if(source_p->servptr && source_p->servptr->serv)
		dlinkDelete(&source_p->lnode, &source_p->servptr->serv->servers);
	else
		s_assert(0);

	dlinkFindDestroy(source_p, &global_serv_list);
	target_p = source_p->from;
	
	if(target_p != NULL && IsServer(target_p) && target_p != client_p &&
	   !IsMe(target_p) && (source_p->flags & FLAGS_KILLED) == 0)
	{
		sendto_one(target_p, POP_QUEUE, ":%s SQUIT %s :%s", 
			   get_id(from, target_p), get_id(source_p, target_p),
			   comment);
	}

	if(has_id(source_p))
		del_from_id_hash(source_p->id, source_p);

	del_from_client_hash(source_p->name, source_p);
	remove_client_from_list(source_p);  
	
	SetDead(source_p);
#ifdef DEBUG_EXITED_CLIENTS
	dlinkAddAlloc(source_p, &dead_remote_list);
#else
	dlinkAddAlloc(source_p, &dead_list);
#endif
	return 0;
}

static int
qs_server(struct Client *client_p, struct Client *source_p, struct Client *from, 
		  const char *comment)
{
	struct Client *target_p;

	if(source_p->servptr && source_p->servptr->serv)
		dlinkDelete(&source_p->lnode, &source_p->servptr->serv->servers);
	else
		s_assert(0);

	dlinkFindDestroy(source_p, &global_serv_list);
	target_p = source_p->from;
	
	if(has_id(source_p))
		del_from_id_hash(source_p->id, source_p);

	del_from_client_hash(source_p->name, source_p);
	remove_client_from_list(source_p);  
	
	SetDead(source_p);
	dlinkAddAlloc(source_p, &dead_list);	
	return 0;
}

static int
exit_local_server(struct Client *client_p, struct Client *source_p, struct Client *from, 
		  const char *comment)
{
	static char comment1[(HOSTLEN*2)+2];
	unsigned int sendk, recvk;
	
	dlinkDelete(&source_p->localClient->tnode, &serv_list);
	dlinkFindDestroy(source_p, &global_serv_list);
	
	unset_chcap_usage_counts(source_p);
	sendk = source_p->localClient->sendK;
	recvk = source_p->localClient->receiveK;

	if(client_p != NULL && source_p != client_p && !IsIOError(source_p))
	{
		sendto_one(source_p, POP_QUEUE, "ERROR :Closing Link: 127.0.0.1 %s (%s)",
			   source_p->name, comment);
	}
	
	if(source_p->localClient->ctrlfd >= 0)
	{
		comm_close(source_p->localClient->ctrlfd);
		source_p->localClient->ctrlfd = -1;
	}

	if(source_p->servptr && source_p->servptr->serv)
		dlinkDelete(&source_p->lnode, &source_p->servptr->serv->servers);
	else
		s_assert(0);


	close_connection(source_p);
	
	if((source_p->serv) && (source_p->serv->up))
		 strcpy(comment1, source_p->serv->up);
	else
		strcpy(comment1, "<Unknown>");
	
	strcat(comment1, " ");
	strcat(comment1, source_p->name);

	if(source_p->serv != NULL)
		remove_dependents(client_p, source_p, from, comment, comment1);

	sendto_realops_flags(UMODE_ALL, L_ALL, "%s was connected"
			     " for %ld seconds.  %d/%d sendK/recvK.",
			     source_p->name, CurrentTime - source_p->localClient->firsttime, sendk, recvk);

	ilog(L_SERVER, "%s was connected for %ld seconds.  %d/%d sendK/recvK.",
	     source_p->name, CurrentTime - source_p->localClient->firsttime, sendk, recvk);
        
	if(has_id(source_p))
		del_from_id_hash(source_p->id, source_p);

	del_from_client_hash(source_p->name, source_p);
	remove_client_from_list(source_p);
	
	SetDead(source_p);
	dlinkAddAlloc(source_p, &dead_list);
	return 0;
}


/* 
 * This assumes IsPerson(source_p) == TRUE && MyConnect(source_p) == TRUE
 */

static int
exit_local_client(struct Client *client_p, struct Client *source_p, struct Client *from,
		  const char *comment)
{
	unsigned long on_for;

	exit_generic_client(client_p, source_p, from, comment);
	clear_monitor(source_p);

	s_assert(IsPerson(source_p));
	linebuf_donebuf(&client_p->localClient->buf_recvq);
	dlinkDelete(&source_p->localClient->tnode, &lclient_list);
	dlinkDelete(&source_p->lnode, &me.serv->users);

	if(IsOper(source_p))
		dlinkFindDestroy(source_p, &oper_list);

	sendto_realops_flags(UMODE_CCONN, L_ALL,
			     "Client exiting: %s (%s@%s) [%s] [%s]",
			     source_p->name,
			     source_p->username, source_p->host, comment,
#ifdef HIDE_SPOOF_IPS
                             IsIPSpoof(source_p) ? "255.255.255.255" :
#endif
			     source_p->sockhost);

	sendto_realops_flags(UMODE_CCONNEXT, L_ALL,
			"CLIEXIT %s %s %s %s 0 %s",
			source_p->name, source_p->username, source_p->host,
#ifdef HIDE_SPOOF_IPS
			IsIPSpoof(source_p) ? "255.255.255.255" :
#endif
			source_p->sockhost, comment);

	on_for = CurrentTime - source_p->localClient->firsttime;

	ilog(L_USER, "%s (%3lu:%02lu:%02lu): %s!%s@%s %d/%d",
		myctime(CurrentTime), on_for / 3600,
		(on_for % 3600) / 60, on_for % 60,
		source_p->name, source_p->username, source_p->host,
		source_p->localClient->sendK, source_p->localClient->receiveK);

	sendto_one(source_p, POP_QUEUE, "ERROR :Closing Link: %s (%s)", source_p->host, comment);
	close_connection(source_p);

	if((source_p->flags & FLAGS_KILLED) == 0)
	{
		sendto_server(client_p, NULL, CAP_TS6, NOCAPS,
			      ":%s QUIT :%s", use_id(source_p), comment);
		sendto_server(client_p, NULL, NOCAPS, CAP_TS6,
			      ":%s QUIT :%s", source_p->name, comment);
	}

	SetDead(source_p);
	dlinkAddAlloc(source_p, &dead_list);
	return(CLIENT_EXITED);
}


/*
** exit_client - This is old "m_bye". Name  changed, because this is not a
**        protocol function, but a general server utility function.
**
**        This function exits a client of *any* type (user, server, etc)
**        from this server. Also, this generates all necessary prototol
**        messages that this exit may cause.
**
**   1) If the client is a local client, then this implicitly
**        exits all other clients depending on this connection (e.g.
**        remote clients having 'from'-field that points to this.
**
**   2) If the client is a remote client, then only this is exited.
**
** For convenience, this function returns a suitable value for
** m_function return value:
**
**        CLIENT_EXITED        if (client_p == source_p)
**        0                if (client_p != source_p)
*/
int
exit_client(struct Client *client_p,	/* The local client originating the
					 * exit or NULL, if this exit is
					 * generated by this server for
					 * internal reasons.
					 * This will not get any of the
					 * generated messages. */
	    struct Client *source_p,	/* Client exiting */
	    struct Client *from,	/* Client firing off this Exit,
					 * never NULL! */
	    const char *comment	/* Reason for the exit */
	)
{
	if(IsClosing(source_p))
		return -1;

	/* note, this HAS to be here, when we exit a client we attempt to
	 * send them data, if this generates a write error we must *not* add
	 * them to the abort list --fl
	 */
	SetClosing(source_p);

	if(MyConnect(source_p))
	{
		/* Local clients of various types */
		if(IsPerson(source_p))
			return exit_local_client(client_p, source_p, from, comment);
		else if(IsServer(source_p))
			return exit_local_server(client_p, source_p, from, comment);
		/* IsUnknown || IsConnecting || IsHandShake */
		else if(!IsReject(source_p))
			return exit_unknown_client(client_p, source_p, from, comment);
	} 
	else 
	{
		/* Remotes */
		if(IsPerson(source_p))
			return exit_remote_client(client_p, source_p, from, comment);
		else if(IsServer(source_p))
			return exit_remote_server(client_p, source_p, from, comment);
	}

	return -1;
}

/*
 * Count up local client memory
 */

/* XXX one common Client list now */
void
count_local_client_memory(size_t * count, size_t * local_client_memory_used)
{
	size_t lusage;
	BlockHeapUsage(lclient_heap, count, NULL, &lusage);
	*local_client_memory_used = lusage + (*count * (sizeof(MemBlock) + sizeof(struct Client)));
}

/*
 * Count up remote client memory
 */
void
count_remote_client_memory(size_t * count, size_t * remote_client_memory_used)
{
	size_t lcount, rcount;
	BlockHeapUsage(lclient_heap, &lcount, NULL, NULL);
	BlockHeapUsage(client_heap, &rcount, NULL, NULL);
	*count = rcount - lcount;
	*remote_client_memory_used = *count * (sizeof(MemBlock) + sizeof(struct Client));
}


/*
 * accept processing, this adds a form of "caller ID" to ircd
 * 
 * If a client puts themselves into "caller ID only" mode,
 * only clients that match a client pointer they have put on 
 * the accept list will be allowed to message them.
 *
 * [ source.on_allow_list ] -> [ target1 ] -> [ target2 ]
 *
 * [target.allow_list] -> [ source1 ] -> [source2 ]
 *
 * i.e. a target will have a link list of source pointers it will allow
 * each source client then has a back pointer pointing back
 * to the client that has it on its accept list.
 * This allows for exit_one_client to remove these now bogus entries
 * from any client having an accept on them. 
 */

/*
 * accept_message
 *
 * inputs	- pointer to source client
 * 		- pointer to target client
 * output	- 1 if accept this message 0 if not
 * side effects - See if source is on target's allow list
 */
int
accept_message(struct Client *source, struct Client *target)
{
	if((target == source) || dlinkFind(source, &target->localClient->allow_list) != NULL)
		return 1;

	return 0;
}


/*
 * del_from_accept
 *
 * inputs	- pointer to source client
 * 		- pointer to target client
 * output	- NONE
 * side effects - Delete's source pointer to targets allow list
 *
 * Walk through the target's accept list, remove if source is found,
 * Then walk through the source's on_accept_list remove target if found.
 */
void
del_from_accept(struct Client *source, struct Client *target)
{
	dlink_node *ptr;
	dlink_node *ptr2;
	dlink_node *next_ptr;
	dlink_node *next_ptr2;
	struct Client *target_p;

	DLINK_FOREACH_SAFE(ptr, next_ptr, target->localClient->allow_list.head)
	{
		target_p = ptr->data;
		if(source == target_p)
		{
			dlinkDestroy(ptr, &target->localClient->allow_list);

			DLINK_FOREACH_SAFE(ptr2, next_ptr2, source->on_allow_list.head)
			{
				target_p = ptr2->data;
				if(target == target_p)
				{
					dlinkDestroy(ptr2, &source->on_allow_list);
				}
			}
		}
	}
}

/*
 * del_all_accepts
 *
 * inputs	- pointer to exiting client
 * output	- NONE
 * side effects - Walk through given clients allow_list and on_allow_list
 *                remove all references to this client
 */
void
del_all_accepts(struct Client *client_p)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	struct Client *target_p;

	if(MyClient(client_p))
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, client_p->localClient->allow_list.head)
		{
			target_p = ptr->data;
			if(target_p != NULL)
				del_from_accept(target_p, client_p);
		}
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, client_p->on_allow_list.head)
	{
		target_p = ptr->data;
		if(target_p != NULL)
			del_from_accept(client_p, target_p);
	}
}

/*
 * show_ip() - asks if the true IP shoudl be shown when source is
 *             askin for info about target 
 *
 * Inputs	- source_p who is asking
 *		- target_p who do we want the info on
 * Output	- returns 1 if clear IP can be showed, otherwise 0
 * Side Effects	- none
 *
 *
 * Truth tables, one if source_p is local to target_p and one if not. 
 * Key:
 *  x  Show IP
 *  -  Don't show IP
 *  ?  Ask IsIPSpoof()
 */

static char show_ip_local[7][7] = {
	/*Target:        A   O   L   H   S   C   E */
	/*Source: */
	/*Admin */ {'x', 'x', 'x', 'x', 'x', 'x', '-'},
	/*Oper */ {'-', '?', 'x', '?', '?', '?', '-'},
	/*Luser */ {'-', '-', '?', '-', '-', '-', '-'},
	/*Handshaker */ {'-', '-', '-', '-', '-', '-', '-'},
	/*Server */ {'-', '-', '-', '-', '-', '-', '-'},
	/*Connecting */ {'-', '-', '-', '-', '-', '-', '-'},
	/*Else */ {'-', '-', '-', '-', '-', '-', '-'}
};

static char show_ip_remote[7][7] = {
	/*Target:        A   O   L   H   S   C   E */
	/*Source: */
	/*Admin */ {'?', '?', '?', '?', '?', '-', '-'},
	/*Oper */ {'-', '?', '?', '?', '?', '-', '-'},
	/*Luser */ {'-', '-', '?', '-', '-', '-', '-'},
	/*Handshake */ {'-', '-', '-', '-', '-', '-', '-'},
	/*Server */ {'-', '-', '-', '-', '-', '-', '-'},
	/*Connecting */ {'-', '-', '-', '-', '-', '-', '-'},
	/*Else */ {'-', '-', '-', '-', '-', '-', '-'}
};

int
show_ip(struct Client *source_p, struct Client *target_p)
{
	int s, t, res;

	if(IsAdmin(source_p))
		s = 0;
	else if(IsOper(source_p))
		s = 1;
	else if(IsClient(source_p))
		s = 2;
	else if(IsHandshake(source_p))
		s = 3;
	else if(IsServer(source_p))
		s = 4;
	else if(IsConnecting(source_p))
		s = 5;
	else
		s = 6;

	if(IsAdmin(target_p))
		t = 0;
	else if(IsOper(target_p))
		t = 1;
	else if(IsClient(target_p))
		t = 2;
	else if(IsHandshake(target_p))
		t = 3;
	else if(IsServer(target_p))
		t = 4;
	else if(IsConnecting(target_p))
		t = 5;
	else
		t = 6;

	if(MyClient(source_p) && MyClient(target_p))
		res = show_ip_local[s][t];
	else
		res = show_ip_remote[s][t];

	if(res == '-')
		return 0;

#ifdef HIDE_SPOOF_IPS
	if(IsIPSpoof(target_p))
		return 0;
#endif

#ifdef HIDE_SERVERS_IPS
	if(IsAnyServer(target_p))
		return 0;
#endif

	if(res == '?')
		return !IsIPSpoof(target_p);

	if(res == 'x')
		return 1;

	/* This should never happen */
	return 0;
}

/*
 * initUser
 *
 * inputs	- none
 * outputs	- none
 *
 * side effects - Creates a block heap for struct Users
 *
 */
static BlockHeap *user_heap;
void
initUser(void)
{
	user_heap = BlockHeapCreate(sizeof(struct User), USER_HEAP_SIZE);
	if(!user_heap)
		outofmemory();
}

/*
 * make_user
 *
 * inputs	- pointer to client struct
 * output	- pointer to struct User
 * side effects - add's an User information block to a client
 *                if it was not previously allocated.
 */
struct User *
make_user(struct Client *client_p)
{
	struct User *user;

	user = client_p->user;
	if(!user)
	{
		user = (struct User *) BlockHeapAlloc(user_heap);
		user->refcnt = 1;
		client_p->user = user;
	}
	return user;
}

/*
 * make_server
 *
 * inputs	- pointer to client struct
 * output	- pointer to struct Server
 * side effects - add's an Server information block to a client
 *                if it was not previously allocated.
 */
struct Server *
make_server(struct Client *client_p)
{
	struct Server *serv = client_p->serv;

	if(!serv)
	{
		serv = (struct Server *) MyMalloc(sizeof(struct Server));
		client_p->serv = serv;
	}
	return client_p->serv;
}

/*
 * free_user
 * 
 * inputs	- pointer to user struct
 *		- pointer to client struct
 * output	- none
 * side effects - Decrease user reference count by one and release block,
 *                if count reaches 0
 */
void
free_user(struct User *user, struct Client *client_p)
{
	if(--user->refcnt <= 0)
	{
		if(user->away)
			MyFree((char *) user->away);
		/*
		 * sanity check
		 */
		if(user->refcnt < 0 || user->invited.head || user->channel.head)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "* %#lx user (%s!%s@%s) %#lx %#lx %#lx %lu %d *",
					     (unsigned long) client_p,
					     client_p ? client_p->
					     name : "<noname>",
					     client_p->username,
					     client_p->host,
					     (unsigned long) user,
					     (unsigned long) user->invited.head,
					     (unsigned long) user->channel.head, 
					     dlink_list_length(&user->channel),
					     user->refcnt);
			s_assert(!user->refcnt);
			s_assert(!user->invited.head);
			s_assert(!user->channel.head);
		}

		BlockHeapFree(user_heap, user);
	}
}

void
init_uid(void)
{
	int i;

	for(i = 0; i < 3; i++)
		current_uid[i] = me.id[i];

	for(i = 3; i < 9; i++)
		current_uid[i] = 'A';

	current_uid[9] = '\0';
}


char *
generate_uid(void)
{
	int i;

	for(i = 8; i > 3; i--)
	{
		if(current_uid[i] == 'Z')
		{
			current_uid[i] = '0';
			return current_uid;
		}
		else if(current_uid[i] != '9')
		{
			current_uid[i]++;
			return current_uid;
		}
		else
			current_uid[i] = 'A';
	}

	/* if this next if() triggers, we're fucked. */
	if(current_uid[3] == 'Z')
	{
		current_uid[i] = 'A';
		s_assert(0);
	}
	else
		current_uid[i]++;

	return current_uid;
}

/*
 * close_connection
 *        Close the physical connection. This function must make
 *        MyConnect(client_p) == FALSE, and set client_p->from == NULL.
 */
void
close_connection(struct Client *client_p)
{
	s_assert(client_p != NULL);
	if(client_p == NULL)
		return;

	s_assert(MyConnect(client_p));
	if(!MyConnect(client_p))
		return;
	
	if(IsServer(client_p))
	{
		struct server_conf *server_p;

		ServerStats.is_sv++;
		ServerStats.is_sbs += client_p->localClient->sendB;
		ServerStats.is_sbr += client_p->localClient->receiveB;
		ServerStats.is_sks += client_p->localClient->sendK;
		ServerStats.is_skr += client_p->localClient->receiveK;
		ServerStats.is_sti += CurrentTime - client_p->localClient->firsttime;
		if(ServerStats.is_sbs > 2047)
		{
			ServerStats.is_sks += (ServerStats.is_sbs >> 10);
			ServerStats.is_sbs &= 0x3ff;
		}
		if(ServerStats.is_sbr > 2047)
		{
			ServerStats.is_skr += (ServerStats.is_sbr >> 10);
			ServerStats.is_sbr &= 0x3ff;
		}

		/*
		 * If the connection has been up for a long amount of time, schedule
		 * a 'quick' reconnect, else reset the next-connect cycle.
		 */
		if((server_p = find_server_conf(client_p->name)) != NULL)
		{
			/*
			 * Reschedule a faster reconnect, if this was a automatically
			 * connected configuration entry. (Note that if we have had
			 * a rehash in between, the status has been changed to
			 * CONF_ILLEGAL). But only do this if it was a "good" link.
			 */
			server_p->hold = time(NULL);
			server_p->hold +=
				(server_p->hold - client_p->localClient->lasttime >
				 HANGONGOODLINK) ? HANGONRETRYDELAY : ConFreq(server_p->class);
			if(nextconnect > server_p->hold)
				nextconnect = server_p->hold;
		}

	}
	else if(IsClient(client_p))
	{
		ServerStats.is_cl++;
		ServerStats.is_cbs += client_p->localClient->sendB;
		ServerStats.is_cbr += client_p->localClient->receiveB;
		ServerStats.is_cks += client_p->localClient->sendK;
		ServerStats.is_ckr += client_p->localClient->receiveK;
		ServerStats.is_cti += CurrentTime - client_p->localClient->firsttime;
		if(ServerStats.is_cbs > 2047)
		{
			ServerStats.is_cks += (ServerStats.is_cbs >> 10);
			ServerStats.is_cbs &= 0x3ff;
		}
		if(ServerStats.is_cbr > 2047)
		{
			ServerStats.is_ckr += (ServerStats.is_cbr >> 10);
			ServerStats.is_cbr &= 0x3ff;
		}
	}
	else
		ServerStats.is_ni++;

	if(-1 < client_p->localClient->fd)
	{
		/* attempt to flush any pending dbufs. Evil, but .. -- adrian */
		if(!IsIOError(client_p))
			send_queued_write(client_p->localClient->fd, client_p);

		comm_close(client_p->localClient->fd);
		client_p->localClient->fd = -1;
	}

	if(HasServlink(client_p))
	{
		if(client_p->localClient->fd > -1)
		{
			comm_close(client_p->localClient->ctrlfd);
			client_p->localClient->ctrlfd = -1;
		}
	}

	linebuf_donebuf(&client_p->localClient->buf_sendq);
	linebuf_donebuf(&client_p->localClient->buf_recvq);
	detach_conf(client_p);

	/* XXX shouldnt really be done here. */
	detach_server_conf(client_p);

	client_p->from = NULL;	/* ...this should catch them! >:) --msa */
	ClearMyConnect(client_p);
	SetIOError(client_p);
}



void
error_exit_client(struct Client *client_p, int error)
{
	/*
	 * ...hmm, with non-blocking sockets we might get
	 * here from quite valid reasons, although.. why
	 * would select report "data available" when there
	 * wasn't... so, this must be an error anyway...  --msa
	 * actually, EOF occurs when read() returns 0 and
	 * in due course, select() returns that fd as ready
	 * for reading even though it ends up being an EOF. -avalon
	 */
	char errmsg[255];
	int current_error = comm_get_sockerr(client_p->localClient->fd);

	SetIOError(client_p);

	if(IsServer(client_p) || IsHandshake(client_p))
	{
		int connected = CurrentTime - client_p->localClient->firsttime;

		if(error == 0)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Server %s closed the connection",
					     get_server_name(client_p, SHOW_IP));

			ilog(L_SERVER, "Server %s closed the connection",
			     log_client_name(client_p, SHOW_IP));
		}
		else
		{
			report_error("Lost connection to %s: %d",
					get_server_name(client_p, SHOW_IP),
					log_client_name(client_p, SHOW_IP),
					current_error);
		}

		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s had been connected for %d day%s, %2d:%02d:%02d",
				     client_p->name, connected / 86400,
				     (connected / 86400 == 1) ? "" : "s",
				     (connected % 86400) / 3600,
				     (connected % 3600) / 60, connected % 60);
	}

	if(error == 0)
		strlcpy(errmsg, "Remote host closed the connection", sizeof(errmsg));
	else
		ircsnprintf(errmsg, sizeof(errmsg), "Read error: %s", strerror(current_error));

	exit_client(client_p, client_p, &me, errmsg);
}

/* flood_endgrace()
 *
 * marks the end of the clients grace period
 */
void
flood_endgrace(struct Client *client_p)
{
	SetFloodDone(client_p);

	/* Drop their flood limit back down */
	client_p->localClient->allow_read = MAX_FLOOD;

	/* sent_parsed could be way over MAX_FLOOD but under MAX_FLOOD_BURST,
	 * so reset it.
	 */
	client_p->localClient->sent_parsed = 0;
}

