/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_server.c: Introduces a server.
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
#include "tools.h"
#include "handlers.h"		/* m_server prototype */
#include "client.h"		/* client struct */
#include "common.h"		/* TRUE bleah */
#include "event.h"
#include "hash.h"		/* add_to_client_hash */
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"		/* me */
#include "numeric.h"		/* ERR_xxx */
#include "s_conf.h"		/* struct ConfItem */
#include "s_log.h"		/* log level defines */
#include "s_serv.h"
#include "s_stats.h"		/* ServerStats */
#include "s_user.h"
#include "s_bsd.h"
#include "scache.h"		/* find_or_add */
#include "send.h"		/* sendto_one */
#include "msg.h"
#include "parse.h"
#include "modules.h"

static int mr_server(struct Client *, struct Client *, int, const char **);
static int ms_server(struct Client *, struct Client *, int, const char **);
static int ms_sid(struct Client *, struct Client *, int, const char **);

struct Message server_msgtab = {
	"SERVER", 0, 0, 4, 0, MFLG_SLOW | MFLG_UNREG, 0,
	{mr_server, m_registered, ms_server, m_registered}
};
struct Message sid_msgtab = {
	"SID", 0, 0, 5, 0, MFLG_SLOW, 0,
	{m_ignore, m_registered, ms_sid, m_registered}
};

mapi_clist_av1 server_clist[] = { &server_msgtab, &sid_msgtab, NULL };
DECLARE_MODULE_AV1(server, NULL, NULL, server_clist, NULL, NULL, "$Revision$");

extern char *crypt();

static int bogus_host(const char *host);
static struct Client *server_exists(const char *);
static int set_server_gecos(struct Client *, const char *);
static int server_estab(struct Client *client_p);
static int check_server(const char *name, struct Client *client_p);

static char buf[BUFSIZE];

/*
 * mr_server - SERVER message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = serverinfo/hopcount
 *      parv[3] = serverinfo
 */
static int
mr_server(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char info[REALLEN + 1];
	const char *name;
	struct Client *target_p;
	int hop;

	if(parc < 4)
	{
		sendto_one(client_p, "ERROR :No servername");
		exit_client(client_p, client_p, client_p, "Wrong number of args");
		return 0;
	}

	name = parv[1];
	hop = atoi(parv[2]);
	strlcpy(info, parv[3], sizeof(info));

	/* 
	 * Reject a direct nonTS server connection if we're TS_ONLY -orabidoo
	 */
	if(!DoesTS(client_p))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN, "Link %s dropped, non-TS server",
				     get_client_name(client_p, HIDE_IP));
		sendto_realops_flags(UMODE_ALL, L_OPER, "Link %s dropped, non-TS server",
				     get_client_name(client_p, MASK_IP));
		exit_client(client_p, client_p, client_p, "Non-TS server");
		return 0;
	}

	if(bogus_host(name))
	{
		exit_client(client_p, client_p, client_p, "Bogus server name");
		return 0;
	}

	/* Now we just have to call check_server and everything should be
	 * check for us... -A1kmm. */
	switch (check_server(name, client_p))
	{
	case -1:
		if(ConfigFileEntry.warn_no_nline)
		{
			sendto_realops_flags(UMODE_ALL, L_ADMIN,
					     "Unauthorised server connection attempt from %s: No entry for "
					     "servername %s", get_client_name(client_p, HIDE_IP),
					     name);

			sendto_realops_flags(UMODE_ALL, L_OPER,
					     "Unauthorised server connection attempt from %s: No entry for "
					     "servername %s", get_client_name(client_p, MASK_IP),
					     name);

			ilog(L_NOTICE, "Access denied, No N line for server %s",
			     log_client_name(client_p, SHOW_IP));
		}

		exit_client(client_p, client_p, client_p, "Invalid servername.");
		return 0;
		/* NOT REACHED */
		break;

	case -2:
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Unauthorised server connection attempt from %s: Bad password "
				     "for server %s", get_client_name(client_p, HIDE_IP), name);

		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Unauthorised server connection attempt from %s: Bad password "
				     "for server %s", get_client_name(client_p, MASK_IP), name);

		ilog(L_NOTICE, "Access denied, invalid password for server %s",
		     log_client_name(client_p, SHOW_IP));

		exit_client(client_p, client_p, client_p, "Invalid password.");
		return 0;
		/* NOT REACHED */
		break;

	case -3:
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Unauthorised server connection attempt from %s: Invalid host "
				     "for server %s", get_client_name(client_p, HIDE_IP), name);

		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Unauthorised server connection attempt from %s: Invalid host "
				     "for server %s", get_client_name(client_p, MASK_IP), name);

		ilog(L_NOTICE, "Access denied, invalid host for server %s",
		     log_client_name(client_p, SHOW_IP));

		exit_client(client_p, client_p, client_p, "Invalid host.");
		return 0;
		/* NOT REACHED */
		break;

		/* servername is > HOSTLEN */
	case -4:
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Invalid servername %s from %s",
				     name, get_client_name(client_p, HIDE_IP));
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Invalid servername %s from %s",
				     name, get_client_name(client_p, MASK_IP));
		ilog(L_NOTICE, "Access denied, invalid servername from %s",
		     log_client_name(client_p, SHOW_IP));

		exit_client(client_p, client_p, client_p, "Invalid servername.");
		return 0;
		/* NOT REACHED */
		break;
	}

	if((target_p = server_exists(name)))
	{
		/*
		 * This link is trying feed me a server that I already have
		 * access through another path -- multiple paths not accepted
		 * currently, kill this link immediately!!
		 *
		 * Rather than KILL the link which introduced it, KILL the
		 * youngest of the two links. -avalon
		 *
		 * Definitely don't do that here. This is from an unregistered
		 * connect - A1kmm.
		 */
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Attempt to re-introduce server %s from %s", name,
				     get_client_name(client_p, HIDE_IP));

		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Attempt to re-introduce server %s from %s", name,
				     get_client_name(client_p, MASK_IP));

		sendto_one(client_p, "ERROR :Server already exists.");
		exit_client(client_p, client_p, client_p, "Server Exists");
		return 0;
	}

	if(has_id(client_p) && (target_p = find_id(client_p->id)) != NULL)
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Attempt to re-introduce SID %s from %s%s",
				     client_p->id, name,
				     get_client_name(client_p, HIDE_IP));
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Attempt to re-introduce SID %s from %s%s",
				     client_p->id, name,
				     get_client_name(client_p, MASK_IP));

		sendto_one(client_p, "ERROR :SID already exists.");
		exit_client(client_p, client_p, client_p, "SID Exists");
		return 0;
	}

	/*
	 * if we are connecting (Handshake), we already have the name from the
	 * C:line in client_p->name
	 */

	strlcpy(client_p->name, name, sizeof(client_p->name));
	set_server_gecos(client_p, info);
	client_p->hopcount = hop;
	server_estab(client_p);

	return 0;
}

