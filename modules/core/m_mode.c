/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_mode.c: Sets a user or channel mode.
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
#include "balloc.h"
#include "handlers.h"
#include "channel.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_user.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "packet.h"
#include "sprintf_irc.h"

static int m_mode(struct Client *, struct Client *, int, const char **);

struct Message mode_msgtab = {
	"MODE", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_mode, m_mode, m_mode}
};

mapi_clist_av1 mode_clist[] = { &mode_msgtab, NULL };
DECLARE_MODULE_AV1(mode, NULL, NULL, mode_clist, NULL, NULL, NULL, "$Revision$");

/* bitmasks for error returns, so we send once per call */
#define SM_ERR_NOTS             0x00000001	/* No TS on channel */
#define SM_ERR_NOOPS            0x00000002	/* No chan ops */
#define SM_ERR_UNKNOWN          0x00000004
#define SM_ERR_RPL_C            0x00000008
#define SM_ERR_RPL_B            0x00000010
#define SM_ERR_RPL_E            0x00000020
#define SM_ERR_NOTONCHANNEL     0x00000040	/* Not on channel */
#define SM_ERR_RESTRICTED       0x00000080	/* Restricted chanop */
#define SM_ERR_RPL_I            0x00000100
#define SM_ERR_RPL_D            0x00000200

static void set_channel_mode(struct Client *, struct Client *,
			     struct Channel *, struct membership *,
			     int, const char **);

static struct ChModeChange mode_changes[BUFSIZE];
static int mode_count;
static int mode_limit;
static int mask_pos;

/* channel.c */
extern BlockHeap *ban_heap;

/*
 * m_mode - MODE command handler
 * parv[0] - sender
 * parv[1] - channel
 */
static int
m_mode(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr = NULL;
	struct membership *msptr;
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	int n = 2;

	if(EmptyString(parv[1]))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
			   me.name, parv[0], "MODE");
		return 0;
	}

	/* Now, try to find the channel in question */
	if(!IsChanPrefix(parv[1][0]))
	{
		/* if here, it has to be a non-channel name */
		user_mode(client_p, source_p, parc, parv);
		return 0;
	}

	if(!check_channel_name(parv[1]))
	{
		sendto_one(source_p, form_str(ERR_BADCHANNAME),
			   me.name, parv[0], (unsigned char *) parv[1]);
		return 0;
	}

	chptr = find_channel(parv[1]);

	if(chptr == NULL)
	{
		sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0], parv[1]);
		return 0;
	}

	/* Now know the channel exists */
	if(parc < n + 1)
	{
		channel_modes(chptr, source_p, modebuf, parabuf);
		sendto_one(source_p, form_str(RPL_CHANNELMODEIS),
			   me.name, parv[0], parv[1], modebuf, parabuf);

		sendto_one(source_p, form_str(RPL_CREATIONTIME),
			   me.name, parv[0], parv[1], chptr->channelts);
	}
	else if(IsServer(source_p))
	{
		set_channel_mode(client_p, source_p, chptr, NULL,
				 parc - n, parv + n);
	}
	else
	{
		msptr = find_channel_membership(chptr, source_p);

		if(is_deop(msptr))
			return 0;

		/* Finish the flood grace period... */
		if(MyClient(source_p) && !IsFloodDone(source_p))
		{
			if(!((parc == 3) && (parv[2][0] == 'b') && (parv[2][1] == '\0')))
				flood_endgrace(source_p);
		}

		set_channel_mode(client_p, source_p, chptr, msptr, 
			         parc - n, parv + n);
	}

	return 0;
}


/* add_id()
 *
 * inputs	- client, channel, id to add, type
 * outputs	- 0 on failure, 1 on success
 * side effects - given id is added to the appropriate list
 */
