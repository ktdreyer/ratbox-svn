/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_kline.c: Bans/unbans a user.
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
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "hostmask.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_log.h"
#include "send.h"
#include "hash.h"
#include "s_serv.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "cluster.h"
#include "event.h"

static int mo_kline(struct Client *, struct Client *, int, const char **);
static int ms_kline(struct Client *, struct Client *, int, const char **);
static int mo_unkline(struct Client *, struct Client *, int, const char **);
static int ms_unkline(struct Client *, struct Client *, int, const char **);

struct Message kline_msgtab = {
	"KLINE", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, {ms_kline, 6}, {ms_kline, 6}, {mo_kline, 2}}
};

struct Message unkline_msgtab = {
	"UNKLINE", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, {ms_unkline, 4}, {ms_unkline, 4}, {mo_unkline, 2}}
};

mapi_clist_av1 kline_clist[] = { &kline_msgtab, &unkline_msgtab, NULL };
DECLARE_MODULE_AV1(kline, NULL, NULL, kline_clist, NULL, NULL, "$Revision$");

/* Local function prototypes */
static time_t valid_tkline(struct Client *source_p, const char *string);
static int find_user_host(struct Client *source_p, const char *userhost, char *user, char *host);
static int valid_comment(char *comment);
static int valid_user_host(const char *user, const char *host);
static int valid_wild_card(const char *user, const char *host);

static void apply_kline(struct Client *source_p, struct ConfItem *aconf,
			const char *reason, const char *oper_reason, const char *current_date);
static void apply_tkline(struct Client *source_p, struct ConfItem *aconf,
			 const char *, const char *, const char *, int);
static int already_placed_kline(struct Client *, const char *, const char *, int);

static void remove_permkline_match(struct Client *, const char *, const char *, int);
static int flush_write(struct Client *, FBFILE *, const char *, const char *);
static int remove_temp_kline(const char *, const char *);

/* mo_kline()
 *
 *   parv[1] - temp time or user@host
 *   parv[2] - user@host, "ON", or reason
 *   parv[3] - "ON", reason, or server to target
 *   parv[4] - server to target, or reason
 *   parv[5] - reason
 */
static int
mo_kline(struct Client *client_p, struct Client *source_p,
	 int parc, const char **parv)
{
	char def[] = "No Reason";
	char user[USERLEN + 2];
	char host[HOSTLEN + 2];
	char buffer[IRCD_BUFSIZE];
	char *reason = def;
	char *oper_reason;
	const char *current_date;
	const char *target_server = NULL;
	struct ConfItem *aconf;
	time_t tkline_time = 0;

	if(!IsOperK(source_p))
	{
		sendto_one(source_p, ":%s NOTICE %s :You need kline = yes;",
			   me.name, source_p->name);
		return 0;
	}

	parv++;
	parc--;

	tkline_time = valid_tkline(source_p, *parv);

	if(tkline_time > 0)
	{
		parv++;
		parc--;
	}

	if(parc == 0)
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
			   me.name, source_p->name, "KLINE");
		return 0;
	}

	if(find_user_host(source_p, *parv, user, host) == 0)
		return 0;

	parc--;
	parv++;

	if(parc != 0)
	{
		if(irccmp(*parv, "ON") == 0)
		{
			parc--;
			parv++;
			if(parc == 0)
			{
				sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
					   me.name, source_p->name, "KLINE");
				return 0;
			}
			target_server = *parv;
			parc--;
			parv++;
		}
	}

	if(parc != 0)
		reason = LOCAL_COPY(*parv);

	if(!valid_user_host(user, host))
	{
		sendto_one(source_p, ":%s NOTICE %s :Invalid K-Line",
			   me.name, source_p->name);
		return 0;
	}

	if(!valid_wild_card(user, host))
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Please include at least %d non-wildcard characters with the user@host",
			   me.name, source_p->name, ConfigFileEntry.min_nonwildcard);
		return 0;
	}

	if(!valid_comment(reason))
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Invalid character '\"' in comment",
			   me.name, source_p->name);
		return 0;
	}

	if(target_server != NULL)
	{
		sendto_match_servs(source_p, target_server, CAP_KLN,
				   "KLINE %s %lu %s %s :%s",
				   target_server, (unsigned long) tkline_time, user, host, reason);

		/* If we are sending it somewhere that doesnt include us, stop */
		if(!match(target_server, me.name))
			return 0;
	}
	/* if we have cluster servers, send it to them.. */
	else if(dlink_list_length(&cluster_list) > 0)
	{
		cluster_kline(source_p, tkline_time, user, host, reason);
	}

	if(already_placed_kline(source_p, user, host, tkline_time))
		return 0;

	set_time();
	current_date = smalldate();
	aconf = make_conf();
	aconf->status = CONF_KILL;
	DupString(aconf->host, host);
	DupString(aconf->user, user);
	aconf->port = 0;

	/* Look for an oper reason */
	if((oper_reason = strchr(reason, '|')) != NULL)
	{
		*oper_reason = '\0';
		oper_reason++;

		if(!EmptyString(oper_reason))
			DupString(aconf->spasswd, oper_reason);
	}

	if(tkline_time)
	{
		ircsnprintf(buffer, sizeof(buffer),
			   "Temporary K-line %d min. - %s (%s)",
			   (int) (tkline_time / 60), reason, current_date);
		DupString(aconf->passwd, buffer);
		apply_tkline(source_p, aconf, reason, oper_reason, current_date, tkline_time);
	}
	else
	{
		ircsnprintf(buffer, sizeof(buffer), "%s (%s)", reason, current_date);
		DupString(aconf->passwd, buffer);
		apply_kline(source_p, aconf, reason, oper_reason, current_date);
	}

	if(ConfigFileEntry.kline_delay)
	{
		if(kline_queued == 0)
		{
			eventAddOnce("check_klines", check_klines_event, NULL,
				     ConfigFileEntry.kline_delay);
			kline_queued = 1;
		}
	}
	else
		check_klines();

	return 0;
}