/*
 * ms_server - SERVER message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = serverinfo/hopcount
 *      parv[3] = serverinfo
 */
static int
ms_server(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char info[REALLEN + 1];
	/* same size as in s_misc.c */
	const char *name;
	struct Client *target_p;
	struct Client *bclient_p;
	struct ConfItem *aconf;
	int hop;
	int hlined = 0;
	int llined = 0;
	dlink_node *ptr;

	/* Just to be sure -A1kmm. */
	if(!IsServer(source_p))
		return 0;

	if(parc < 4)
	{
		sendto_one(client_p, "ERROR :No servername");
		return 0;
	}

	name = parv[1];
	hop = atoi(parv[2]);
	strlcpy(info, parv[3], sizeof(info));

	if((target_p = server_exists(name)))
	{
		/*
		 * This link is trying feed me a server that I already have
		 * access through another path -- multiple paths not accepted
		 * currently, kill this link immediately!!
		 *
		 * Rather than KILL the link which introduced it, KILL the
		 * youngest of the two links. -avalon
		 *
		 * I think that we should exit the link itself, not the introducer,
		 * and we should always exit the most recently received(i.e. the
		 * one we are receiving this SERVER for. -A1kmm
		 *
		 * You *cant* do this, if you link somewhere, it bursts you a server
		 * that already exists, then sends you a client burst, you squit the
		 * server, but you keep getting the burst of clients on a server that
		 * doesnt exist, although ircd can handle it, its not a realistic
		 * solution.. --fl_ 
		 */
		/* It is behind a host-masked server. Completely ignore the
		 * server message(don't propagate or we will delink from whoever
		 * we propagate to). -A1kmm */
		if(irccmp(target_p->name, name) && target_p->from == client_p)
			return 0;

		sendto_one(client_p, "ERROR :Server %s already exists", name);

		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s cancelled, server %s already exists",
				     get_client_name(client_p, SHOW_IP), name);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s cancelled, server %s already exists",
				     client_p->name, name);

		exit_client(client_p, client_p, &me, "Server Exists");
		return 0;
	}

	/* 
	 * User nicks never have '.' in them and server names
	 * must always have '.' in them.
	 */
	if(strchr(name, '.') == NULL)
	{
		/*
		 * Server trying to use the same name as a person. Would
		 * cause a fair bit of confusion. Enough to make it hellish
		 * for a while and servers to send stuff to the wrong place.
		 */
		sendto_one(client_p, "ERROR :Nickname %s already exists!", name);
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s cancelled: Server/nick collision on %s",
				     /* inpath */ get_client_name(client_p, HIDE_IP), name);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s cancelled: Server/nick collision on %s",
				     get_client_name(client_p, MASK_IP), name);
		exit_client(client_p, client_p, client_p, "Nick as Server");
		return 0;
	}

	/*
	 * Server is informing about a new server behind
	 * this link. Create REMOTE server structure,
	 * add it to list and propagate word to my other
	 * server links...
	 */
	if(parc == 1 || EmptyString(info))
	{
		sendto_one(client_p, "ERROR :No server info specified for %s", name);
		return 0;
	}

	/*
	 * See if the newly found server is behind a guaranteed
	 * leaf. If so, close the link.
	 *
	 */

	for (aconf = ConfigItemList; aconf; aconf = aconf->next)
	{
		if((aconf->status & (CONF_LEAF | CONF_HUB)) == 0)
			continue;

		if(match(aconf->name, client_p->name))
		{
			if(aconf->status == CONF_HUB)
			{
				if(match(aconf->host, name))
					hlined++;
			}
			else if(aconf->status == CONF_LEAF)
			{
				if(match(aconf->host, name))
					llined++;
			}
		}
	}

	/* Ok, this way this works is
	 *
	 * A server can have a CONF_HUB allowing it to introduce servers
	 * behind it.
	 *
	 * connect {
	 *            name = "irc.bighub.net";
	 *            hub_mask="*";
	 *            ...
	 * 
	 * That would allow "irc.bighub.net" to introduce anything it wanted..
	 *
	 * However
	 *
	 * connect {
	 *            name = "irc.somehub.fi";
	 *            hub_mask="*";
	 *            leaf_mask="*.edu";
	 *...
	 * Would allow this server in finland to hub anything but
	 * .edu's
	 */

	/* Ok, check client_p can hub the new server */
	if(!hlined)
	{
		/* OOOPs nope can't HUB */
		sendto_realops_flags(UMODE_ALL, L_ADMIN, "Non-Hub link %s introduced %s.",
				     get_client_name(client_p, HIDE_IP), name);

		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Non-Hub link %s introduced %s.",
				     get_client_name(client_p, MASK_IP), name);

		exit_client(NULL, client_p, &me, "No matching hub_mask.");
		return 0;
	}

	/* Check for the new server being leafed behind this HUB */
	if(llined)
	{
		/* OOOPs nope can't HUB this leaf */
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s introduced leafed server %s.",
				     get_client_name(client_p, HIDE_IP), name);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s introduced leafed server %s.", client_p->name, name);

		exit_client(NULL, client_p, &me, "Leafed Server.");
		return 0;
	}



	if(strlen(name) > HOSTLEN)
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s introduced server with invalid servername %s",
				     get_client_name(client_p, HIDE_IP), name);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s introduced server with invalid servername %s",
				     client_p->name, name);

		exit_client(NULL, client_p, &me, "Invalid servername introduced.");
		return 0;
	}

	target_p = make_client(client_p);
	make_server(target_p);
	target_p->hopcount = hop;

	strlcpy(target_p->name, name, sizeof(target_p->name));

	set_server_gecos(target_p, info);

	target_p->serv->up = find_or_add(source_p->name);

	if(has_id(source_p))
		target_p->serv->upid = source_p->id;

	target_p->servptr = source_p;

	SetServer(target_p);

	dlinkAddTail(target_p, &target_p->node, &global_client_list);
	dlinkAddTailAlloc(target_p, &global_serv_list);
	add_to_client_hash(target_p->name, target_p);
	dlinkAdd(target_p, &target_p->lnode, &target_p->servptr->serv->servers);


	/*
	 * Old sendto_serv_but_one() call removed because we now
	 * need to send different names to different servers
	 * (domain name matching)
	 */
	DLINK_FOREACH(ptr, serv_list.head)
	{
		bclient_p = ptr->data;

		if(bclient_p == client_p)
			continue;
		if(!(aconf = bclient_p->serv->sconf))
		{
			sendto_realops_flags(UMODE_ALL, L_ADMIN,
					     "Lost N-line for %s on %s. Closing",
					     get_client_name(client_p, HIDE_IP), name);
			sendto_realops_flags(UMODE_ALL, L_OPER,
					     "Lost N-line for %s on %s. Closing",
					     get_client_name(client_p, MASK_IP), name);
			exit_client(client_p, client_p, client_p, "Lost N line");
			return 0;
		}

		sendto_one(bclient_p, ":%s SERVER %s %d :%s%s",
			   parv[0], target_p->name, hop + 1,
			   IsHidden(target_p) ? "(H) " : "", target_p->info);
	}

	sendto_realops_flags(UMODE_EXTERNAL, L_ALL,
			     "Server %s being introduced by %s", target_p->name, source_p->name);

	return 0;
}