static int
add_id(struct Client *source_p, struct Channel *chptr, const char *banid,
       dlink_list *list, long mode_type)
{
	struct Ban *actualBan;
	char *realban = LOCAL_COPY(banid);
	dlink_node *ptr;

	/* dont let local clients overflow the banlist */
	if(MyClient(source_p))
	{
		if(chptr->num_mask >= ConfigChannel.max_bans)
		{
			sendto_one(source_p, form_str(ERR_BANLISTFULL),
				   me.name, source_p->name, chptr->chname, realban);
			return 0;
		}

		collapse(realban);
	}

	DLINK_FOREACH(ptr, list->head)
	{
		actualBan = ptr->data;
		if(match(actualBan->banstr, realban))
			return 0;
	}


	actualBan = (struct Ban *) BlockHeapAlloc(ban_heap);
	memset(actualBan, 0, sizeof(struct Ban));

	strlcpy(actualBan->banstr, realban, sizeof(actualBan->banstr));
	actualBan->when = CurrentTime;

	if(IsPerson(source_p))
		ircsprintf(actualBan->who, "%s!%s@%s",
			   source_p->name, source_p->username, source_p->host);
	else
		strlcpy(actualBan->who, source_p->name, sizeof(actualBan->who));

	dlinkAdd(actualBan, &actualBan->node, list);
	chptr->num_mask++;

	/* invalidate the can_send() cache */
	if(mode_type == CHFL_BAN || mode_type == CHFL_EXCEPTION)
		chptr->bants++;

	return 1;
}

/* del_id()
 *
 * inputs	- channel, id to remove, type
 * outputs	- 0 on failure, 1 on success
 * side effects - given id is removed from the appropriate list
 */
static int
del_id(struct Channel *chptr, const char *banid, dlink_list *list,
       long mode_type)
{
	dlink_node *ptr;
	struct Ban *banptr;

	if(EmptyString(banid))
		return 0;

	DLINK_FOREACH(ptr, list->head)
	{
		banptr = ptr->data;

		if(irccmp(banid, banptr->banstr) == 0)
		{
			dlinkDelete(&banptr->node, list);
			BlockHeapFree(ban_heap, banptr);

			/* num_mask should never be < 0 */
			if(chptr->num_mask > 0)
				chptr->num_mask--;
			else
				chptr->num_mask = 0;

			/* invalidate the can_send() cache */
			if(mode_type == CHFL_BAN || mode_type == CHFL_EXCEPTION)
				chptr->bants++;

			return 1;
		}
	}

	return 0;
}

/* check_string()
 *
 * input	- string to check
 * output	- pointer to 'fixed' string, or "*" if empty
 * side effects - any white space found becomes \0
 */
static char *
check_string(char *s)
{
	char *str = s;
	static char splat[] = "*";
	if(!(s && *s))
		return splat;

	for (; *s; ++s)
	{
		if(IsSpace(*s))
		{
			*s = '\0';
			break;
		}
	}
	return str;
}

/* pretty_mask()
 *
 * inputs	- mask to pretty
 * outputs	- better version of the mask
 * side effects - mask is chopped to limits, and transformed:
 *                x!y@z => x!y@z
 *                y@z   => *!y@z
 *                x!y   => x!y@*
 *                x     => x!*@*
 *                z.d   => *!*@z.d
 */
static char *
pretty_mask(const char *idmask)
{
	static char mask_buf[BUFSIZE];
	int old_mask_pos;
	char *nick, *user, *host;
	char splat[] = "*";
	char *t, *at, *ex;
	char ne = 0, ue = 0, he = 0;	/* save values at nick[NICKLEN], et all */
	char *mask;

	mask = LOCAL_COPY(idmask);
	mask = check_string(mask);

	nick = user = host = splat;

	if((size_t)BUFSIZE - mask_pos < strlen(mask) + 5)
		return NULL;

	old_mask_pos = mask_pos;

	at = ex = NULL;
	if((t = strchr(mask, '@')) != NULL)
	{
		at = t;
		*t++ = '\0';
		if(*t != '\0')
			host = t;

		if((t = strchr(mask, '!')) != NULL)
		{
			ex = t;
			*t++ = '\0';
			if(*t != '\0')
				user = t;
			if(*mask != '\0')
				nick = mask;
		}
		else
		{
			if(*mask != '\0')
				user = mask;
		}
	}
	else if((t = strchr(mask, '!')) != NULL)
	{
		ex = t;
		*t++ = '\0';
		if(*mask != '\0')
			nick = mask;
		if(*t != '\0')
			user = t;
	}
	else if(strchr(mask, '.') != NULL && strchr(mask, ':') != NULL)
	{
		if(*mask != '\0')
			host = mask;
	}
	else
	{
		if(*mask != '\0')
			nick = mask;
	}

	/* truncate values to max lengths */
	if(strlen(nick) > NICKLEN - 1)
	{
		ne = nick[NICKLEN - 1];
		nick[NICKLEN - 1] = '\0';
	}
	if(strlen(user) > USERLEN)
	{
		ue = user[USERLEN];
		user[USERLEN] = '\0';
	}
	if(strlen(host) > HOSTLEN)
	{
		he = host[HOSTLEN];
		host[HOSTLEN] = '\0';
	}

	mask_pos += ircsprintf(mask_buf + mask_pos, "%s!%s@%s", nick, user, host) + 1;

	/* restore mask, since we may need to use it again later */
	if(at)
		*at = '@';
	if(ex)
		*ex = '!';
	if(ne)
		nick[NICKLEN - 1] = ne;
	if(ue)
		user[USERLEN] = ue;
	if(he)
		host[HOSTLEN] = he;

	return mask_buf + old_mask_pos;
}

