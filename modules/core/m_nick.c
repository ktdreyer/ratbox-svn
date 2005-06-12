/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_nick.c: Sets a users nick.
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
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_stats.h"
#include "s_user.h"
#include "hash.h"
#include "whowas.h"
#include "s_serv.h"
#include "send.h"
#include "parse.h"
#include "modules.h"
#include "scache.h"
#include "s_newconf.h"
#include "monitor.h"
#include "commio.h"

static int mr_nick(struct Client *, struct Client *, int, const char **);
static int m_nick(struct Client *, struct Client *, int, const char **);
static int mc_nick(struct Client *, struct Client *, int, const char **);
static int ms_nick(struct Client *, struct Client *, int, const char **);
static int ms_uid(struct Client *, struct Client *, int, const char **);

struct Message nick_msgtab = {
	"NICK", 0, 0, 0, MFLG_SLOW,
	{{mr_nick, 0}, {m_nick, 0}, {mc_nick, 3}, {ms_nick, 8}, mg_ignore, {m_nick, 0}}
};
struct Message uid_msgtab = {
	"UID", 0, 0, 0, MFLG_SLOW,
	{mg_ignore, mg_ignore, mg_ignore, {ms_uid, 9}, mg_ignore, mg_ignore}
};

mapi_clist_av1 nick_clist[] = { &nick_msgtab, &uid_msgtab, NULL };
DECLARE_MODULE_AV1(nick, NULL, NULL, nick_clist, NULL, NULL, "$Revision$");

static int change_remote_nick(struct Client *, struct Client *, int, const char **, time_t, const char *);

static int clean_nick(const char *, int loc_client);
static int clean_username(const char *);
static int clean_host(const char *);
static int clean_uid(const char *uid);

static void set_initial_nick(struct Client *client_p, struct Client *source_p, char *nick);
static void change_local_nick(struct Client *client_p, struct Client *source_p, char *nick);
static int register_client(struct Client *client_p, struct Client *server, 
			   const char *nick, time_t newts, int parc, const char *parv[]);

static int perform_nick_collides(struct Client *, struct Client *,
				 struct Client *, int, const char **, 
				 time_t, const char *, const char *);
static int perform_nickchange_collides(struct Client *, struct Client *,
				       struct Client *, int, const char **, 
				       time_t, const char *);

/* mr_nick()
 *       parv[0] = sender prefix
 *       parv[1] = nickname
 */
static int
mr_nick(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	char nick[NICKLEN];
	char *s;

	if(parc < 2 || EmptyString(parv[1]) || (parv[1][0] == '~'))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NONICKNAMEGIVEN),
			   me.name, 
			   EmptyString(source_p->name) ? "*" : source_p->name);
		return 0;
	}

	/* due to the scandinavian origins, (~ being uppercase of ^) and ~
	 * being disallowed as a nick char, we need to chop the first ~
	 * instead of just erroring.
	 */
	if((s = strchr(parv[1], '~')))
		*s = '\0';

	/* copy the nick and terminate it */
	strlcpy(nick, parv[1], sizeof(nick));

	/* check the nickname is ok */
	if(!clean_nick(nick, 1))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_ERRONEUSNICKNAME),
			   me.name, EmptyString(parv[0]) ? "*" : parv[0], parv[1]);
		return 0;
	}

	/* check if the nick is resv'd */
	if(find_nick_resv(nick))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_ERRONEUSNICKNAME),
			   me.name, EmptyString(source_p->name) ? "*" : source_p->name, nick);
		return 0;
	}

	if(hash_find_nd(nick))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_UNAVAILRESOURCE),
			me.name, 
			EmptyString(source_p->name) ? "*" : source_p->name,
			nick);
		return 0;
	}

	if((target_p = find_client(nick)) == NULL)
		set_initial_nick(client_p, source_p, nick);
	else if(source_p == target_p)
		strcpy(source_p->name, nick);
	else
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NICKNAMEINUSE), me.name, "*", nick);

	return 0;
}

/* m_nick()
 *     parv[0] = sender prefix
 *     parv[1] = nickname
 */