static int
ms_sid(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	struct ConfItem *aconf;
	int hop;
	int hlined = 0;
	int llined = 0;

	hop = atoi(parv[2]);

	/* collision on the name? */
	if((target_p = server_exists(parv[1])) != NULL)
	{
		sendto_one(client_p, "ERROR :Server %s already exists", parv[1]);
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s cancelled, server %s already exists",
				     get_client_name(client_p, SHOW_IP), parv[1]);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s cancelled, server %s already exists",
				     client_p->name, parv[1]);
		exit_client(NULL, client_p, &me, "Server Exists");
		return 0;
	}

	/* collision on the SID? */
	if((target_p = find_id(parv[3])) != NULL)
	{
		sendto_one(client_p, "ERROR :SID %s already exists", parv[3]);
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s cancelled, SID %s already exists",
				     get_client_name(client_p, SHOW_IP), parv[3]);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s cancelled, SID %s already exists",
				     client_p->name, parv[3]);
		exit_client(NULL, client_p, &me, "Server Exists");
		return 0;
	}

	if(bogus_host(parv[1]) || strlen(parv[1]) > HOSTLEN)
	{
		sendto_one(client_p, "ERROR :Invalid servername");
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s cancelled, servername %s invalid",
				     get_client_name(client_p, SHOW_IP), parv[1]);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s cancelled, servername %s invalid",
				     client_p->name, parv[1]);
		exit_client(NULL, client_p, &me, "Bogus server name");
		return 0;
	}

	if(!IsDigit(parv[3][0]) || !IsIdChar(parv[3][1]) || 
	   !IsIdChar(parv[3][2]) || parv[3][3] != '\0')
	{
		sendto_one(client_p, "ERROR :Invalid SID");
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s cancelled, SID %s invalid",
				     get_client_name(client_p, SHOW_IP), parv[3]);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s cancelled, SID %s invalid",
				     client_p->name, parv[3]);
		exit_client(NULL, client_p, &me, "Bogus SID");
		return 0;
	}

	if(EmptyString(parv[4]))
	{
		sendto_one(client_p, "ERROR :No server info specified for %s", 
			   parv[1]);
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s cancelled, serverinfo invalid",
				     get_client_name(client_p, SHOW_IP));
		sendto_realops_flags(UMODE_ALL, L_OPER,
				      "Link %s cancelled, serverinfo invalid",
				      client_p->name);
		exit_client(NULL, client_p, &me, "Bogus server name");
		return 0;
	}

	/* for the directly connected server:
	 * H: allows it to introduce a server matching that mask
	 * L: disallows it introducing a server matching that mask
	 */
	for(aconf = ConfigItemList; aconf; aconf = aconf->next)
	{
		if((aconf->status & (CONF_LEAF | CONF_HUB)) == 0)
			continue;

		if(match(aconf->name, client_p->name))
		{
			if(match(aconf->host, parv[1]))
			{
				if(aconf->status == CONF_HUB)
					hlined++;
				else
					llined++;
			}
		}
	}

	/* no matching hub_mask */
	if(!hlined)
	{
		sendto_one(client_p, "ERROR :No matching hub_mask");
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Non-Hub link %s introduced %s.",
				     get_client_name(client_p, SHOW_IP), parv[1]);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Non-Hub link %s introduced %s.",
				     client_p->name, parv[1]);
		exit_client(NULL, client_p, &me, "No matching hub_mask.");
		return 0;
	}

	/* matching leaf_mask */
	if(llined)
	{
		sendto_one(client_p, "ERROR :Matching leaf_mask");
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s introduced leafed server %s.",
				     get_client_name(client_p, SHOW_IP), parv[1]);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s introduced leafed server %s.",
				     client_p->name, parv[1]);
		exit_client(NULL, client_p, &me, "Leafed Server.");
		return 0;
	}

	/* ok, alls good */
	target_p = make_client(client_p);
	make_server(target_p);

	strlcpy(target_p->name, parv[1], sizeof(target_p->name));
	target_p->hopcount = atoi(parv[2]) + 1;
	strcpy(target_p->id, parv[3]);
	set_server_gecos(target_p, parv[4]);

	target_p->serv->up = find_or_add(source_p->name);

	if(has_id(source_p))
		target_p->serv->upid = source_p->id;

	target_p->servptr = source_p;
	SetServer(target_p);

	dlinkAddTail(target_p, &target_p->node, &global_client_list);
	dlinkAddTailAlloc(target_p, &global_serv_list);
	add_to_client_hash(target_p->name, target_p);
	add_to_id_hash(target_p->id, target_p);
	dlinkAdd(target_p, &target_p->lnode, &target_p->servptr->serv->servers);

	sendto_server(client_p, NULL, CAP_TS6, NOCAPS,
		      ":%s SID %s %d %s :%s%s",
		      source_p->id, target_p->name, target_p->hopcount,
		      target_p->id,
		      IsHidden(target_p) ? "(H) " : "", target_p->info);
	sendto_server(client_p, NULL, NOCAPS, CAP_TS6,
		      ":%s SERVER %s %d :%s%s",
		      source_p->name, target_p->name, target_p->hopcount,
		      IsHidden(target_p) ? "(H) " : "", target_p->info);

	sendto_realops_flags(UMODE_EXTERNAL, L_ALL,
			     "Server %s being introduced by %s",
			     target_p->name, source_p->name);
	return 0;
}
	