/* fix_key()
 *
 * input	- key to fix
 * output	- the same key, fixed
 * side effects - anything below ascii 13 is discarded, ':' discarded,
 *                high ascii is dropped to lower half of ascii table
 */
static char *
fix_key(char *arg)
{
	u_char *s, *t, c;

	for (s = t = (u_char *) arg; (c = *s); s++)
	{
		c &= 0x7f;
		if(c != ':' && c > ' ')
			*t++ = c;
	}

	*t = '\0';
	return arg;
}

/* fix_key_remote()
 *
 * input	- key to fix
 * ouput	- the same key, fixed
 * side effects - high ascii dropped to lower half of table,
 *                CR/LF/':' are dropped
 */
static char *
fix_key_remote(char *arg)
{
	u_char *s, *t, c;

	for (s = t = (u_char *) arg; (c = *s); s++)
	{
		c &= 0x7f;
		if((c != 0x0a) && (c != ':') && (c != 0x0d))
			*t++ = c;
	}

	*t = '\0';
	return arg;
}

/* chm_*()
 *
 * The handlers for each specific mode.
 */
static void
chm_nosuch(struct Client *source_p, struct Channel *chptr, int parc, int *parn,
	   const char **parv, int *errors, int alev, int dir, char c, long mode_type)
{
	if(*errors & SM_ERR_UNKNOWN)
		return;
	*errors |= SM_ERR_UNKNOWN;
	sendto_one(source_p, form_str(ERR_UNKNOWNMODE), me.name, source_p->name, c);
}