static int
m_nick(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	char nick[NICKLEN];
	char *s;

	if(parc < 2 || EmptyString(parv[1]) || (parv[1][0] == '~'))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NONICKNAMEGIVEN),
			   me.name, source_p->name);
		return 0;
	}

	/* due to the scandinavian origins, (~ being uppercase of ^) and ~
	 * being disallowed as a nick char, we need to chop the first ~
	 * instead of just erroring.
	 */
	if((s = strchr(parv[1], '~')))
		*s = '\0';

	/* mark end of grace period, to prevent nickflooding */
	if(!IsFloodDone(source_p))
		flood_endgrace(source_p);

	/* terminate nick to NICKLEN, we dont want clean_nick() to error! */
	strlcpy(nick, parv[1], sizeof(nick));

	/* check the nickname is ok */
	if(!clean_nick(nick, 1))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_ERRONEUSNICKNAME),
			   me.name, parv[0], nick);
		return 0;
	}

	if(find_nick_resv(nick))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_ERRONEUSNICKNAME),
			   me.name, source_p->name, nick);
		return 0;
	}

	if(hash_find_nd(nick))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_UNAVAILRESOURCE),
			me.name, 
			EmptyString(source_p->name) ? "*" : source_p->name,
			nick);
		return 0;
	}

	if((target_p = find_client(nick)))
	{
		/* If(target_p == source_p) the client is changing nicks between
		 * equivalent nicknames ie: [nick] -> {nick}
		 */
		if(target_p == source_p)
		{
			/* check the nick isnt exactly the same */
			if(strcmp(target_p->name, nick))
				change_local_nick(client_p, source_p, nick);

		}

		/* drop unregged client */
		else if(IsUnknown(target_p))
		{
			exit_client(NULL, target_p, &me, "Overridden");
			change_local_nick(client_p, source_p, nick);
		}
		else
			sendto_one(source_p, POP_QUEUE, form_str(ERR_NICKNAMEINUSE), me.name, parv[0], nick);

		return 0;
	}
	else
		change_local_nick(client_p, source_p, nick);

	return 0;
}

/* ms_nick()
 *      
 * server -> server nick change
 *    parv[0] = sender prefix
 *    parv[1] = nickname
 *    parv[2] = TS when nick change
 *
 * server introducing new nick
 *    parv[0] = sender prefix
 *    parv[1] = nickname
 *    parv[2] = hop count
 *    parv[3] = TS
 *    parv[4] = umode
 *    parv[5] = username
 *    parv[6] = hostname
 *    parv[7] = server
 *    parv[8] = ircname
 */
static int
mc_nick(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	time_t newts = 0;

	/* if nicks erroneous, or too long, kill */
	if(!clean_nick(parv[1], 0))
	{
		ServerStats.is_kill++;
		sendto_realops_flags(UMODE_DEBUG, L_ALL,
				     "Bad Nick: %s From: %s(via %s)",
				     parv[1], source_p->user->server,
				     client_p->name);
		sendto_one(client_p, POP_QUEUE, ":%s KILL %s :%s (Bad Nickname)",
			   me.name, parv[1], me.name);

		/* bad nick change, issue kill for the old nick to the rest
		 * of the network.
		 */
		kill_client_serv_butone(client_p, source_p,
					"%s (Bad Nickname)", me.name);
		source_p->flags |= FLAGS_KILLED;
		exit_client(client_p, source_p, &me, "Bad Nickname");
		return 0;
	}
			      
	newts = atol(parv[2]);
	target_p = find_client(parv[1]);

	/* if the nick doesnt exist, allow it and process like normal */
	if(target_p == NULL)
	{
		change_remote_nick(client_p, source_p, parc, parv, newts, parv[1]);
	}
	else if(IsUnknown(target_p))
	{
		exit_client(NULL, target_p, &me, "Overridden");
		change_remote_nick(client_p, source_p, parc, parv, newts, parv[1]);
	}
	else if(target_p == source_p)
	{
		/* client changing case of nick */
		if(strcmp(target_p->name, parv[1]))
			change_remote_nick(client_p, source_p, parc, parv, newts, parv[1]);
	}
	/* we've got a collision! */
	else
		perform_nickchange_collides(source_p, client_p, target_p, 
					    parc, parv, newts, parv[1]);

	return 0;
}

