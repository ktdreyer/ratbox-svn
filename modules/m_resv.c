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

static int mo_resv(struct Client *, struct Client *, int, const char **);
static int ms_resv(struct Client *, struct Client *, int, const char **);
static int mo_unresv(struct Client *, struct Client *, int, const char **);
static int ms_unresv(struct Client *, struct Client *, int, const char **);

struct Message resv_msgtab = {
	"RESV", 0, 0, 0, MFLG_SLOW | MFLG_UNREG,
	{mg_ignore, mg_not_oper, {ms_resv, 4}, {ms_resv, 4}, {mo_resv, 3}}
};
struct Message unresv_msgtab = {
	"UNRESV", 0, 0, 0, MFLG_SLOW | MFLG_UNREG,
	{mg_ignore, mg_not_oper, {ms_unresv, 3}, {ms_unresv, 3}, {mo_unresv, 2}}
};

mapi_clist_av1 resv_clist[] = {	&resv_msgtab, &unresv_msgtab, NULL };
DECLARE_MODULE_AV1(resv, NULL, NULL, resv_clist, NULL, NULL, "$Revision$");

static void parse_resv(struct Client *source_p, const char *name,
			const char *reason, int temp_time);
static void remove_resv(struct Client *source_p, const char *name);
static int remove_temp_resv(struct Client *source_p, const char *name);

/*
 * mo_resv()
 *      parv[0] = sender prefix
 *      parv[1] = channel/nick to forbid
 *      parv[2] = reason
 */
static int
mo_resv(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *name;
	const char *reason;
	const char *target_server = NULL;
	int temp_time;
	int loc = 1;

	/* RESV [time] <name> [ON <server>] :<reason> */

	if((temp_time = valid_temp_time(parv[loc])) >= 0)
		loc++;

	name = parv[loc];
	loc++;

	if((parc >= loc+2) && (irccmp(parv[2], "ON") == 0))
	{
		target_server = parv[loc+1];
		loc += 2;
	}

	if(parc <= loc || EmptyString(parv[loc]))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
			   me.name, source_p->name, "RESV");
		return 0;
	}

	reason = parv[loc];

	/* remote resv.. */
	if(target_server)
	{
		sendto_match_servs(source_p, parv[3], CAP_CLUSTER,
				   "RESV %s %s :%s",
				   parv[3], parv[1], reason);

		if(match(parv[3], me.name) == 0)
			return 0;
	}
#ifdef XXX_BROKEN_CLUSTER
	else if(dlink_list_length(&cluster_conf_list) > 0)
	{
		cluster_resv(source_p, parv[1], reason);
	}
#endif

	parse_resv(source_p, name, reason, temp_time);

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

	if(find_shared_conf(source_p->username, source_p->host,
				source_p->user->server, SHARED_RESV))
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
	   const char *reason, int temp_time)
{
	struct ConfItem *aconf;

	if(IsChannelName(name))
	{
		if(find_channel_resv(name))
		{
			sendto_one_notice(source_p,
					":A RESV has already been placed on channel: %s",
					name);
			return;
		}

		if(strlen(name) > CHANNELLEN)
		{
			sendto_one_notice(source_p, ":Invalid RESV length: %s",
					  name);
			return;
		}

		aconf = make_conf();
		aconf->status = CONF_RESV_CHANNEL;
		DupString(aconf->name, name);
		DupString(aconf->passwd, reason);
		add_to_resv_hash(aconf->name, aconf);

		if(temp_time > 0)
		{
			aconf->hold = CurrentTime + temp_time;

			sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s added temporary %d min. RESV for [%s] [%s]",
				     get_oper_name(source_p), temp_time / 60,
				     name, reason);
			ilog(L_KLINE, "%s added temporary %d min. RESV for [%s] [%s]",
				get_oper_name(source_p), temp_time / 60,
				name, reason);
			sendto_one_notice(source_p, ":Added temporary %d min. RESV [%s]",
					temp_time / 60, name);
		}
		else
			write_confitem(RESV_TYPE, source_p, NULL, aconf->name, 
					aconf->passwd, NULL, NULL, 0);
	}
	else if(clean_resv_nick(name))
	{
		if(strlen(name) > NICKLEN*2)
		{
			sendto_one_notice(source_p, ":Invalid RESV length: %s",
					   name);
			return;
		}

		if(!valid_wild_card_simple(name))
		{
			sendto_one_notice(source_p,
					   ":Please include at least %d non-wildcard "
					   "characters with the resv",
					   ConfigFileEntry.min_nonwildcard_simple);
			return;
		}

		if(find_nick_resv(name))
		{
			sendto_one_notice(source_p,
					   ":A RESV has already been placed on nick: %s",
					   name);
			return;
		}

		aconf = make_conf();
		aconf->status = CONF_RESV_NICK;
		DupString(aconf->name, name);
		DupString(aconf->passwd, reason);
		dlinkAddAlloc(aconf, &resv_conf_list);

		if(temp_time > 0)
		{
			aconf->hold = CurrentTime + (temp_time * 60);

			sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s added temporary %d min. RESV for [%s] [%s]",
				     get_oper_name(source_p), temp_time / 60,
				     name, reason);
			ilog(L_KLINE, "%s added temporary %d min. RESV for [%s] [%s]",
				get_oper_name(source_p), temp_time / 60,
				name, reason);
			sendto_one_notice(source_p, ":Added temporary %d min. RESV [%s]",
					temp_time / 60, name);
		}
		else
			write_confitem(RESV_TYPE, source_p, NULL, aconf->name, 
					aconf->passwd, NULL, NULL, 0);
	}
	else
		sendto_one_notice(source_p,
				  ":You have specified an invalid resv: [%s]",
				  name);
}