/* ms_kline()
 *
 *   parv[1] - server targeted at
 *   parv[2] - tkline time (0 if perm)
 *   parv[3] - user
 *   parv[4] - host
 *   parv[5] - reason
 */
static int
ms_kline(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *current_date;
	struct ConfItem *aconf = NULL;
	int tkline_time;

	const char *kuser;
	const char *khost;
	char *kreason;
	char *oper_reason;

	sendto_match_servs(source_p, parv[1], CAP_KLN,
			   "KLINE %s %s %s %s :%s", parv[1], parv[2], parv[3], parv[4], parv[5]);

	if(!match(parv[1], me.name))
		return 0;

	if(!IsPerson(source_p))
		return 0;

	kuser = parv[3];
	khost = parv[4];
	kreason = LOCAL_COPY(parv[5]);

	if(find_cluster(source_p->user->server, CLUSTER_KLINE))
	{
		if(!valid_user_host(kuser, khost) || !valid_wild_card(kuser, khost) ||
		   !valid_comment(kreason))
			return 0;

		tkline_time = atoi(parv[2]);

		aconf = make_conf();

		aconf->status = CONF_KILL;
		DupString(aconf->user, kuser);
		DupString(aconf->host, khost);

		/* Look for an oper reason */
		if((oper_reason = strchr(kreason, '|')) != NULL)
		{
			*oper_reason = '\0';
			oper_reason++;

			if(!EmptyString(oper_reason))
				DupString(aconf->spasswd, oper_reason);
		}

		DupString(aconf->passwd, kreason);
		current_date = smalldate();

		if(tkline_time)
			apply_tkline(source_p, aconf, kreason, oper_reason,
				     current_date, tkline_time);
		else
			apply_kline(source_p, aconf, aconf->passwd, oper_reason, current_date);
	}
	else if(find_shared(source_p->username, source_p->host, source_p->user->server, OPER_K))
	{
		if(!valid_user_host(kuser, khost))
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "*** %s!%s@%s on %s is requesting an Invalid K-Line for [%s@%s] [%s]",
					     source_p->name, source_p->username, source_p->host,
					     source_p->user->server, kuser, khost, kreason);
			return 0;
		}

		if(!valid_wild_card(kuser, khost))
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "*** %s!%s@%s on %s is requesting a K-Line without %d wildcard chars for [%s@%s] [%s]",
					     source_p->name, source_p->username, source_p->host,
					     source_p->user->server,
					     ConfigFileEntry.min_nonwildcard, kuser, khost,
					     kreason);
			return 0;
		}

		if(!valid_comment(kreason))
			return 0;

		tkline_time = atoi(parv[2]);

		/* We check if the kline already exists after we've announced its 
		 * arrived, to avoid confusing opers - fl
		 */
		if(already_placed_kline(source_p, kuser, khost, tkline_time))
			return 0;

		aconf = make_conf();

		aconf->status = CONF_KILL;
		DupString(aconf->user, kuser);
		DupString(aconf->host, khost);

		/* Look for an oper reason */
		if((oper_reason = strchr(kreason, '|')) != NULL)
		{
			*oper_reason = '\0';
			oper_reason++;
		}

		DupString(aconf->passwd, kreason);
		current_date = smalldate();

		if(tkline_time)
			apply_tkline(source_p, aconf, kreason, oper_reason,
				     current_date, tkline_time);
		else
			apply_kline(source_p, aconf, aconf->passwd, oper_reason, current_date);
	}

	if(ConfigFileEntry.kline_delay)
	{
		if(kline_queued == 0)
		{
			eventAddOnce("check_klines", check_klines_event, NULL,
				     ConfigFileEntry.kline_delay);
			kline_queued = 1;
		}
	}
	else
		check_klines();

	return 0;
}