/* set_server_gecos()
 *
 * input	- pointer to client
 * output	- none
 * side effects - servers gecos field is set
 */
static int
set_server_gecos(struct Client *client_p, const char *info)
{
	/* check the info for [IP] */
	if(info[0])
	{
		char *p;
		char *s;
		char *t;

		s = LOCAL_COPY(info);

		/* we should only check the first word for an ip */
		if((p = strchr(s, ' ')))
			*p = '\0';

		/* check for a ] which would symbolise an [IP] */
		if((t = strchr(s, ']')))
		{
			/* set s to after the first space */
			if(p)
				s = ++p;
			else
				s = NULL;
		}
		/* no ], put the space back */
		else if(p)
			*p = ' ';

		/* p may have been set to a trailing space, so check s exists and that
		 * it isnt \0 */
		if(s && (*s != '\0'))
		{
			/* a space? if not (H) could be the last part of info.. */
			if((p = strchr(s, ' ')))
				*p = '\0';

			/* check for (H) which is a hidden server */
			if(!strcmp(s, "(H)"))
			{
				SetHidden(client_p);

				/* if there was no space.. theres nothing to set info to */
				if(p)
					s = ++p;
				else
					s = NULL;
			}
			else if(p)
				*p = ' ';

			/* if there was a trailing space, s could point to \0, so check */
			if(s && (*s != '\0'))
			{
				strlcpy(client_p->info, s, sizeof(client_p->info));
				return 1;
			}
		}
	}

	strlcpy(client_p->info, "(Unknown Location)", sizeof(client_p->info));

	return 1;
}