static int
ms_nick(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	time_t newts = 0;

	if(parc < 9)
		return 0;

	/* if nicks empty, erroneous, or too long, kill */
	if(!clean_nick(parv[1], 0))
	{
		ServerStats.is_kill++;
		sendto_realops_flags(UMODE_DEBUG, L_ALL,
				     "Bad Nick: %s From: %s(via %s)",
				     parv[1], parv[7], client_p->name);
		sendto_one(client_p, POP_QUEUE, ":%s KILL %s :%s (Bad Nickname)",
			   me.name, parv[1], me.name);
		return 0;
	}
			      
	/* invalid username or host? */
	if(!clean_username(parv[5]) || !clean_host(parv[6]))
	{
		ServerStats.is_kill++;
		sendto_realops_flags(UMODE_DEBUG, L_ALL,
				"Bad user@host: %s@%s From: %s(via %s)",
				parv[5], parv[6], parv[7],
				client_p->name);
		sendto_one(client_p, POP_QUEUE, ":%s KILL %s :%s (Bad user@host)",
				me.name, parv[1], me.name);
		return 0;
	}

	/* check the length of the clients gecos */
	if(strlen(parv[8]) > REALLEN)
	{
		char *s = LOCAL_COPY(parv[8]);
		/* why exactly do we care? --fl */
		sendto_realops_flags(UMODE_ALL, L_ALL,
				"Long realname from server %s for %s", parv[7],
				parv[1]);

		s[REALLEN] = '\0';
		parv[8] = s;
	}

	newts = atol(parv[3]);

	target_p = find_client(parv[1]);

	/* if the nick doesnt exist, allow it and process like normal */
	if(target_p == NULL)
	{
		register_client(client_p, NULL, parv[1], newts, parc, parv);
	}
	else if(IsUnknown(target_p))
	{
		exit_client(NULL, target_p, &me, "Overridden");
		register_client(client_p, NULL, parv[1], newts, parc, parv);
	}
	else if(target_p == source_p)
	{
		/* client changing case of nick */
		if(strcmp(target_p->name, parv[1]))
			register_client(client_p, NULL, parv[1], newts, parc, parv);
	}
	/* we've got a collision! */
	else
		perform_nick_collides(source_p, client_p, target_p, parc, parv, 
					newts, parv[1], NULL);

	return 0;
}

/* ms_uid()
 *     parv[1] - nickname
 *     parv[2] - hops
 *     parv[3] - TS
 *     parv[4] - umodes
 *     parv[5] - username
 *     parv[6] - hostname
 *     parv[7] - IP
 *     parv[8] - UID
 *     parv[9] - gecos
 */
static int
ms_uid(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	time_t newts = 0;

	newts = atol(parv[3]);

	if(parc < 10)
		return 0;

	/* if nicks erroneous, or too long, kill */
	if(!clean_nick(parv[1], 0))
	{
		ServerStats.is_kill++;
		sendto_realops_flags(UMODE_DEBUG, L_ALL,
				     "Bad Nick: %s From: %s(via %s)",
				     parv[1], source_p->name,
				     client_p->name);
		sendto_one(client_p, POP_QUEUE, ":%s KILL %s :%s (Bad Nickname)",
			   me.id, parv[8], me.name);
		return 0;
	}

	if(!clean_username(parv[5]) || !clean_host(parv[6]))
	{
		ServerStats.is_kill++;
		sendto_realops_flags(UMODE_DEBUG, L_ALL,
				     "Bad user@host: %s@%s From: %s(via %s)",
				     parv[5], parv[6], source_p->name,
				     client_p->name);
		sendto_one(client_p, POP_QUEUE, ":%s KILL %s :%s (Bad user@host)",
			   me.id, parv[8], me.name);
		return 0;
	}

	if(!clean_uid(parv[8]))
	{
		ServerStats.is_kill++;
		sendto_realops_flags(UMODE_DEBUG, L_ALL,
					"Bad UID: %s From: %s(via %s)",
					parv[8], source_p->name,
					client_p->name);
		sendto_one(client_p, POP_QUEUE, ":%s KILL %s :%s (Bad UID)",
				me.id, parv[8], me.name);
		return 0;
	}

	/* check length of clients gecos */
	if(strlen(parv[9]) > REALLEN)
	{
		char *s = LOCAL_COPY(parv[9]);
		sendto_realops_flags(UMODE_ALL, L_ALL, "Long realname from server %s for %s",
				     parv[0], parv[1]);
		s[REALLEN] = '\0';
		parv[9] = s;
	}

	target_p = find_client(parv[1]);
	
	if(target_p == NULL)
	{
		register_client(client_p, source_p, parv[1], newts, parc, parv);
	}
	else if(IsUnknown(target_p))
	{
		exit_client(NULL, target_p, &me, "Overridden");
		register_client(client_p, source_p, parv[1], newts, parc, parv);
	}
	/* we've got a collision! */
	else
		perform_nick_collides(source_p, client_p, target_p, parc, parv,
					newts, parv[1], parv[8]);

	return 0;
}

