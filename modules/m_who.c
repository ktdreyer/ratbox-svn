/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_who.c: Shows who is on a channel.
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
#include "common.h"
#include "handlers.h"
#include "client.h"
#include "channel.h"
#include "channel_mode.h"
#include "hash.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "packet.h"

static int m_who(struct Client *, struct Client *, int, const char **);

struct Message who_msgtab = {
	"WHO", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_who, m_ignore, m_who}
};

mapi_clist_av1 who_clist[] = { &who_msgtab, NULL };
DECLARE_MODULE_AV1(NULL, NULL, who_clist, NULL, NULL, "$Revision$");

static void do_who_on_channel(struct Client *source_p,
			      struct Channel *chptr, const char *real_name,
			      int server_oper, int member);

static void who_global(struct Client *source_p, const char *mask, int server_oper);

static void do_who(struct Client *source_p,
		   struct Client *target_p, const char *chname, const char *op_flags);


/*
** m_who
**      parv[0] = sender prefix
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag, only 'o' for now.
*/
static int
m_who(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	char *mask;
	dlink_node *lp;
	struct Channel *chptr = NULL;
	struct Channel *mychannel = NULL;
	int server_oper = parc > 2 ? (*parv[2] == 'o') : 0;	/* Show OPERS only */
	int member;

	if (parc > 1)
		mask = LOCAL_COPY(parv[1]);
	else
		mask = NULL;

	/* See if mask is there, collapse it or return if not there */

	if(mask != NULL)
	{
		(void) collapse(mask);

		if(*mask == '\0')
		{
			sendto_one(source_p, form_str(RPL_ENDOFWHO), me.name, parv[0], "*");
			return 0;
		}
	}
	else
	{
		if(!IsFloodDone(source_p))
			flood_endgrace(source_p);

		who_global(source_p, mask, server_oper);
		sendto_one(source_p, form_str(RPL_ENDOFWHO), me.name, parv[0], "*");
		return 0;
	}

	/* mask isn't NULL at this point. repeat after me... -db */

	/* '/who *' */

	if((*(mask + 1) == (char) 0) && (*mask == '*'))
	{
		if(source_p->user)
			if((lp = source_p->user->channel.head))
				mychannel = lp->data;

		if(!mychannel)
		{
			sendto_one(source_p, form_str(RPL_ENDOFWHO), me.name, parv[0], "*");
			return 0;
		}

		do_who_on_channel(source_p, mychannel, "*", NO, YES);

		sendto_one(source_p, form_str(RPL_ENDOFWHO), me.name, parv[0], "*");
		return 0;
	}

	/* '/who #some_channel' */

	if(IsChannelName(mask))
	{
		/*
		 * List all users on a given channel
		 */
		chptr = find_channel(mask);
		if(chptr != NULL)
		{
			if(IsMember(source_p, chptr))
				do_who_on_channel(source_p, chptr, chptr->chname, NO, YES);
			else if(!SecretChannel(chptr))
				do_who_on_channel(source_p, chptr, chptr->chname, NO, NO);
		}
		sendto_one(source_p, form_str(RPL_ENDOFWHO), me.name, parv[0], mask);
		return 0;
	}

	/* '/who nick' */

	if(((target_p = find_client(mask)) != NULL) &&
	   IsPerson(target_p) && (!server_oper || IsOper(target_p)))
	{
		char *chname = NULL;
		int isinvis = 0;


		isinvis = IsInvisible(target_p);
		DLINK_FOREACH(lp, target_p->user->channel.head)
		{
			chptr = lp->data;
			chname = chptr->chname;

			member = IsMember(source_p, chptr);
			if(isinvis && !member)
			{
				chptr = NULL;
				continue;
			}
			if(member || (!isinvis && PubChannel(chptr)))
			{
				break;
			}
			chptr = NULL;	/* Must be NULL if we don't loop again */
		}

		if(chptr != NULL)
		{
			if(is_chan_op(chptr, target_p))
				do_who(source_p, target_p, chname, channel_flags[0]);
			else if(is_voiced(chptr, target_p))
				do_who(source_p, target_p, chname, channel_flags[2]);
			else
				do_who(source_p, target_p, chname, "");
		}
		else
		{
			do_who(source_p, target_p, NULL, "");
		}

		sendto_one(source_p, form_str(RPL_ENDOFWHO), me.name, parv[0], mask);
		return 0;
	}

	if(!IsFloodDone(source_p))
		flood_endgrace(source_p);

	/* '/who 0' */
	if((*(mask + 1) == '\0') && (*mask == '0'))
		who_global(source_p, NULL, server_oper);
	else
		who_global(source_p, mask, server_oper);

	/* Wasn't a nick, wasn't a channel, wasn't a '*' so ... */
	sendto_one(source_p, form_str(RPL_ENDOFWHO), me.name, parv[0], mask);

	return 0;
}