/* mo_unkline()
 *
 *   parv[1] - kline to remove
 *   parv[2] - optional "ON"
 *   parv[3] - optional target server
 */
static int
mo_unkline(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *user;
	char *host;
	char splat[] = "*";
	char *h = LOCAL_COPY(parv[1]);

	if(!IsOperUnkline(source_p))
	{
		sendto_one(source_p, ":%s NOTICE %s :You need unkline = yes;", me.name, parv[0]);
		return 0;
	}

	if((host = strchr(h, '@')) || *h == '*')
	{
		/* Explicit user@host mask given */

		if(host)	/* Found user@host */
		{
			user = parv[1];	/* here is user part */
			*(host++) = '\0';	/* and now here is host */
		}
		else
		{
			user = splat;	/* no @ found, assume its *@somehost */
			host = h;
		}
	}
	else
	{
		sendto_one(source_p, ":%s NOTICE %s :Invalid parameters", me.name, source_p->name);
		return 0;
	}

	/* possible remote kline.. */
	if((parc > 3) && (irccmp(parv[2], "ON") == 0))
	{
		sendto_match_servs(source_p, parv[3], CAP_UNKLN,
				   "UNKLINE %s %s %s", parv[3], user, host);

		if(match(parv[3], me.name) == 0)
			return 0;
	}
	else if(dlink_list_length(&cluster_list) > 0)
	{
		cluster_unkline(source_p, user, host);
	}

	if(remove_temp_kline(user, host))
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Un-klined [%s@%s] from temporary k-lines",
			   me.name, parv[0], user, host);
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s has removed the temporary K-Line for: [%s@%s]",
				     get_oper_name(source_p), user, host);
		ilog(L_NOTICE, "%s removed temporary K-Line for [%s@%s]", parv[0], user, host);
		return 0;
	}

	remove_permkline_match(source_p, host, user, 0);

	return 0;
}

/* ms_unkline()
 *
 *   parv[1] - target server
 *   parv[2] - user to unkline
 *   parv[3] - host to unkline
 */
static int
ms_unkline(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *kuser;
	const char *khost;

	/* parv[0]  parv[1]        parv[2]  parv[3]
	 * oper     target server  user     host    */
	sendto_match_servs(source_p, parv[1], CAP_UNKLN,
			   "UNKLINE %s %s %s", parv[1], parv[2], parv[3]);

	kuser = parv[2];
	khost = parv[3];

	if(!match(parv[1], me.name))
		return 0;

	if(!IsPerson(source_p))
		return 0;

	if(find_cluster(source_p->user->server, CLUSTER_UNKLINE))
	{
		if(remove_temp_kline(kuser, khost))
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "%s has removed the temporary K-Line for: [%s@%s]",
					     get_oper_name(source_p), kuser, khost);
			ilog(L_NOTICE, "%s removed temporary K-Line for [%s@%s]",
			     source_p->name, kuser, khost);
			return 0;
		}

		remove_permkline_match(source_p, khost, kuser, 1);
	}
	else if(find_shared(source_p->username, source_p->host,
			    source_p->user->server, OPER_UNKLINE))
	{
		if(remove_temp_kline(kuser, khost))
		{
			sendto_one_notice(source_p,
					  ":Un-klined [%s@%s] from temporary k-lines",
					  kuser, khost);

			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "%s has removed the temporary K-Line for: [%s@%s]",
					     get_oper_name(source_p), kuser, khost);

			ilog(L_NOTICE, "%s removed temporary K-Line for [%s@%s]",
			     source_p->name, kuser, khost);
			return 0;
		}

		remove_permkline_match(source_p, khost, kuser, 0);
	}

	return 0;
}

