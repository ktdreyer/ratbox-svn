/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_knock.c: Requests to be invited to a channel.
 *
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2003 ircd-ratbox development team
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
#include "sprintf_irc.h"
#include "tools.h"
#include "handlers.h"
#include "channel.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "s_serv.h"

static int m_knock(struct Client *, struct Client *, int, const char **);
static int ms_knock(struct Client *, struct Client *, int, const char **);

struct Message knock_msgtab = {
	"KNOCK", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_knock, ms_knock, m_knock}
};

mapi_clist_av1 knock_clist[] = { &knock_msgtab, NULL };
DECLARE_MODULE_AV1(knock, NULL, NULL, knock_clist, NULL, NULL, NULL, "$Revision$");

static void parse_knock(struct Client *, struct Client *, const char *);

/* m_knock
 *    parv[0] = sender prefix
 *    parv[1] = channel
 *
 *  The KNOCK command has the following syntax:
 *   :<sender> KNOCK <channel>
 *
 *  If a user is not banned from the channel they can use the KNOCK
 *  command to have the server NOTICE the channel operators notifying
 *  they would like to join.  Helpful if the channel is invite-only, the
 *  key is forgotten, or the channel is full (INVITE can bypass each one
 *  of these conditions.  Concept by Dianora <db@db.net> and written by
 *  <anonymous>
 */
static int
m_knock(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(EmptyString(parv[1]))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
			   me.name, parv[0], "KNOCK");
		return 0;
	}

	if(ConfigChannel.use_knock == 0)
	{
		sendto_one(source_p, form_str(ERR_KNOCKDISABLED),
			   me.name, source_p->name);
		return 0;
	}


	parse_knock(client_p, source_p, parv[1]);

	return 0;
}

/* 
 * ms_knock()
 *	parv[0] = sender prefix
 *	parv[1] = channel
 */
static int
ms_knock(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(EmptyString(parv[1]) || !IsClient(source_p))
		return 0;

	parse_knock(client_p, source_p, parv[1]);

	return 0;
}


/* parse_knock()
 *
 * input        - client/source issuing knock, channel knocking to
 * output       -
 * side effects - sends knock if possible, else errors
 */
static void
parse_knock(struct Client *client_p, struct Client *source_p,
	    const char *channame)
{
	struct Channel *chptr;
	char *p, *name;

	name = LOCAL_COPY(channame);

	/* dont allow one knock to multiple chans */
	if((p = strchr(name, ',')))
		*p = '\0';

	if(!IsChannelName(name))
	{
		sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
			   me.name, source_p->name, name);
		return;
	}

	if((chptr = find_channel(name)) == NULL)
	{
		sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
			   me.name, source_p->name, name);
		return;
	}

	if(IsMember(source_p, chptr))
	{
		sendto_one(source_p, form_str(ERR_KNOCKONCHAN),
			   me.name, source_p->name, name);
		return;
	}

	if(!((chptr->mode.mode & MODE_INVITEONLY) || (*chptr->mode.key) || 
	     (chptr->mode.limit && 
	      dlink_list_length(&chptr->members) >= chptr->mode.limit)))
	{
		sendto_one(source_p, form_str(ERR_CHANOPEN),
			   me.name, source_p->name, name);
		return;
	}

	/* cant knock to a +p channel */
	if(HiddenChannel(chptr))
	{
		sendto_one(source_p, form_str(ERR_CANNOTSENDTOCHAN),
			   me.name, source_p->name, name);
		return;
	}

	
	if(MyClient(source_p))
	{
		/* don't allow a knock if the user is banned */
		if(is_banned(chptr, source_p, NULL, NULL))
		{
			sendto_one(source_p, form_str(ERR_CANNOTSENDTOCHAN),
				   me.name, source_p->name, name);
			return;
		}

		/* local flood protection:
		 * allow one knock per user per knock_delay
		 * allow one knock per channel per knock_delay_channel
		 */
		if(!IsOper(source_p) && 
		   (source_p->localClient->last_knock + ConfigChannel.knock_delay) > CurrentTime)
		{
			sendto_one(source_p, form_str(ERR_TOOMANYKNOCK),
					me.name, source_p->name, name, "user");
			return;
		}
		else if((chptr->last_knock + ConfigChannel.knock_delay_channel) > CurrentTime)
		{
			sendto_one(source_p, form_str(ERR_TOOMANYKNOCK),
					me.name, source_p->name, name, "channel");
			return;
		}

		/* ok, we actually can send the knock, tell client */
		source_p->localClient->last_knock = CurrentTime;

		sendto_one(source_p, form_str(RPL_KNOCKDLVR),
			   me.name, source_p->name, name);
	}

	chptr->last_knock = CurrentTime;

	if(ConfigChannel.use_knock)
		sendto_channel_local(ONLY_CHANOPS, chptr, form_str(RPL_KNOCK),
				     me.name, name, name, source_p->name,
				     source_p->username, source_p->host);

	sendto_server(client_p, chptr, CAP_KNOCK, NOCAPS,
		      ":%s KNOCK %s", source_p->name, name);
	return;
}