/* clean_nick()
 *
 * input	- nickname to check
 * output	- 0 if erroneous, else 1
 * side effects - 
 */
static int
clean_nick(const char *nick, int loc_client)
{
	int len = 0;

	/* nicks cant start with a digit or -, and must have a length */
	if(*nick == '-' || *nick == '\0')
		return 0;

	if(loc_client && IsDigit(*nick))
		return 0;

	for (; *nick; nick++)
	{
		len++;
		if(!IsNickChar(*nick))
			return 0;
	}

	/* nicklen is +1 */
	if(len >= NICKLEN)
		return 0;

	return 1;
}

/* clean_username()
 *
 * input	- username to check
 * output	- 0 if erroneous, else 0
 * side effects -
 */
static int
clean_username(const char *username)
{
	int len = 0;

	for (; *username; username++)
	{
		len++;

		if(!IsUserChar(*username))
			return 0;
	}

	if(len > USERLEN)
		return 0;

	return 1;
}

/* clean_host()
 *
 * input	- host to check
 * output	- 0 if erroneous, else 0
 * side effects -
 */
static int
clean_host(const char *host)
{
	int len = 0;

	for (; *host; host++)
	{
		len++;

		if(!IsHostChar(*host))
			return 0;
	}

	if(len > HOSTLEN)
		return 0;

	return 1;
}

static int
clean_uid(const char *uid)
{
	int len = 1;

	if(!IsDigit(*uid++))
		return 0;

	for(; *uid; uid++)
	{
		len++;

		if(!IsIdChar(*uid))
			return 0;
	}

	if(len != IDLEN-1)
		return 0;

	return 1;
}

static void
set_initial_nick(struct Client *client_p, struct Client *source_p, char *nick)
{
	char buf[USERLEN + 1];

	/* This had to be copied here to avoid problems.. */
	source_p->tsinfo = CurrentTime;
	if(source_p->name[0])
		del_from_client_hash(source_p->name, source_p);

	strcpy(source_p->name, nick);
	add_to_client_hash(nick, source_p);

	/* fd_desc is long enough */
	comm_note(client_p->localClient->fd, "Nick: %s", nick);

	if(source_p->user)
	{
		strlcpy(buf, source_p->username, sizeof(buf));

		/* got user, heres nick. */
		register_local_user(client_p, source_p, buf);

	}
}