/* apply_kline()
 *
 * inputs	- 
 * output	- NONE
 * side effects	- kline as given, is added to the hashtable
 *		  and conf file
 */
static void
apply_kline(struct Client *source_p, struct ConfItem *aconf,
	    const char *reason, const char *oper_reason, const char *current_date)
{
	add_conf_by_address(aconf->host, CONF_KILL, aconf->user, aconf);
	write_confitem(KLINE_TYPE, source_p, aconf->user, aconf->host,
		       reason, oper_reason, current_date, 0);
}

/* apply_tkline()
 *
 * inputs	-
 * output	- NONE
 * side effects	- tkline as given is placed
 */
static void
apply_tkline(struct Client *source_p, struct ConfItem *aconf,
	     const char *reason, const char *oper_reason, const char *current_date, int tkline_time)
{
	aconf->hold = CurrentTime + tkline_time;
	add_temp_kline(aconf);

	/* no oper reason.. */
	if(EmptyString(oper_reason))
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s added temporary %d min. K-Line for [%s@%s] [%s]",
				     get_oper_name(source_p), tkline_time / 60,
				     aconf->user, aconf->host, reason);
		ilog(L_TRACE, "%s added temporary %d min. K-Line for [%s@%s] [%s]",
		     source_p->name, tkline_time / 60, aconf->user, aconf->host, reason);
	}
	else
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s added temporary %d min. K-Line for [%s@%s] [%s|%s]",
				     get_oper_name(source_p), tkline_time / 60,
				     aconf->user, aconf->host, reason, oper_reason);
		ilog(L_TRACE, "%s added temporary %d min. K-Line for [%s@%s] [%s|%s]",
		     source_p->name, tkline_time / 60,
		     aconf->user, aconf->host, reason, oper_reason);
	}

	sendto_one_notice(source_p, ":Added temporary %d min. K-Line [%s@%s]",
			  tkline_time / 60, aconf->user, aconf->host);
}

/* valid_tkline()
 * 
 * inputs	- client requesting kline, kline time
 * outputs	- 0 if not an integer number, else the number
 * side effects -
 */
static time_t
valid_tkline(struct Client *source_p, const char *p)
{
	time_t result = 0;

	while(*p)
	{
		if(IsDigit(*p))
		{
			result *= 10;
			result += ((*p) & 0xF);
			p++;
		}
		else
			return (0);
	}

	/* if they /quote kline 0 user@host :reason, set it to one minute */
	if(result == 0)
		result = 1;

	/* max at 4 weeks */
	if(result > (60 * 24 * 7 * 4))
		result = (24 * 60 * 7 * 4);

	/* turn into seconds.. */
	result *= 60;

	return (result);
}

/* find_user_host()
 * 
 * inputs	- client placing kline, user@host, user buffer, host buffer
 * output	- 0 if not ok to kline, 1 to kline i.e. if valid user host
 * side effects -
 */
static int
find_user_host(struct Client *source_p, const char *userhost, char *luser, char *lhost)
{
	char *hostp;

	hostp = strchr(userhost, '@');
	
	if(hostp != NULL)	/* I'm a little user@host */
	{
		*(hostp++) = '\0';	/* short and squat */
		if(*userhost)
			strlcpy(luser, userhost, USERLEN + 1);	/* here is my user */
		else
			strcpy(luser, "*");
		if(*hostp)
			strlcpy(lhost, hostp, HOSTLEN + 1);	/* here is my host */
		else
			strcpy(lhost, "*");
		}
	else
	{
		/* no '@', no '.', so its not a user@host or host, therefore
		 * its a nick, which support was removed for.
		 */
		if(strchr(userhost, '.') == NULL)
			return 0;

		luser[0] = '*';	/* no @ found, assume its *@somehost */
		luser[1] = '\0';
		strlcpy(lhost, userhost, HOSTLEN + 1);
	}

	return 1;
}