static void
chm_simple(struct Client *source_p,struct Channel *chptr, int parc, int *parn,
	   const char **parv, int *errors, int alev, int dir, char c, long mode_type)
{
	if(alev < CHACCESS_CHANOP)
	{
		if(!(*errors & SM_ERR_NOOPS))
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, source_p->name, chptr->chname);
		*errors |= SM_ERR_NOOPS;
		return;
	}

	/* +ntspmaikl == 9 + MAXMODEPARAMS (4 * +o) */
	if(MyClient(source_p) && (++mode_limit > (9 + MAXMODEPARAMS)))
		return;

	/* setting + */
	if((dir == MODE_ADD) && !(chptr->mode.mode & mode_type))
	{
		chptr->mode.mode |= mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count++].arg = NULL;
	}
	else if((dir == MODE_DEL) && (chptr->mode.mode & mode_type))
	{
		/* setting - */

		chptr->mode.mode &= ~mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

static void
chm_ban(struct Client *source_p, struct Channel *chptr, int parc, int *parn,
	const char **parv, int *errors, int alev, int dir, char c, long mode_type)
{
	const char *mask;
	const char *raw_mask;
	dlink_list *list;
	dlink_node *ptr;
	struct Ban *banptr;
	int errorval;
	int rpl_list;
	int rpl_endlist;
	int caps;

	switch(mode_type)
	{
		case CHFL_BAN:
			list = &chptr->banlist;
			errorval = SM_ERR_RPL_B;
			rpl_list = RPL_BANLIST;
			rpl_endlist = RPL_ENDOFBANLIST;
			caps = 0;
			break;

		case CHFL_EXCEPTION:
			/* if +e is disabled, allow all but +e locally */
			if(!ConfigChannel.use_except && MyClient(source_p) &&
			   ((dir == MODE_ADD) && (parc > *parn)))
				return;

			list = &chptr->exceptlist;
			errorval = SM_ERR_RPL_E;
			rpl_list = RPL_EXCEPTLIST;
			rpl_endlist = RPL_ENDOFEXCEPTLIST;
			caps = CAP_EX;
			break;

		case CHFL_INVEX:
			/* if +I is disabled, allow all but +I locally */
			if(!ConfigChannel.use_invex && MyClient(source_p) &&
					(dir == MODE_ADD) && (parc > *parn))
				return;

			list = &chptr->invexlist;
			errorval = SM_ERR_RPL_I;
			rpl_list = RPL_INVITELIST;
			rpl_endlist = RPL_ENDOFINVITELIST;
			caps = CAP_IE;
			break;

		default:
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "chm_ban() called with unknown type!");
			return;
			break;
	}

	if(dir == 0 || parc <= *parn)
	{
		if((*errors & errorval) != 0)
			return;
		*errors |= errorval;

		/* non-ops cant see +eI lists.. */
		if(alev < CHACCESS_CHANOP && mode_type != CHFL_BAN)
		{
			if(!(*errors & SM_ERR_NOOPS))
				sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
					   me.name, source_p->name, chptr->chname);
			*errors |= SM_ERR_NOOPS;
			return;
		}

		DLINK_FOREACH(ptr, list->head)
		{
			banptr = ptr->data;
			sendto_one(source_p, form_str(rpl_list),
				   me.name, source_p->name, chptr->chname,
				   banptr->banstr, banptr->who, banptr->when);
		}
		sendto_one(source_p, form_str(rpl_endlist),
			   me.name, source_p->name, chptr->chname);
		return;
	}

	if(alev < CHACCESS_CHANOP)
	{
		if(!(*errors & SM_ERR_NOOPS))
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, source_p->name, chptr->chname);
		*errors |= SM_ERR_NOOPS;
		return;
	}

	if(MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
		return;

	raw_mask = parv[(*parn)];
	(*parn)++;

	/* empty ban, ignore it */
	if(EmptyString(raw_mask))
		return;

	if(!MyClient(source_p))
		mask = raw_mask;
	else
		mask = pretty_mask(raw_mask);

	/* we'd have problems parsing this, hyb6 does it too */
	if(strlen(mask) > (MODEBUFLEN - 2))
		return;

	/* if we're adding a NEW id */
	if(dir == MODE_ADD)
	{
		/* dont allow local clients to overflow the banlist, and dont
		 * let servers do redundant +b's, as it wastes bandwidth on
		 * a netjoin.
		 */
		if(!add_id(source_p, chptr, mask, list, mode_type) &&
		   (MyClient(source_p) || IsServer(source_p)))
			return;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].caps = caps;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = mask;
	}
	else if(dir == MODE_DEL)
	{
		if(del_id(chptr, mask, list, mode_type) == 0)
		{
			/* mask isn't a valid ban, check raw_mask */
			if(del_id(chptr, raw_mask, list, mode_type))
				mask = raw_mask;
		}

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].caps = caps;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = mask;
	}
}