/* who_common_channel
 * inputs	- pointer to client requesting who
 * 		- pointer to channel member chain.
 *		- char * mask to match
 *		- int if oper on a server or not
 *		- pointer to int maxmatches
 * output	- NONE
 * side effects - lists matching clients on specified channel,
 * 		  marks matched clients.
 *
 */
static void
who_common_channel(struct Client *source_p, dlink_list chain,
		   const char *mask, int server_oper, int *maxmatches)
{
	dlink_node *clp;
	struct Client *target_p;

	DLINK_FOREACH(clp, chain.head)
	{
		target_p = clp->data;

		if(!IsInvisible(target_p) || IsMarked(target_p))
			continue;

		if(server_oper && !IsOper(target_p))
			continue;

		SetMark(target_p);

		if((mask == NULL) ||
		   match(mask, target_p->name) || match(mask, target_p->username) ||
		   match(mask, target_p->host) ||
		   (match(mask, target_p->user->server) &&
		    (IsOper(source_p) || !ConfigServerHide.hide_servers)) ||
		   match(mask, target_p->info))
		{

			do_who(source_p, target_p, NULL, "");

			if(*maxmatches > 0)
			{
				--(*maxmatches);
				if(*maxmatches == 0)
					return;
			}

		}
	}
}

/*
 * who_global
 *
 * inputs	- pointer to client requesting who
 *		- char * mask to match
 *		- int if oper on a server or not
 * output	- NONE
 * side effects - do a global scan of all clients looking for match
 *		  this is slightly expensive on EFnet ...
 */

static void
who_global(struct Client *source_p, const char *mask, int server_oper)
{
	struct Channel *chptr = NULL;
	struct Client *target_p;
	dlink_node *lp, *ptr;
	int maxmatches = 500;

	/* first, list all matching INvisible clients on common channels */

	DLINK_FOREACH(lp, source_p->user->channel.head)
	{
		chptr = lp->data;
		who_common_channel(source_p, chptr->chanops, mask, server_oper, &maxmatches);
		who_common_channel(source_p, chptr->chanops_voiced, mask, server_oper, &maxmatches);
		who_common_channel(source_p, chptr->voiced, mask, server_oper, &maxmatches);
		who_common_channel(source_p, chptr->peons, mask, server_oper, &maxmatches);
	}

	/* second, list all matching visible clients */
	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = (struct Client *) ptr->data;
		if(!IsPerson(target_p))
			continue;

		if(IsInvisible(target_p))
		{
			ClearMark(target_p);
			continue;
		}

		if(server_oper && !IsOper(target_p))
			continue;

		if(!mask ||
		   match(mask, target_p->name) || match(mask, target_p->username) ||
		   match(mask, target_p->host) || match(mask, target_p->user->server) ||
		   match(mask, target_p->info))
		{

			do_who(source_p, target_p, NULL, "");
			if(maxmatches > 0)
			{
				--maxmatches;
				if(maxmatches == 0)
					return;
			}

		}

	}
}


/*
 * do_who_on_channel
 *
 * inputs	- pointer to client requesting who
 *		- pointer to channel to do who on
 *		- The "real name" of this channel
 *		- int if source_p is a server oper or not
 *		- int if client is member or not
 * output	- NONE
 * side effects - do a who on given channel
 */

static void
do_who_on_channel(struct Client *source_p, struct Channel *chptr,
		  const char *chname, int server_oper, int member)
{
	struct Client *target_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, chptr->peons.head)
	{
		target_p = ptr->data;

		if(member || !IsInvisible(target_p))
			do_who(source_p, target_p, chname, channel_flags[3]);
	}

	DLINK_FOREACH(ptr, chptr->voiced.head)
	{
		target_p = ptr->data;

		if(member || !IsInvisible(target_p))
			do_who(source_p, target_p, chname, channel_flags[2]);
	}

	DLINK_FOREACH(ptr, chptr->chanops_voiced.head)
	{
		target_p = ptr->data;

		if(member || !IsInvisible(target_p))
			do_who(source_p, target_p, chname, channel_flags[1]);
	}

	DLINK_FOREACH(ptr, chptr->chanops.head)
	{
		target_p = ptr->data;

		if(member || !IsInvisible(target_p))
			do_who(source_p, target_p, chname, channel_flags[0]);
	}
}

/*
 * do_who
 *
 * inputs	- pointer to client requesting who
 *		- pointer to client to do who on
 *		- The reported name
 *		- channel flags
 * output	- NONE
 * side effects - do a who on given person
 */

static void
do_who(struct Client *source_p, struct Client *target_p, const char *chname, const char *op_flags)
{
	char status[5];

	ircsprintf(status, "%c%s%s",
		   target_p->user->away ? 'G' : 'H', IsOper(target_p) ? "*" : "", op_flags);

	sendto_one(source_p, form_str(RPL_WHOREPLY), me.name, source_p->name,
		   (chname) ? (chname) : "*",
		   target_p->username,
		   target_p->host, target_p->user->server, target_p->name,
		   status, target_p->hopcount, target_p->info);
}
