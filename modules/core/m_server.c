/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_server.c: Introduces a server.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2004 ircd-ratbox development team
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
#include "client.h"		/* client struct */
#include "common.h"		/* TRUE bleah */
#include "event.h"
#include "hash.h"		/* add_to_client_hash */
#include "irc_string.h"
#include "ircd.h"		/* me */
#include "numeric.h"		/* ERR_xxx */
#include "s_conf.h"		/* struct ConfItem */
#include "s_newconf.h"
#include "s_log.h"		/* log level defines */
#include "s_serv.h"		/* server_estab, check_server */
#include "s_stats.h"		/* ServerStats */
#include "scache.h"		/* find_or_add */
#include "send.h"		/* sendto_one */
#include "msg.h"
#include "parse.h"
#include "modules.h"

static int mr_server(struct Client *, struct Client *, int, const char **);
static int ms_server(struct Client *, struct Client *, int, const char **);
static int ms_sid(struct Client *, struct Client *, int, const char **);

struct Message server_msgtab = {
	"SERVER", 0, 0, 0, MFLG_SLOW | MFLG_UNREG,
	{{mr_server, 4}, mg_reg, mg_ignore, {ms_server, 4}, mg_ignore, mg_reg}
};
struct Message sid_msgtab = {
	"SID", 0, 0, 0, MFLG_SLOW,
	{mg_ignore, mg_reg, mg_ignore, {ms_sid, 5}, mg_ignore, mg_reg}
};

mapi_clist_av1 server_clist[] = { &server_msgtab, &sid_msgtab, NULL };
DECLARE_MODULE_AV1(server, NULL, NULL, server_clist, NULL, NULL, "$Revision$");

int bogus_host(const char *host);
struct Client *server_exists(const char *);
static int set_server_gecos(struct Client *, const char *);

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

			ilog(L_SERVER, "Access denied, No N line for server %s",
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

		ilog(L_SERVER, "Access denied, invalid password for server %s",
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

		ilog(L_SERVER, "Access denied, invalid host for server %s",
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
		ilog(L_SERVER, "Access denied, invalid servername from %s",
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
	struct remote_conf *hub_p;
	int hop;
	int hlined = 0;
	int llined = 0;
	dlink_node *ptr;

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
	DLINK_FOREACH(ptr, hubleaf_conf_list.head)
	{
		hub_p = ptr->data;

		if(match(hub_p->server, client_p->name) &&
		   match(hub_p->host, name))
		{
			if(hub_p->flags & CONF_HUB)
				hlined++;
			else
				llined++;
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

	/* Ok, check client_p can hub the new server, and make sure it's not a LL */
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

	sendto_server(client_p, NULL, NOCAPS, NOCAPS,
		      ":%s SERVER %s %d :%s%s",
		      source_p->name, target_p->name, target_p->hopcount + 1,
		      IsHidden(target_p) ? "(H) " : "", target_p->info);

	sendto_realops_flags(UMODE_EXTERNAL, L_ALL,
			     "Server %s being introduced by %s", target_p->name, source_p->name);

	return 0;
}

static int
ms_sid(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	struct remote_conf *hub_p;
	dlink_node *ptr;
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

	/* for the directly connected server:
	 * H: allows it to introduce a server matching that mask
	 * L: disallows it introducing a server matching that mask
	 */
	DLINK_FOREACH(ptr, hubleaf_conf_list.head)
	{
		hub_p = ptr->data;

		if(match(hub_p->server, client_p->name) &&
		   match(hub_p->host, parv[1]))
		{
			if(hub_p->flags & CONF_HUB)
				hlined++;
			else
				llined++;
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
	target_p->hopcount = atoi(parv[2]);
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
		      source_p->id, target_p->name, target_p->hopcount + 1,
		      target_p->id,
		      IsHidden(target_p) ? "(H) " : "", target_p->info);
	sendto_server(client_p, NULL, NOCAPS, CAP_TS6,
		      ":%s SERVER %s %d :%s%s",
		      source_p->name, target_p->name, target_p->hopcount + 1,
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
int
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
struct Client *
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