static void
chm_op(struct Client *source_p, struct Channel *chptr, int parc, int *parn,
       const char **parv, int *errors, int alev, int dir, char c, long mode_type)
{
	struct membership *msptr;
	const char *opnick;
	struct Client *targ_p;

	if(alev < CHACCESS_CHANOP)
	{
		if(!(*errors & SM_ERR_NOOPS))
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, source_p->name, chptr->chname);
		*errors |= SM_ERR_NOOPS;
		return;
	}

	if((dir == MODE_QUERY) || (parc <= *parn))
		return;

	if(IsRestricted(source_p) && (dir == MODE_ADD))
	{
		if(!(*errors & SM_ERR_RESTRICTED))
			sendto_one(source_p,
				   ":%s NOTICE %s :*** Notice -- You are restricted and cannot chanop others",
				   me.name, source_p->name);

		*errors |= SM_ERR_RESTRICTED;
		return;
	}

	opnick = parv[(*parn)];
	(*parn)++;

	/* empty nick */
	if(EmptyString(opnick))
	{
		sendto_one(source_p, form_str(ERR_NOSUCHNICK),
			   me.name, source_p->name, "*");
		return;
	}

	if((targ_p = find_chasing(source_p, opnick, NULL)) == NULL)
	{
		return;
	}

	msptr = find_channel_membership(chptr, targ_p);

	if(msptr == NULL)
	{
		if(!(*errors & SM_ERR_NOTONCHANNEL))
			sendto_one(source_p, form_str(ERR_USERNOTINCHANNEL),
				   me.name, source_p->name, opnick, chptr->chname);
		*errors |= SM_ERR_NOTONCHANNEL;
		return;
	}

	if(MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
		return;

	if(dir == MODE_ADD)
	{
		if(targ_p == source_p)
			return;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = targ_p->user->id;
		mode_changes[mode_count].arg = targ_p->name;
		mode_changes[mode_count++].client = targ_p;

		msptr->flags |= CHFL_CHANOP;
		msptr->flags &= ~CHFL_DEOPPED;
	}
	else
	{
		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = targ_p->user->id;
		mode_changes[mode_count].arg = targ_p->name;
		mode_changes[mode_count++].client = targ_p;

		msptr->flags &= ~CHFL_CHANOP;
	}
}

static void
chm_voice(struct Client *source_p, struct Channel *chptr, int parc, int *parn,
	  const char **parv, int *errors, int alev, int dir, char c, long mode_type)
{
	struct membership *msptr;
	const char *opnick;
	struct Client *targ_p;

	if(alev < CHACCESS_CHANOP)
	{
		if(!(*errors & SM_ERR_NOOPS))
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, source_p->name, chptr->chname);
		*errors |= SM_ERR_NOOPS;
		return;
	}

	if((dir == MODE_QUERY) || parc <= *parn)
		return;

	opnick = parv[(*parn)];
	(*parn)++;

	/* empty nick */
	if(EmptyString(opnick))
	{
		sendto_one(source_p, form_str(ERR_NOSUCHNICK),
			   me.name, source_p->name, "*");
		return;
	}

	if((targ_p = find_chasing(source_p, opnick, NULL)) == NULL)
	{
		return;
	}

	msptr = find_channel_membership(chptr, targ_p);

	if(msptr == NULL)
	{
		if(!(*errors & SM_ERR_NOTONCHANNEL))
			sendto_one(source_p, form_str(ERR_USERNOTINCHANNEL),
				   me.name, source_p->name, opnick, chptr->chname);
		*errors |= SM_ERR_NOTONCHANNEL;
		return;
	}

	if(MyClient(source_p) && (++mode_limit > MAXMODEPARAMS))
		return;

	if(dir == MODE_ADD)
	{
		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = targ_p->user->id;
		mode_changes[mode_count].arg = targ_p->name;
		mode_changes[mode_count++].client = targ_p;

		msptr->flags |= CHFL_VOICE;
	}
	else
	{
		mode_changes[mode_count].letter = 'v';
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = targ_p->user->id;
		mode_changes[mode_count].arg = targ_p->name;
		mode_changes[mode_count++].client = targ_p;

		msptr->flags &= ~CHFL_VOICE;
	}
}