/*
 * bogus_host
 *
 * inputs	- hostname
 * output	- 1 if a bogus hostname input, 0 if its valid
 * side effects	- none
 */
static int
bogus_host(const char *host)
{
	int bogus_server = 0;
	const char *s;
	int dots = 0;

	for (s = host; *s; s++)
	{
		if(!IsServChar(*s))
		{
			bogus_server = 1;
			break;
		}
		if('.' == *s)
			++dots;
	}

	if(!dots || bogus_server)
		return 1;

	return 0;
}

/*
 * server_exists()
 * 
 * inputs	- servername
 * output	- 1 if server exists, 0 if doesnt exist
 */
static struct Client *
server_exists(const char *servername)
{
	struct Client *target_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, global_serv_list.head)
	{
		target_p = ptr->data;

		if(match(target_p->name, servername) || match(servername, target_p->name))
			return target_p;
	}

	return NULL;
}

static int
check_server(const char *name, struct Client *client_p)
{
	struct ConfItem *aconf = NULL;
	struct ConfItem *server_aconf = NULL;
	int error = -1;

	s_assert(NULL != client_p);

	if(client_p == NULL)
		return error;

	if(!(client_p->localClient->passwd))
		return -2;

	if(strlen(name) > HOSTLEN)
		return -4;

	/* loop through looking for all possible connect items that might work */
	for (aconf = ConfigItemList; aconf; aconf = aconf->next)
	{
		if((aconf->status & CONF_SERVER) == 0)
			continue;

		if(!match(name, aconf->name))
			continue;

		error = -3;

		/* XXX: Fix me for IPv6 */
		/* XXX sockhost is the IPv4 ip as a string */

		if(match(aconf->host, client_p->host) ||
		   match(aconf->host, client_p->sockhost))
		{
			error = -2;

			if(IsConfEncrypted(aconf))
			{
				if(strcmp(aconf->passwd,
					  crypt(client_p->
						localClient->passwd, aconf->passwd)) == 0)
					server_aconf = aconf;
			}
			else
			{
				if(strcmp
				   (aconf->passwd, client_p->localClient->passwd) == 0)
					server_aconf = aconf;
			}
		}
	}

	if(server_aconf == NULL)
		return error;

	/* XXX -- is this detach_conf() needed? --fl */
	detach_conf(client_p);
	attach_conf(client_p, server_aconf);

#ifdef HAVE_LIBZ		/* otherwise, cleait unconditionally */
	if(!(server_aconf->flags & CONF_FLAGS_COMPRESSED))
#endif
		ClearCap(client_p, CAP_ZIP);

	if(aconf != NULL)
	{
#ifdef IPV6
		if(client_p->localClient->ip.ss_family == AF_INET6)
		{
			if(IN6_IS_ADDR_UNSPECIFIED(&((struct sockaddr_in6 *)&aconf->ipnum)->sin6_addr))
			{
				memcpy(&((struct sockaddr_in6 *)&aconf->ipnum)->sin6_addr, 
					&((struct sockaddr_in6 *)&client_p->localClient->ip)->sin6_addr, 
					sizeof(struct in6_addr)); 
			} 
		} else
#endif
		{
			if(((struct sockaddr_in *)&aconf->ipnum)->sin_addr.s_addr == INADDR_NONE)
			{
				((struct sockaddr_in *)&aconf->ipnum)->sin_addr.s_addr = 
					((struct sockaddr_in *)&client_p->localClient->ip)->sin_addr.s_addr;
			}

		}
	}
	return 0;
}


/* burst_modes_TS5()
 *
 * input	- client to burst to, channel name, list to burst, mode flag
 * output	-
 * side effects - client is sent a list of +b, or +e, or +I modes
 */
static void
burst_modes_TS5(struct Client *client_p, char *chname, dlink_list *list, char flag)
{
	dlink_node *ptr;
	struct Ban *banptr;
	char mbuf[MODEBUFLEN];
	char pbuf[BUFSIZE];
	int tlen;
	int mlen;
	int cur_len;
	char *mp;
	char *pp;
	int count = 0;

	mlen = ircsprintf(buf, ":%s MODE %s +", me.name, chname);
	cur_len = mlen;

	mp = mbuf;
	pp = pbuf;

	DLINK_FOREACH(ptr, list->head)
	{
		banptr = ptr->data;
		tlen = strlen(banptr->banstr) + 3;

		/* uh oh */
		if(tlen > MODEBUFLEN)
			continue;

		if((count >= MAXMODEPARAMS) || ((cur_len + tlen + 2) > (BUFSIZE - 3)))
		{
			sendto_one(client_p, "%s%s %s", buf, mbuf, pbuf);

			mp = mbuf;
			pp = pbuf;
			cur_len = mlen;
			count = 0;
		}

		*mp++ = flag;
		*mp = '\0';
		pp += ircsprintf(pp, "%s ", banptr->banstr);
		cur_len += tlen;
		count++;
	}

	if(count != 0)
		sendto_one(client_p, "%s%s %s", buf, mbuf, pbuf);
}

