/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_resv.c: Reserves(jupes) a nickname or channel.
 *
 *  Copyright (C) 2001-2002 Hybrid Development Team
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
#include "handlers.h"
#include "client.h"
#include "channel.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "hash.h"
#include "s_log.h"
#include "sprintf_irc.h"
#include "cluster.h"

static int mo_resv(struct Client *, struct Client *, int, const char **);
static int ms_resv(struct Client *, struct Client *, int, const char **);
static int mo_unresv(struct Client *, struct Client *, int, const char **);
static int ms_unresv(struct Client *, struct Client *, int, const char **);

static void parse_resv(struct Client *source_p, const char *name,
			const char *reason, int cluster);
static void remove_resv(struct Client *source_p, const char *name,
			int cluster);

struct Message resv_msgtab = {
	"RESV", 0, 0, 3, 0, MFLG_SLOW | MFLG_UNREG, 0,
	{m_ignore, m_not_oper, ms_resv, mo_resv}
};

struct Message unresv_msgtab = {
	"UNRESV", 0, 0, 2, 0, MFLG_SLOW | MFLG_UNREG, 0,
	{m_ignore, m_not_oper, ms_unresv, mo_unresv}
};

mapi_clist_av1 resv_clist[] = {
	&resv_msgtab, &unresv_msgtab, NULL
};
DECLARE_MODULE_AV1(resv, NULL, NULL, resv_clist, NULL, NULL, NULL, "$Revision$");

/*
 * mo_resv()
 *      parv[0] = sender prefix
 *      parv[1] = channel/nick to forbid
 *      parv[2] = reason
 */
static int
mo_resv(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *reason;

	if(parc == 5)
		reason = parv[4];
	else
		reason = parv[2];

	if(EmptyString(parv[1]) || EmptyString(reason))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
			   me.name, source_p->name, "RESV");
		return 0;
	}

	/* remote resv.. */
	if((parc == 5) && (irccmp(parv[2], "ON") == 0))
	{
		sendto_match_servs(source_p, parv[3], CAP_CLUSTER,
				   "RESV %s %s :%s",
				   parv[3], parv[1], reason);

		if(match(parv[3], me.name) == 0)
			return 0;
	}

	else if(dlink_list_length(&cluster_list) > 0)
	{
		cluster_resv(source_p, parv[1], reason);
	}

	parse_resv(source_p, parv[1], reason, 0);

	return 0;
}

/* ms_resv()
 *     parv[0] = sender prefix
 *     parv[1] = target server
 *     parv[2] = channel/nick to forbid
 *     parv[3] = reason
 */
static int
ms_resv(struct Client *client_p, struct Client *source_p,
	int parc, const char *parv[])
{
	if((parc != 4) || EmptyString(parv[3]))
		return 0;

	/* parv[0]  parv[1]        parv[2]  parv[3]
	 * oper     target server  resv     reason
	 */
	sendto_match_servs(source_p, parv[1], CAP_CLUSTER,
			   "RESV %s %s :%s",
			   parv[1], parv[2], parv[3]);

	if(!match(parv[1], me.name))
		return 0;

	if(!IsPerson(source_p))
		return 0;

	if(find_cluster(source_p->user->server, CLUSTER_RESV))
	{
		parse_resv(source_p, parv[2], parv[3], 1);
	}
	else if(find_shared(source_p->username, source_p->host, 
			    source_p->user->server, OPER_RESV))
	{
		parse_resv(source_p, parv[2], parv[3], 0);
	}

	return 0;
}

/* parse_resv()
 *
 * inputs       - source_p if error messages wanted
 * 		- thing to resv
 * 		- reason for resv
 * outputs	-
 * side effects - will parse the resv and create it if valid
 */
static void
parse_resv(struct Client *source_p, const char *name, 
	   const char *reason, int cluster)
{
	if(IsChannelName(name))
	{
		struct rxconf *resv_p;

		resv_p = make_rxconf(name, reason, 0, CONF_RESV|RESV_CHANNEL);

		if(resv_p == NULL)
		{
			if(!cluster)
				sendto_one(source_p,
					   ":%s NOTICE %s :A RESV has already been placed on channel: %s",
					   me.name, source_p->name, name);
			return;
		}

		add_rxconf(resv_p);
		write_confitem(RESV_TYPE, source_p, NULL, resv_p->name, resv_p->reason,
			       NULL, NULL, 0);
	}
	else if(clean_resv_nick(name))
	{
		struct rxconf *resv_p;

		if(!valid_wild_card_simple(name))
		{
			if(!cluster)
				sendto_one(source_p,
					   ":%s NOTICE %s :Please include at least %d"
					   " non-wildcard characters with the resv",
					   me.name, source_p->name,
					   ConfigFileEntry.min_nonwildcard_simple);
			return;
		}

		resv_p = make_rxconf(name, reason, 0, CONF_RESV|RESV_NICK);

		if(resv_p == NULL)
		{
			if(!cluster)
				sendto_one(source_p,
					   ":%s NOTICE %s :A RESV has already been placed on nick: %s",
					   me.name, source_p->name, name);
			return;
		}

		add_rxconf(resv_p);
		write_confitem(RESV_TYPE, source_p, NULL, resv_p->name, resv_p->reason,
			       NULL, NULL, 0);
	}
	else if(!cluster)
		sendto_one(source_p,
			   ":%s NOTICE %s :You have specified an invalid resv: [%s]",
			   me.name, source_p->name, name);
}