static void
change_local_nick(struct Client *client_p, struct Client *source_p, char *nick)
{
	int samenick;
	if((source_p->localClient->last_nick_change + ConfigFileEntry.max_nick_time) < CurrentTime)
		source_p->localClient->number_of_nick_changes = 0;
	source_p->localClient->last_nick_change = CurrentTime;
	source_p->localClient->number_of_nick_changes++;

	if(ConfigFileEntry.anti_nick_flood && !IsOper(source_p) &&
	   source_p->localClient->number_of_nick_changes > ConfigFileEntry.max_nick_changes)
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NICKTOOFAST), 
				me.name, source_p->name, source_p->name, 
				nick, ConfigFileEntry.max_nick_time);
		return;
	}

        samenick = irccmp(source_p->name, nick) ? 0 : 1;

        /* dont reset TS if theyre just changing case of nick */
        if(!samenick)
	{
                source_p->tsinfo = CurrentTime;
		monitor_signoff(source_p);
	}

	sendto_realops_flags(UMODE_NCHANGE, L_ALL,
			"Nick change: From %s to %s [%s@%s]",
			source_p->name, nick, source_p->username, source_p->host);

	/* send the nick change to the users channels */
	sendto_common_channels_local(source_p, ":%s!%s@%s NICK :%s",
				source_p->name,	source_p->username, 
				source_p->host, nick);

	/* send the nick change to servers.. */
	if(source_p->user)
	{
		add_history(source_p, 1);

		sendto_server(client_p, NULL, CAP_TS6, NOCAPS, ":%s NICK %s :%ld",
				use_id(source_p), nick, (long) source_p->tsinfo);
		sendto_server(client_p, NULL, NOCAPS, CAP_TS6, ":%s NICK %s :%ld",
				source_p->name, nick, (long) source_p->tsinfo);
	}

	/* Finally, add to hash */
	del_from_client_hash(source_p->name, source_p);
	strcpy(source_p->name, nick);
	add_to_client_hash(nick, source_p);

	if(!samenick)
		monitor_signon(source_p);

	/* Make sure everyone that has this client on its accept list
	 * loses that reference. 
	 */
	del_all_accepts(source_p);

	/* fd_desc is long enough */
	comm_note(client_p->localClient->fd, "Nick: %s", nick);

	return;
}

/*
 * change_remote_nick()
 */
static int
change_remote_nick(struct Client *client_p, struct Client *source_p, int parc,
		 const char *parv[], time_t newts, const char *nick)
{
	struct nd_entry *nd;
	int samenick = irccmp(source_p->name, nick) ? 0 : 1;

	/* client changing their nick - dont reset ts if its same */
	if(!samenick)
	{
		source_p->tsinfo = newts ? newts : CurrentTime;
		monitor_signoff(source_p);
	}

	sendto_common_channels_local(source_p, ":%s!%s@%s NICK :%s",
				     source_p->name, source_p->username,
				     source_p->host, nick);

	if(source_p->user)
	{
		add_history(source_p, 1);
		sendto_server(client_p, NULL, NOCAPS, NOCAPS, ":%s NICK %s :%ld",
			      parv[0], nick, (long) source_p->tsinfo);
	}

	del_from_client_hash(source_p->name, source_p);

	/* invalidate nick delay when a remote client uses the nick.. */
	if((nd = hash_find_nd(nick)))
		free_nd_entry(nd);

	strcpy(source_p->name, nick);
	add_to_client_hash(nick, source_p);

	if(!samenick)
		monitor_signon(source_p);

	/* remove all accepts pointing to the client */
	del_all_accepts(source_p);

	return 0;
}

static int
perform_nick_collides(struct Client *source_p, struct Client *client_p,
		      struct Client *target_p, int parc, const char *parv[],
		      time_t newts, const char *nick, const char *uid)
{
	int sameuser;

	/* if we dont have a ts, or their TS's are the same, kill both */
	if(!newts || !target_p->tsinfo || (newts == target_p->tsinfo))
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Nick collision on %s(%s <- %s)(both killed)",
				     target_p->name, target_p->from->name, client_p->name);

		sendto_one_numeric(target_p, POP_QUEUE, ERR_NICKCOLLISION,
				   form_str(ERR_NICKCOLLISION), target_p->name);

		/* if the new client being introduced has a UID, we need to
		 * issue a KILL for it..
		 */
		if(uid)
			sendto_one(client_p, POP_QUEUE, ":%s KILL %s :%s (Nick collision (new))",
					me.id, uid, me.name);

		/* we then need to KILL the old client everywhere */
		kill_client_serv_butone(NULL, target_p,
					"%s (Nick collision (new))", me.name);
		ServerStats.is_kill++;

		target_p->flags |= FLAGS_KILLED;
		exit_client(client_p, target_p, &me, "Nick collision (new)");
		return 0;
	}
	/* the timestamps are different */
	else
	{
		sameuser = (target_p->user) && !irccmp(target_p->username, parv[5])
				&& !irccmp(target_p->host, parv[6]);

		if((sameuser && newts < target_p->tsinfo) ||
		   (!sameuser && newts > target_p->tsinfo))
		{
			/* if we have a UID, then we need to issue a KILL,
			 * otherwise we do nothing and hope that the other
			 * client will collide it..
			 */
			if(uid)
				sendto_one(client_p, POP_QUEUE,
					":%s KILL %s :%s (Nick collision (new))",
					me.id, uid, me.name);
			return 0;
		}
		else
		{
			if(sameuser)
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Nick collision on %s(%s <- %s)(older killed)",
						     target_p->name, target_p->from->name,
						     client_p->name);
			else
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Nick collision on %s(%s <- %s)(newer killed)",
						     target_p->name, target_p->from->name,
						     client_p->name);

			ServerStats.is_kill++;
			sendto_one_numeric(target_p, POP_QUEUE, ERR_NICKCOLLISION,
					   form_str(ERR_NICKCOLLISION), target_p->name);

			/* now we just need to kill the existing client */
			kill_client_serv_butone(client_p, target_p,
						"%s (Nick collision (new))", me.name);

			target_p->flags |= FLAGS_KILLED;
			(void) exit_client(client_p, target_p, &me, "Nick collision");

			register_client(client_p, parc == 10 ? source_p : NULL,
					nick, newts, parc, parv);

			return 0;
		}
	}
}