/* burst_modes_TS6()
 *
 * input	- client to burst to, channel name, list to burst, mode flag
 * output	-
 * side effects - client is sent a list of +b, +e, or +I modes
 */
static void
burst_modes_TS6(struct Client *client_p, struct Channel *chptr, 
		dlink_list *list, char flag)
{
	dlink_node *ptr;
	struct Ban *banptr;
	char *t;
	int tlen;
	int mlen;
	int cur_len;

	cur_len = mlen = ircsprintf(buf, ":%s BMASK %lu %s %c :",
				    me.id, chptr->channelts,
				    chptr->chname, flag);
	t = buf + mlen;

	DLINK_FOREACH(ptr, list->head)
	{
		banptr = ptr->data;

		tlen = strlen(banptr->banstr) + 1;

		/* uh oh */
		if(cur_len + tlen > BUFSIZE - 3)
		{
			/* the one we're trying to send doesnt fit at all! */
			if(cur_len == mlen)
			{
				s_assert(0);
				continue;
			}

			/* chop off trailing space and send.. */
			*(t-1) = '\0';
			sendto_one(client_p, "%s", buf);
			cur_len = mlen;
			t = buf + mlen;
		}

		ircsprintf(t, "%s ", banptr->banstr);
		t += tlen;
		cur_len += tlen;
	}

	/* cant ever exit the loop above without having modified buf,
	 * chop off trailing space and send.
	 */
	*(t-1) = '\0';
	sendto_one(client_p, "%s", buf);
}

/*
 * burst_TS5
 * 
 * inputs	- client (server) to send nick towards
 * 		- client to send nick for
 * output	- NONE
 * side effects	- NICK message is sent towards given client_p
 */
static void
burst_TS5(struct Client *client_p)
{
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	static char ubuf[12];
	struct Client *target_p;
	struct Channel *chptr;
	struct membership *msptr;
	struct hook_burst_channel hinfo;
	dlink_node *ptr;
	dlink_node *uptr;
	char *t;
	int tlen, mlen;
	int cur_len = 0;

	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = ptr->data;

		if(!IsPerson(target_p))
			continue;

		send_umode(NULL, target_p, 0, SEND_UMODES, ubuf);
		if(!*ubuf)
		{
			ubuf[0] = '+';
			ubuf[1] = '\0';
		}

		sendto_one(client_p, "NICK %s %d %lu %s %s %s %s :%s",
			   target_p->name, target_p->hopcount + 1,
			   (unsigned long) target_p->tsinfo, ubuf,
			   target_p->username, target_p->host,
			   target_p->user->server, target_p->info);

		if(ConfigFileEntry.burst_away && !EmptyString(target_p->user->away))
			sendto_one(client_p, ":%s AWAY :%s",
				   target_p->name, target_p->user->away);
	}

	DLINK_FOREACH(ptr, global_channel_list.head)
	{
		chptr = ptr->data;

		s_assert(dlink_list_length(&chptr->members) > 0);
		if(dlink_list_length(&chptr->members) <= 0)
			continue;

		if(*chptr->chname != '#')
			return;

		hinfo.chptr = chptr;
		hinfo.client = client_p;
		hook_call_event(h_burst_channel_id, &hinfo);

		*modebuf = *parabuf = '\0';
		channel_modes(chptr, client_p, modebuf, parabuf);

		cur_len = mlen = ircsprintf(buf, ":%s SJOIN %lu %s %s %s:", me.name,
				(unsigned long) chptr->channelts,
				chptr->chname, modebuf, parabuf);

		t = buf + mlen;

		DLINK_FOREACH(uptr, chptr->members.head)
		{
			msptr = uptr->data;

			tlen = strlen(msptr->client_p->name) + 1;
			if(is_chanop(msptr))
				tlen++;
			if(is_voiced(msptr))
				tlen++;

			if(cur_len + tlen >= BUFSIZE - 3)
			{
				t--;
				*t = '\0';
				sendto_one(client_p, "%s", buf);
				cur_len = mlen;
				t = buf + mlen;
			}

			ircsprintf(t, "%s%s ", find_channel_status(msptr, 1), 
				   msptr->client_p->name);

			cur_len += tlen;
			t += tlen;
		}

		/* remove trailing space */
		t--;
		*t = '\0';
		sendto_one(client_p, "%s", buf);

		burst_modes_TS5(client_p, chptr->chname, &chptr->banlist, 'b');

		if(IsCapable(client_p, CAP_EX))
			burst_modes_TS5(client_p, chptr->chname, &chptr->exceptlist, 'e');

		if(IsCapable(client_p, CAP_IE))
			burst_modes_TS5(client_p, chptr->chname, &chptr->invexlist, 'I');
	}
}

/*
 * burst_TS6
 * 
 * inputs	- client (server) to send nick towards
 * 		- client to send nick for
 * output	- NONE
 * side effects	- NICK message is sent towards given client_p
 */