/* valid_user_host()
 *
 * inputs       - user buffer, host buffer
 * output	- 0 if invalid, 1 if valid
 * side effects -
 */
static int
valid_user_host(const char *luser, const char *lhost)
{
	/* # is invalid, as is '!' (n!u@h kline) */
	if(strchr(lhost, '#') || strchr(luser, '#') || strchr(luser, '!'))
		return 0;

	return 1;
}

/* valid_wild_card()
 * 
 * input        - user buffer, host buffer
 * output       - 0 if invalid, 1 if valid
 * side effects -
 */
static int
valid_wild_card(const char *luser, const char *lhost)
{
	const char *p;
	char tmpch;
	int nonwild = 0;

	/* check there are enough non wildcard chars */
	p = luser;
	while ((tmpch = *p++))
	{
		if(!IsKWildChar(tmpch))
		{
			/*
			 * If we find enough non-wild characters, we can
			 * break - no point in searching further.
			 */
			if(++nonwild >= ConfigFileEntry.min_nonwildcard)
				break;
		}
	}

	/* try host, as user didnt contain enough */
	p = lhost;
	while ((tmpch = *p++))
	{
		if(!IsKWildChar(tmpch))
			if(++nonwild >= ConfigFileEntry.min_nonwildcard)
				break;
	}

	if(nonwild < ConfigFileEntry.min_nonwildcard)
		return 0;

	return 1;
}

/*
 * valid_comment
 * inputs	- pointer to client
 *              - pointer to comment
 * output       - 0 if no valid comment, 1 if valid
 * side effects - NONE
 */
static int
valid_comment(char *comment)
{
	if(strchr(comment, '"'))
		return 0;

	if(strlen(comment) > REASONLEN)
		comment[REASONLEN] = '\0';

	return 1;
}

/* already_placed_kline()
 *
 * inputs       - source to notify, user@host to check, tkline time
 * outputs      - 1 if a perm kline or a tkline when a tkline is being
 *                set exists, else 0
 * side effects - notifies source_p kline exists
 */
/* Note: This currently works if the new K-line is a special case of an
 *       existing K-line, but not the other way round. To do that we would
 *       have to walk the hash and check every existing K-line. -A1kmm.
 */
static int
already_placed_kline(struct Client *source_p, const char *luser, const char *lhost, int tkline)
{
	const char *reason;
	struct sockaddr_storage iphost, *piphost;
	struct ConfItem *aconf;
        int t;
	if(ConfigFileEntry.non_redundant_klines)
	{
		if((t = parse_netmask(lhost, &iphost, NULL)) != HM_HOST)
		{
#ifdef IPV6
			if(t == HM_IPV6)
				t = AF_INET6;
			else
#endif
				t = AF_INET;
				
			piphost = &iphost;
		}
		else
			piphost = NULL;

		if((aconf = find_conf_by_address(lhost, piphost, CONF_KILL, t, luser)))
		{
			/* setting a tkline, or existing one is perm */
			if(tkline || ((aconf->flags & CONF_FLAGS_TEMPORARY) == 0))
			{
				reason = aconf->passwd ? aconf->passwd : "<No Reason>";

				sendto_one_notice(source_p,
						  ":[%s@%s] already K-Lined by [%s@%s] - %s",
						  luser, lhost, aconf->user,
						  aconf->host, reason);
				return 1;
			}
		}
	}

	return 0;
}

/* remove_permkline_match()
 *
 * hunts for a permanent kline, and removes it.
 */