static void
chm_limit(struct Client *source_p, struct Channel *chptr, int parc, int *parn,
	  const char **parv, int *errors, int alev, int dir, char c, long mode_type)
{
	const char *lstr;
	static char limitstr[30];
	int limit;

	if(alev < CHACCESS_CHANOP)
	{
		if(!(*errors & SM_ERR_NOOPS))
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, source_p->name, chptr->chname);
		*errors |= SM_ERR_NOOPS;
		return;
	}

	if(dir == MODE_QUERY)
		return;

	if((dir == MODE_ADD) && parc > *parn)
	{
		lstr = parv[(*parn)];
		(*parn)++;

		if(EmptyString(lstr) || (limit = atoi(lstr)) <= 0)
			return;

		ircsprintf(limitstr, "%d", limit);

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = limitstr;

		chptr->mode.limit = limit;
	}
	else if(dir == MODE_DEL)
	{
		if(!chptr->mode.limit)
			return;

		chptr->mode.limit = 0;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

static void
chm_key(struct Client *source_p, struct Channel *chptr, int parc, int *parn,
	const char **parv, int *errors, int alev, int dir, char c, long mode_type)
{
	char *key;

	if(alev < CHACCESS_CHANOP)
	{
		if(!(*errors & SM_ERR_NOOPS))
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, source_p->name, chptr->chname);
		*errors |= SM_ERR_NOOPS;
		return;
	}

	if(dir == MODE_QUERY)
		return;

	if((dir == MODE_ADD) && parc > *parn)
	{
		key = LOCAL_COPY(parv[(*parn)]);
		(*parn)++;

		if(EmptyString(key))
			return;

		if(MyClient(source_p))
			fix_key(key);
		else
			fix_key_remote(key);

		s_assert(key[0] != ' ');
		strlcpy(chptr->mode.key, key, sizeof(chptr->mode.key));

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = chptr->mode.key;
	}
	else if(dir == MODE_DEL)
	{
		if (parc > *parn)
			(*parn)++;

		if(!(*chptr->mode.key))
			return;

		*chptr->mode.key = 0;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].caps = 0;
		mode_changes[mode_count].nocaps = 0;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = "*";
	}
}

struct ChannelMode
{
	void (*func) (struct Client *source_p, struct Channel *chptr,
		      int parc, int *parn, const char **parv, int *errors,
		      int alev, int dir, char c, long mode_type);
	long mode_type;
};

/* *INDENT-OFF* */
static struct ChannelMode ModeTable[255] =
{
  {chm_nosuch,	0 },
  {chm_nosuch,	0 },			/* A */
  {chm_nosuch,	0 },			/* B */
  {chm_nosuch,	0 },			/* C */
  {chm_nosuch,	0 },			/* D */
  {chm_nosuch,	0 },			/* E */
  {chm_nosuch,	0 },			/* F */
  {chm_nosuch,	0 },			/* G */
  {chm_nosuch,	0 },			/* H */
  {chm_ban,	CHFL_INVEX },                    /* I */
  {chm_nosuch,	0 },			/* J */
  {chm_nosuch,	0 },			/* K */
  {chm_nosuch,	0 },			/* L */
  {chm_nosuch,	0 },			/* M */
  {chm_nosuch,	0 },			/* N */
  {chm_nosuch,	0 },			/* O */
  {chm_nosuch,	0 },			/* P */
  {chm_nosuch,	0 },			/* Q */
  {chm_nosuch,	0 },			/* R */
  {chm_nosuch,	0 },			/* S */
  {chm_nosuch,	0 },			/* T */
  {chm_nosuch,	0 },			/* U */
  {chm_nosuch,	0 },			/* V */
  {chm_nosuch,	0 },			/* W */
  {chm_nosuch,	0 },			/* X */
  {chm_nosuch,	0 },			/* Y */
  {chm_nosuch,	0 },			/* Z */
  {chm_nosuch,	0 },
  {chm_nosuch,	0 },
  {chm_nosuch,	0 },
  {chm_nosuch,	0 },
  {chm_nosuch,	0 },
  {chm_nosuch,	0 },
  {chm_nosuch,	0 },			/* a */
  {chm_ban,	CHFL_BAN },		/* b */
  {chm_nosuch,	0 },			/* c */
  {chm_nosuch,	0 },			/* d */
  {chm_ban,	CHFL_EXCEPTION },	/* e */
  {chm_nosuch,	0 },			/* f */
  {chm_nosuch,	0 },			/* g */
  {chm_nosuch,	0 },			/* h */
  {chm_simple,	MODE_INVITEONLY },	/* i */
  {chm_nosuch,	0 },			/* j */
  {chm_key,	0 },			/* k */
  {chm_limit,	0 },			/* l */
  {chm_simple,	MODE_MODERATED },	/* m */
  {chm_simple,	MODE_NOPRIVMSGS },	/* n */
  {chm_op,	0 },			/* o */
  {chm_simple,	MODE_PRIVATE },		/* p */
  {chm_nosuch,	0 },			/* q */
  {chm_nosuch,	0 },			/* r */
  {chm_simple,	MODE_SECRET },		/* s */
  {chm_simple,	MODE_TOPICLIMIT },	/* t */
  {chm_nosuch,	0 },			/* u */
  {chm_voice,	0 },			/* v */
  {chm_nosuch,	0 },			/* w */
  {chm_nosuch,	0 },			/* x */
  {chm_nosuch,	0 },			/* y */
  {chm_nosuch,	0 },			/* z */
};
/* *INDENT-ON* */