static void
burst_TS6(struct Client *client_p)
{
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	static char ubuf[12];
	struct Client *target_p;
	struct Channel *chptr;
	struct membership *msptr;
	struct hook_burst_channel hinfo;
	dlink_node *ptr;
	dlink_node *uptr;
	char *t;
	int tlen, mlen;
	int cur_len = 0;

	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = ptr->data;

		if(!IsPerson(target_p))
			continue;

		send_umode(NULL, target_p, 0, SEND_UMODES, ubuf);
		if(!*ubuf)
		{
			ubuf[0] = '+';
			ubuf[1] = '\0';
		}

		if(has_id(target_p))
			sendto_one(client_p, ":%s UID %s %d %lu %s %s %s %s %s :%s",
				   target_p->servptr->id, target_p->name,
				   target_p->hopcount + 1, 
				   (unsigned long) target_p->tsinfo, ubuf,
				   target_p->username, target_p->host,
				   IsIPSpoof(target_p) ? "0" : target_p->sockhost,
				   target_p->id, target_p->info);
		else
			sendto_one(client_p, "NICK %s %d %lu %s %s %s %s :%s",
					target_p->name,
					target_p->hopcount + 1,
					(unsigned long) target_p->tsinfo,
					ubuf,
					target_p->username, target_p->host,
					target_p->user->server, target_p->info);

		if(ConfigFileEntry.burst_away && !EmptyString(target_p->user->away))
			sendto_one(client_p, ":%s AWAY :%s",
				   use_id(target_p),
				   target_p->user->away);
	}

	DLINK_FOREACH(ptr, global_channel_list.head)
	{
		chptr = ptr->data;

		s_assert(dlink_list_length(&chptr->members) > 0);
		if(dlink_list_length(&chptr->members) <= 0)
			continue;

		if(*chptr->chname != '#')
			return;

		hinfo.chptr = chptr;
		hinfo.client = client_p;
		hook_call_event(h_burst_channel_id, &hinfo);

		*modebuf = *parabuf = '\0';
		channel_modes(chptr, client_p, modebuf, parabuf);

		cur_len = mlen = ircsprintf(buf, ":%s SJOIN %lu %s %s %s:", me.name,
				(unsigned long) chptr->channelts,
				chptr->chname, modebuf, parabuf);

		t = buf + mlen;

		DLINK_FOREACH(uptr, chptr->members.head)
		{
			msptr = uptr->data;

			tlen = strlen(use_id(msptr->client_p)) + 1;
			if(is_chanop(msptr))
				tlen++;
			if(is_voiced(msptr))
				tlen++;

			if(cur_len + tlen >= BUFSIZE - 3)
			{
				*(t-1) = '\0';
				sendto_one(client_p, "%s", buf);
				cur_len = mlen;
				t = buf + mlen;
			}

			ircsprintf(t, "%s%s ", find_channel_status(msptr, 1), 
				   use_id(msptr->client_p));

			cur_len += tlen;
			t += tlen;
		}

		/* remove trailing space */
		*(t-1) = '\0';
		sendto_one(client_p, "%s", buf);

		if(dlink_list_length(&chptr->banlist) > 0)
			burst_modes_TS6(client_p, chptr, &chptr->banlist, 'b');

		if(IsCapable(client_p, CAP_EX) &&
		   dlink_list_length(&chptr->exceptlist) > 0)
			burst_modes_TS6(client_p, chptr, &chptr->exceptlist, 'e');

		if(IsCapable(client_p, CAP_IE) &&
		   dlink_list_length(&chptr->invexlist) > 0)
			burst_modes_TS6(client_p, chptr, &chptr->invexlist, 'I');
	}
}

/*
 * server_estab
 *
 * inputs       - pointer to a struct Client
 * output       -
 * side effects -
 */