/*
 * mo_unresv()
 *     parv[0] = sender prefix
 *     parv[1] = channel/nick to unforbid
 */
static int
mo_unresv(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(EmptyString(parv[1]))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
			   me.name, source_p->name, "RESV");
		return 0;
	}

	if((parc == 4) && (irccmp(parv[2], "ON") == 0))
	{
		sendto_match_servs(source_p, parv[3], CAP_CLUSTER,
				   "UNRESV %s %s",
				   parv[3], parv[1]);

		if(match(parv[3], me.name) == 0)
			return 0;
	}
	else if(dlink_list_length(&cluster_list) > 0)
	{
		cluster_unresv(source_p, parv[1]);
	}

	remove_resv(source_p, parv[1], 0);
	return 0;
}

/* ms_unresv()
 *     parv[0] = sender prefix
 *     parv[1] = target server
 *     parv[2] = resv to remove
 */
static int
ms_unresv(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if((parc != 3) || EmptyString(parv[2]))
		return 0;

	/* parv[0]  parv[1]        parv[2]
	 * oper     target server  resv to remove
	 */
	sendto_match_servs(source_p, parv[1], CAP_CLUSTER,
			   "UNRESV %s %s",
			   parv[1], parv[2]);

	if(!match(me.name, parv[1]))
		return 0;

	if(!IsPerson(source_p))
		return 0;

	if(find_cluster(source_p->user->server, CLUSTER_UNRESV))
	{
		remove_resv(source_p, parv[2], 1);
	}
	else if(find_shared(source_p->username, source_p->host, 
			    source_p->user->server, OPER_RESV))
	{
		remove_resv(source_p, parv[2], 0);
	}

	return 0;
}

/* remove_resv()
 *
 * inputs	- client removing the resv
 * 		- resv to remove
 * 		- whether this is done as a cluster or not
 * outputs	-
 * side effects - resv if found, is removed
 */
static void
remove_resv(struct Client *source_p, const char *name, int cluster)
{
	FBFILE *in, *out;
	char buf[BUFSIZE];
	char buff[BUFSIZE];
	char temppath[BUFSIZE];
	const char *filename;
	mode_t oldumask;
	char *p;
	int error_on_write = 0;
	int found_resv = 0;

	ircsprintf(temppath, "%s.tmp", ConfigFileEntry.resvfile);
	filename = get_conf_name(RESV_TYPE);

	if((in = fbopen(filename, "r")) == NULL)
	{
		sendto_one(source_p, ":%s NOTICE %s :Cannot open %s",
			   me.name, source_p->name, filename);
		return;
	}

	oldumask = umask(0);

	if((out = fbopen(temppath, "w")) == NULL)
	{
		if(!cluster)
			sendto_one(source_p, ":%s NOTICE %s :Cannot open %s",
				   me.name, source_p->name, temppath);
		fbclose(in);
		umask(oldumask);
		return;
	}

	umask(oldumask);

	while (fbgets(buf, sizeof(buf), in))
	{
		const char *resv_name;

		if(error_on_write)
		{
			if(temppath != NULL)
				(void) unlink(temppath);

			break;
		}

		strlcpy(buff, buf, sizeof(buff));

		if((p = strchr(buff, '\n')) != NULL)
			*p = '\0';

		if((*buff == '\0') || (*buff == '#'))
		{
			error_on_write = (fbputs(buf, out) < 0) ? YES : NO;
			continue;
		}

		if((resv_name = getfield(buff)) == NULL)
		{
			error_on_write = (fbputs(buf, out) < 0) ? YES : NO;
			continue;
		}

		if(irccmp(resv_name, name) == 0)
		{
			found_resv++;
		}
		else
		{
			error_on_write = (fbputs(buf, out) < 0) ? YES : NO;
		}
	}

	fbclose(in);
	fbclose(out);

	if(!error_on_write)
	{
		(void) rename(temppath, filename);
		rehash(0);
	}
	else
	{
		if(!cluster)
			sendto_one(source_p,
				   ":%s NOTICE %s :Couldn't write temp resv file, aborted",
				   me.name, source_p->name);
		return;
	}

	if(!found_resv)
	{
		if(!cluster)
			sendto_one(source_p, ":%s NOTICE %s :No RESV for %s",
				   me.name, source_p->name, name);
		return;
	}

	if(!cluster)
		sendto_one(source_p, ":%s NOTICE %s :RESV for [%s] is removed",
			   me.name, source_p->name, name);
	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "%s has removed the RESV for: [%s]", get_oper_name(source_p), name);
	ilog(L_NOTICE, "%s has removed the RESV for [%s]", get_oper_name(source_p), name);
}