/*
 * mo_unresv()
 *     parv[0] = sender prefix
 *     parv[1] = channel/nick to unforbid
 */
static int
mo_unresv(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if((parc == 4) && (irccmp(parv[2], "ON") == 0))
	{
		sendto_match_servs(source_p, parv[3], CAP_CLUSTER,
				   "UNRESV %s %s",
				   parv[3], parv[1]);

		if(match(parv[3], me.name) == 0)
			return 0;
	}
#ifdef XXX_BROKEN_CLUSTER
	else if(dlink_list_length(&cluster_conf_list) > 0)
	{
		cluster_unresv(source_p, parv[1]);
	}
#endif

	if(remove_temp_resv(source_p, parv[1]))
	{
		sendto_one_notice(source_p, ":RESV for [%s] is removed", parv[1]);
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s has removed the temporary RESV for: [%s]", 
				     get_oper_name(source_p), parv[1]);
		ilog(L_KLINE, "%s has removed the temporary RESV for [%s]", 
			get_oper_name(source_p), parv[1]);
		return 0;
	}

	remove_resv(source_p, parv[1]);
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

	if(find_shared_conf(source_p->username, source_p->host,
				source_p->user->server, SHARED_UNRESV))
	{
		if(remove_temp_resv(source_p, parv[1]))
		{
			sendto_one_notice(source_p, ":RESV for [%s] is removed", parv[1]);
			sendto_realops_flags(UMODE_ALL, L_ALL,
					"%s has removed the RESV for: [%s]", 
					get_oper_name(source_p), parv[1]);
			ilog(L_KLINE, "%s has removed the RESV for [%s]", 
					get_oper_name(source_p), parv[1]);
			return 0;
		}

		remove_resv(source_p, parv[2]);
	}

	return 0;
}

static int
remove_temp_resv(struct Client *source_p, const char *name)
{
	struct ConfItem *aconf;

	if(IsChannelName(name))
	{
		if((aconf = hash_find_resv(name)) == NULL)
			return 0;

		/* its permanent, let remove_resv do it properly */
		if(!aconf->hold)
			return 0;

		del_from_resv_hash(name, aconf);
		free_conf(aconf);
		return 1;
	}

	if((aconf = find_nick_resv(name)) == NULL)
		return 0;

	/* permanent, remove_resv() needs to do it properly */
	if(!aconf->hold)
		return 0;

	dlinkFindDestroy(&resv_conf_list, aconf);
	free_conf(aconf);
	return 1;
}

/* remove_resv()
 *
 * inputs	- client removing the resv
 * 		- resv to remove
 * outputs	-
 * side effects - resv if found, is removed
 */
static void
remove_resv(struct Client *source_p, const char *name)
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
		sendto_one_notice(source_p, ":Cannot open %s", filename);
		return;
	}

	oldumask = umask(0);

	if((out = fbopen(temppath, "w")) == NULL)
	{
		sendto_one_notice(source_p, ":Cannot open %s", temppath);
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

	if(error_on_write)
	{
		sendto_one_notice(source_p, ":Couldn't write temp resv file, aborted");
		return;
	}
	else if(!found_resv)
	{
		sendto_one_notice(source_p, ":No RESV for %s", name);

		if(temppath != NULL)
			(void) unlink(temppath);

		return;
	}

	(void) rename(temppath, filename);
	rehash(0);

	sendto_one_notice(source_p, ":RESV for [%s] is removed", name);
	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "%s has removed the RESV for: [%s]", get_oper_name(source_p), name);
	ilog(L_KLINE, "%s has removed the RESV for [%s]", get_oper_name(source_p), name);
}