/* set_channel_mode()
 *
 * inputs	- client, source, channel, membership pointer, params
 * output	- 
 * side effects - channel modes/memberships are changed, MODE is issued
 */
void
set_channel_mode(struct Client *client_p, struct Client *source_p,
		 struct Channel *chptr, struct membership *msptr,
		 int parc, const char *parv[])
{
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	int pbl, mbl, nc, mc;
	int i;
	int dir = MODE_ADD;
	int parn = 1;
	int alevel, errors = 0;
	const char *ml = parv[0];
	char c;
	int table_position;

	mask_pos = 0;
	mode_count = 0;
	mode_limit = 0;

	if(!MyClient(source_p) || is_chanop(msptr))
		alevel = CHACCESS_CHANOP;
	else
		alevel = CHACCESS_PEON;

	for (; (c = *ml) != 0; ml++)
	{
		switch (c)
		{
		case '+':
			dir = MODE_ADD;
			break;
		case '-':
			dir = MODE_DEL;
			break;
		case '=':
			dir = MODE_QUERY;
			break;
		default:
			if(c < 'A' || c > 'z')
				table_position = 0;
			else
				table_position = c - 'A' + 1;
			ModeTable[table_position].func(source_p, chptr, parc,
						       &parn, parv, &errors,
						       alevel, dir, c, 
						       ModeTable[table_position].mode_type);
			break;
		}
	}

	dir = MODE_QUERY;

	/* bail out if we have nothing to do... */
	if(!mode_count)
		return;

	if(IsServer(source_p))
		mbl = ircsprintf(modebuf, ":%s MODE %s ", me.name, chptr->chname);
	else
		mbl = ircsprintf(modebuf, ":%s!%s@%s MODE %s ",
				 source_p->name, source_p->username, source_p->host, chptr->chname);

	pbl = 0;
	parabuf[0] = '\0';
	nc = 0;
	mc = 0;

	for (i = 0; i < mode_count; i++)
	{
		if(mode_changes[i].letter == 0 ||
		   mode_changes[i].mems == NON_CHANOPS || mode_changes[i].mems == ONLY_SERVERS)
			continue;

		if(mode_changes[i].arg != NULL &&
		   ((mc == MAXMODEPARAMS) ||
		    ((strlen(mode_changes[i].arg) + mbl + pbl + 2) > BUFSIZE)))
		{
			if(mbl && modebuf[mbl - 1] == '-')
				modebuf[mbl - 1] = '\0';

			if(nc != 0)
				sendto_channel_local(ALL_MEMBERS, chptr, "%s %s", modebuf, parabuf);

			nc = 0;
			mc = 0;

			if(IsServer(source_p))
				mbl = ircsprintf(modebuf, ":%s MODE %s ", me.name, chptr->chname);
			else
				mbl = ircsprintf(modebuf,
						 ":%s!%s@%s MODE %s ",
						 source_p->name,
						 source_p->username, source_p->host, chptr->chname);

			pbl = 0;
			parabuf[0] = '\0';
			dir = MODE_QUERY;
		}

		if(dir != mode_changes[i].dir)
		{
			modebuf[mbl++] = (mode_changes[i].dir == MODE_ADD) ? '+' : '-';
			dir = mode_changes[i].dir;
		}

		modebuf[mbl++] = mode_changes[i].letter;
		modebuf[mbl] = '\0';
		nc++;

		if(mode_changes[i].arg != NULL)
		{
			mc++;
			pbl = strlen(strcat(parabuf, mode_changes[i].arg));
			parabuf[pbl++] = ' ';
			parabuf[pbl] = '\0';
		}
	}

	if(pbl && parabuf[pbl - 1] == ' ')
		parabuf[pbl - 1] = '\0';

	if(nc != 0)
		sendto_channel_local(ALL_MEMBERS, chptr, "%s %s", modebuf, parabuf);

	send_cap_mode_changes(client_p, source_p, chptr, mode_changes, mode_count);
}