static void
remove_permkline_match(struct Client *source_p, const char *host, const char *user, int cluster)
{
	FBFILE *in, *out;
	int pairme = 0;
	int error_on_write = NO;
	char buf[BUFSIZE];
	char buff[BUFSIZE];
	char temppath[BUFSIZE];
	const char *filename;
	mode_t oldumask;
	char *p;

	ircsnprintf(temppath, sizeof(temppath),
		 "%s.tmp", ConfigFileEntry.klinefile);

	filename = get_conf_name(KLINE_TYPE);

	if((in = fbopen(filename, "r")) == 0)
	{
		sendto_one_notice(source_p, ":Cannot open %s", filename);
		return;
	}

	oldumask = umask(0);
	if((out = fbopen(temppath, "w")) == 0)
	{
		sendto_one_notice(source_p, ":Cannot open %s", temppath);
		fbclose(in);
		umask(oldumask);
		return;
	}
	umask(oldumask);

	while (fbgets(buf, sizeof(buf), in))
	{
		char *found_host, *found_user;

		strlcpy(buff, buf, sizeof(buff));

		if((p = strchr(buff, '\n')) != NULL)
			*p = '\0';

		if((*buff == '\0') || (*buff == '#'))
		{
			if(!error_on_write)
				flush_write(source_p, out, buf, temppath);
			continue;
		}

		if((found_user = getfield(buff)) == NULL)
		{
			if(!error_on_write)
				flush_write(source_p, out, buf, temppath);
			continue;
		}

		if((found_host = getfield(NULL)) == NULL)
		{
			if(!error_on_write)
				flush_write(source_p, out, buf, temppath);
			continue;
		}

		if((irccmp(host, found_host) == 0) && (irccmp(user, found_user) == 0))
		{
			pairme++;
		}
		else
		{
			if(!error_on_write)
				flush_write(source_p, out, buf, temppath);
		}
	}
	fbclose(in);
	fbclose(out);

/* The result of the rename should be checked too... oh well */
/* If there was an error on a write above, then its been reported
 * and I am not going to trash the original kline /conf file
 */
	if(error_on_write)
	{
		sendto_one_notice(source_p, ":Couldn't write temp kline file, aborted");
		return;
	}
	else if(!pairme)
	{
		if(!cluster)
			sendto_one_notice(source_p, ":No K-Line for %s@%s",
					  user, host);

		if(temppath != NULL)
			(void) unlink(temppath);

		return;
	}
		
	(void) rename(temppath, filename);
	rehash(0);

	if(!cluster)
	{
		sendto_one_notice(source_p, ":K-Line for [%s@%s] is removed",
				  user, host);
	}

	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "%s has removed the K-Line for: [%s@%s]",
			     get_oper_name(source_p), user, host);

	ilog(L_NOTICE, "%s removed K-Line for [%s@%s]", source_p->name, user, host);
	return;
}



/*
 * flush_write()
 *
 * inputs       - pointer to client structure of oper requesting unkline
 *              - out is the file descriptor
 *              - buf is the buffer to write
 *              - ntowrite is the expected number of character to be written
 *              - temppath is the temporary file name to be written
 * output       - YES for error on write
 *              - NO for success
 * side effects - if successful, the buf is written to output file
 *                if a write failure happesn, and the file pointed to
 *                by temppath, if its non NULL, is removed.
 *
 * The idea here is, to be as robust as possible when writing to the 
 * kline file.
 *
 * -Dianora
 */

static int
flush_write(struct Client *source_p, FBFILE * out, const char *buf, const char *temppath)
{
	int error_on_write = (fbputs(buf, out) < 0) ? YES : NO;

	if(error_on_write)
	{
		sendto_one_notice(source_p, ":Unable to write to %s",
				  temppath);
		fbclose(out);
		if(temppath != NULL)
			(void) unlink(temppath);
	}
	return (error_on_write);
}

static dlink_list *tkline_list[] = {
	&tkline_hour,
	&tkline_day,
	&tkline_min,
	&tkline_week,
	NULL
};

/* remove_temp_kline()
 *
 * inputs       - username, hostname to unkline
 * outputs      -
 * side effects - tries to unkline anything that matches
 */
static int
remove_temp_kline(const char *user, const char *host)
{
	dlink_list *tklist;
	struct ConfItem *aconf;
	dlink_node *ptr;
	struct sockaddr_storage addr, caddr;
	int bits, cbits;
	int i;

	parse_netmask(host, &addr, &bits);

	for (i = 0; tkline_list[i] != NULL; i++)
	{
		tklist = tkline_list[i];

		DLINK_FOREACH(ptr, tklist->head)
		{
			aconf = ptr->data;

			parse_netmask(aconf->host, &caddr, &cbits);

			if(user && irccmp(user, aconf->user))
				continue;

			if(!irccmp(aconf->host, host) || (bits == cbits
							  && comp_with_mask_sock(&addr,
									    &caddr, bits)))
			{
				dlinkDestroy(ptr, tklist);
				delete_one_address_conf(aconf->host, aconf);
				return YES;
			}
		}
	}

	return NO;
}