static int
perform_nickchange_collides(struct Client *source_p, struct Client *client_p,
			    struct Client *target_p, int parc, 
			    const char *parv[], time_t newts, const char *nick)
{
	int sameuser;

	/* its a client changing nick and causing a collide */
	if(!newts || !target_p->tsinfo || (newts == target_p->tsinfo) || !source_p->user)
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Nick change collision from %s to %s(%s <- %s)(both killed)",
				     source_p->name, target_p->name, target_p->from->name,
				     client_p->name);

		ServerStats.is_kill++;
		sendto_one_numeric(target_p, POP_QUEUE, ERR_NICKCOLLISION,
				   form_str(ERR_NICKCOLLISION), target_p->name);

		kill_client_serv_butone(NULL, source_p, "%s (Nick change collision)", me.name);

		ServerStats.is_kill++;

		kill_client_serv_butone(NULL, target_p, "%s (Nick change collision)", me.name);

		target_p->flags |= FLAGS_KILLED;
		exit_client(NULL, target_p, &me, "Nick collision(new)");
		source_p->flags |= FLAGS_KILLED;
		exit_client(client_p, source_p, &me, "Nick collision(old)");
		return 0;
	}
	else
	{
		sameuser = !irccmp(target_p->username, source_p->username) &&
			!irccmp(target_p->host, source_p->host);

		if((sameuser && newts < target_p->tsinfo) ||
		   (!sameuser && newts > target_p->tsinfo))
		{
			if(sameuser)
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Nick change collision from %s to %s(%s <- %s)(older killed)",
						     source_p->name, target_p->name,
						     target_p->from->name, client_p->name);
			else
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Nick change collision from %s to %s(%s <- %s)(newer killed)",
						     source_p->name, target_p->name,
						     target_p->from->name, client_p->name);

			ServerStats.is_kill++;

			sendto_one_numeric(target_p, POP_QUEUE, ERR_NICKCOLLISION,
					   form_str(ERR_NICKCOLLISION), target_p->name);

			/* kill the client issuing the nickchange */
			kill_client_serv_butone(client_p, source_p,
						"%s (Nick change collision)", me.name);

			source_p->flags |= FLAGS_KILLED;

			if(sameuser)
				exit_client(client_p, source_p, &me, "Nick collision(old)");
			else
				exit_client(client_p, source_p, &me, "Nick collision(new)");
			return 0;
		}
		else
		{
			if(sameuser)
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Nick collision on %s(%s <- %s)(older killed)",
						     target_p->name, target_p->from->name,
						     client_p->name);
			else
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Nick collision on %s(%s <- %s)(newer killed)",
						     target_p->name, target_p->from->name,
						     client_p->name);

			sendto_one_numeric(target_p, POP_QUEUE, ERR_NICKCOLLISION,
					   form_str(ERR_NICKCOLLISION), target_p->name);

			/* kill the client who existed before hand */
			kill_client_serv_butone(client_p, target_p, 
					"%s (Nick collision)", me.name);

			ServerStats.is_kill++;

			target_p->flags |= FLAGS_KILLED;
			(void) exit_client(client_p, target_p, &me, "Nick collision");
		}
	}

	change_remote_nick(client_p, source_p, parc, parv, newts, nick);

	return 0;
}