int
server_estab(struct Client *client_p)
{
	struct Client *target_p;
	struct ConfItem *aconf;
	const char *inpath;
	static char inpath_ip[HOSTLEN * 2 + USERLEN + 5];
	char *host;
	dlink_node *ptr;

	s_assert(NULL != client_p);
	if(client_p == NULL)
		return -1;
	ClearAccess(client_p);

	strcpy(inpath_ip, get_client_name(client_p, SHOW_IP));
	inpath = get_client_name(client_p, MASK_IP);	/* "refresh" inpath with host */
	host = client_p->name;

	aconf = client_p->localClient->att_conf;

	if((aconf == NULL) || ((aconf->status & CONF_SERVER) == 0) ||
	   irccmp(aconf->name, client_p->name) || !match(aconf->name, client_p->name))
	{
		/* This shouldn't happen, better tell the ops... -A1kmm */
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Warning: Lost connect{} block for server %s!", host);
		return exit_client(client_p, client_p, client_p, "Lost connect{} block!");
	}

	/* We shouldn't have to check this, it should already done before
	 * server_estab is called. -A1kmm
	 */
	memset((void *) client_p->localClient->passwd, 0, sizeof(client_p->localClient->passwd));

	/* Its got identd , since its a server */
	SetGotId(client_p);

	/* If there is something in the serv_list, it might be this
	 * connecting server..
	 */
	if(!ServerInfo.hub && serv_list.head)
	{
		if(client_p != serv_list.head->data || serv_list.head->next)
		{
			ServerStats->is_ref++;
			sendto_one(client_p, "ERROR :I'm a leaf not a hub");
			return exit_client(client_p, client_p, client_p, "I'm a leaf");
		}
	}

	if(IsUnknown(client_p))
	{
		/*
		 * jdc -- 1.  Use EmptyString(), not [0] index reference.
		 *        2.  Check aconf->spasswd, not aconf->passwd.
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

		send_capabilities(client_p, aconf, default_server_capabs
				  | ((aconf->flags & CONF_FLAGS_COMPRESSED) ?
				     CAP_ZIP_SUPPORTED : 0));

		sendto_one(client_p, "SERVER %s 1 :%s%s",
			   me.name,
			   ConfigServerHide.hidden ? "(H) " : "",
			   (me.info[0]) ? (me.info) : "IRCers United");
	}

	/*
	 * XXX - this should be in s_bsd
	 */
	if(!set_sock_buffers(client_p->localClient->fd, READBUF_SIZE))
		report_error(L_ALL, SETBUF_ERROR_MSG, get_client_name(client_p, SHOW_IP), errno);

	/* XXX - ZIP */
	if(IsCapable(client_p, CAP_ZIP))
	{
	}

	sendto_one(client_p, "SVINFO %d %d 0 :%lu",
		   TS_CURRENT, TS_MIN, (unsigned long) CurrentTime);

	client_p->servptr = &me;

	if(IsDeadorAborted(client_p))
		return CLIENT_EXITED;

	SetServer(client_p);

	/* Update the capability combination usage counts */
	set_chcap_usage_counts(client_p);

	dlinkAdd(client_p, &client_p->lnode, &me.serv->servers);
	dlinkMoveNode(&client_p->localClient->tnode, &unknown_list, &serv_list);
	dlinkAddTailAlloc(client_p, &global_serv_list);

	if(has_id(client_p))
		add_to_id_hash(client_p->id, client_p);

	add_to_client_hash(client_p->name, client_p);
	/* doesnt duplicate client_p->serv if allocated this struct already */
	make_server(client_p);
	client_p->serv->up = me.name;
	client_p->serv->upid = me.id;
	/* add it to scache */
	find_or_add(client_p->name);
	client_p->firsttime = CurrentTime;
	/* fixing eob timings.. -gnp */

	/* Show the real host/IP to admins */
	sendto_realops_flags(UMODE_ALL, L_ADMIN,
			     "Link with %s established: (%s) link",
			     inpath_ip, show_capabilities(client_p));

	/* Now show the masked hostname/IP to opers */
	sendto_realops_flags(UMODE_ALL, L_OPER,
			     "Link with %s established: (%s) link",
			     inpath, show_capabilities(client_p));

	ilog(L_NOTICE, "Link with %s established: (%s) link",
	     log_client_name(client_p, SHOW_IP), show_capabilities(client_p));

	client_p->serv->sconf = aconf;
	fd_note(client_p->localClient->fd, "Server: %s", client_p->name);

	/*
	 ** Old sendto_serv_but_one() call removed because we now
	 ** need to send different names to different servers
	 ** (domain name matching) Send new server to other servers.
	 */
	DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = ptr->data;

		if(target_p == client_p)
			continue;

		if(DoesTS6(target_p) && has_id(client_p))
			sendto_one(target_p, ":%s SID %s 2 %s :%s%s",
				   me.id, client_p->name, client_p->id,
				   IsHidden(client_p) ? "(H) " : "", client_p->info);
		else
			sendto_one(target_p, ":%s SERVER %s 2 :%s%s",
				   me.name, client_p->name,
				   IsHidden(client_p) ? "(H) " : "", client_p->info);
	}

	/*
	 ** Pass on my client information to the new server
	 **
	 ** First, pass only servers (idea is that if the link gets
	 ** cancelled beacause the server was already there,
	 ** there are no NICK's to be cancelled...). Of course,
	 ** if cancellation occurs, all this info is sent anyway,
	 ** and I guess the link dies when a read is attempted...? --msa
	 ** 
	 ** Note: Link cancellation to occur at this point means
	 ** that at least two servers from my fragment are building
	 ** up connection this other fragment at the same time, it's
	 ** a race condition, not the normal way of operation...
	 **
	 ** ALSO NOTE: using the get_client_name for server names--
	 **    see previous *WARNING*!!! (Also, original inpath
	 **    is destroyed...)
	 */

	DLINK_FOREACH(ptr, global_serv_list.head)
	{
		target_p = ptr->data;

		/* target_p->from == target_p for target_p == client_p */
		if(IsMe(target_p) || target_p->from == client_p)
			continue;

		/* presumption, if target has an id, so does its uplink */
		if(DoesTS6(client_p) && has_id(target_p))
			sendto_one(client_p, ":%s SID %s %d %s :%s%s",
				   target_p->serv->upid, target_p->name,
				   target_p->hopcount + 1, target_p->id,
				   IsHidden(client_p) ? "(H) " : "", client_p->info);
		else
			sendto_one(client_p, ":%s SERVER %s %d :%s%s",
				   target_p->serv->up,
				   target_p->name, target_p->hopcount + 1,
				   IsHidden(target_p) ? "(H) " : "", target_p->info);
	}

	if(DoesTS6(client_p))
		burst_TS6(client_p);
	else
		burst_TS5(client_p);
		
	if(IsCapable(client_p, CAP_EOB))
		sendto_one(client_p, ":%s EOB", get_id(&me, client_p));

	/* Always send a PING after connect burst is done */
	sendto_one(client_p, "PING :%s", get_id(&me, client_p));

	return 0;
}