static int
register_client(struct Client *client_p, struct Client *server, 
		const char *nick, time_t newts, int parc, const char *parv[])
{
	struct Client *source_p;
	struct User *user;
	struct nd_entry *nd;
	const char *m;
	int flag;

	source_p = make_client(client_p);
	user = make_user(source_p);
	dlinkAddTail(source_p, &source_p->node, &global_client_list);

	source_p->hopcount = atoi(parv[2]);
	source_p->tsinfo = newts;

	strcpy(source_p->name, nick);
	strlcpy(source_p->username, parv[5], sizeof(source_p->username));
	strlcpy(source_p->host, parv[6], sizeof(source_p->host));
	
	if(parc == 10)
	{
		user->server = find_or_add(server->name);
		strlcpy(source_p->info, parv[9], sizeof(source_p->info));
		strlcpy(source_p->sockhost, parv[7], sizeof(source_p->sockhost));
		strlcpy(source_p->id, parv[8], sizeof(source_p->id));
		add_to_id_hash(source_p->id, source_p);
	}
	else
	{
		user->server = find_or_add(parv[7]);
		strlcpy(source_p->info, parv[8], sizeof(source_p->info));
	}

	/* remove any nd entries for this nick */
	if((nd = hash_find_nd(nick)))
		free_nd_entry(nd);

	add_to_client_hash(nick, source_p);
	add_to_hostname_hash(source_p->host, source_p);
	monitor_signon(source_p);

	m = &parv[4][1];
	while(*m)
	{
		flag = user_modes_from_c_to_bitmask[(unsigned char) *m];

#ifdef ENABLE_SERVICES
		if(flag & UMODE_SERVICE)
		{
			int hit = 0;
			dlink_node *ptr;

			DLINK_FOREACH(ptr, service_list.head)
			{
				if(!irccmp((const char *) ptr->data, user->server))
				{
					hit++;
					break;
				}
			}

			if(!hit)
			{
				m++;
				continue;
			}
		}
#endif

		/* increment +i count if theyre invis */
		if(!(source_p->umodes & UMODE_INVISIBLE) &&
		   (flag & UMODE_INVISIBLE))
			Count.invisi++;

		/* increment opered count if theyre opered */
		if(!(source_p->umodes & UMODE_OPER) && (flag & UMODE_OPER))
			Count.oper++;

		source_p->umodes |= (flag & SEND_UMODES);
		m++;
	}

	SetRemoteClient(source_p);

	if(++Count.total > Count.max_tot)
		Count.max_tot = Count.total;
	
	if(server == NULL)
	{
		if((source_p->servptr = find_server(NULL, user->server)) == NULL)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Ghost killed: %s on invalid server %s",
					     source_p->name, user->server);
			kill_client(client_p, source_p, "%s (Server doesn't exist)", me.name);
			source_p->flags |= FLAGS_KILLED;
			return exit_client(NULL, source_p, &me, "Ghosted Client");
		}
	}
	else
		source_p->servptr = server;

	dlinkAdd(source_p, &source_p->lnode, &source_p->servptr->serv->users);

	/* fake direction */
	if(source_p->servptr->from != source_p->from)
	{
		struct Client *target_p = source_p->servptr->from;

		sendto_realops_flags(UMODE_DEBUG, L_ALL,
				     "Bad User [%s] :%s USER %s@%s %s, != %s[%s]",
				     client_p->name, source_p->name,
				     source_p->username, source_p->host,
				     user->server, target_p->name,
				     target_p->from->name);
		kill_client(client_p, source_p,
			    "%s (NICK from wrong direction (%s != %s))",
			    me.name, user->server, target_p->from->name);
		source_p->flags |= FLAGS_KILLED;
		return exit_client(source_p, source_p, &me, "USER server wrong direction");
	}

	return (introduce_client(client_p, source_p, user, nick));
}

