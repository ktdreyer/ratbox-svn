/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_invite.c: Invites the user to join a channel.
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
#include "handlers.h"
#include "common.h"
#include "channel.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "s_serv.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "packet.h"

static int m_invite(struct Client *, struct Client *, int, const char **);

struct Message invite_msgtab = {
	"INVITE", 0, 0, 3, 0, MFLG_SLOW, 0,
	{m_unregistered, m_invite, m_invite, m_invite}
};
mapi_clist_av1 invite_clist[] = { &invite_msgtab, NULL };
DECLARE_MODULE_AV1(invite, NULL, NULL, invite_clist, NULL, NULL, NULL, "$Revision$");

static void add_invite(struct Channel *, struct Client *);

/* m_invite()
 *      parv[0] - sender prefix
 *      parv[1] - user to invite
 *      parv[2] - channel name
 */
static int
m_invite(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	struct Channel *chptr;
	struct membership *msptr;
	int store_invite = 0;

	if(EmptyString(parv[2]))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS), me.name, parv[0], "INVITE");
		return 0;
	}

	if(!IsClient(source_p))
		return 0;

	if(MyClient(source_p) && !IsFloodDone(source_p))
		flood_endgrace(source_p);

	if((target_p = find_person(parv[1])) == NULL)
	{
		sendto_one(source_p, form_str(ERR_NOSUCHNICK),
			   me.name, parv[0], parv[1]);
		return 0;
	}

	if(check_channel_name(parv[2]) == 0)
	{
		sendto_one(source_p, form_str(ERR_BADCHANNAME),
			   me.name, parv[0], (unsigned char *) parv[2]);
		return 0;
	}

	if(!IsChannelName(parv[2]))
	{
		if(MyClient(source_p))
			sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
				   me.name, parv[0], parv[2]);
		return 0;
	}

	/* Do not send local channel invites to users if they are not on the
	 * same server as the person sending the INVITE message. 
	 */
	if(parv[2][0] == '&')
	{
		if(ConfigServerHide.disable_local_channels)
			return 0;

		/* if we're in shide, we're buggered anyway, because they
		 * could just test for RPL_INVITED to determine whether a
		 * user is local or not.  rely on disable_local_channels --fl
		 */
		if(!MyConnect(target_p))
		{
			sendto_one(source_p, form_str(ERR_USERNOTONSERV),
				   me.name, parv[0], parv[1]);
			return 0;
		}
	}

	if((chptr = find_channel(parv[2])) == NULL)
	{
		sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
			   me.name, parv[0], parv[2]);
		return 0;
	}

	msptr = find_channel_membership(chptr, source_p);
	if(MyClient(source_p) && (msptr == NULL))
	{
		sendto_one(source_p, form_str(ERR_NOTONCHANNEL), me.name, parv[0], parv[2]);
		return 0;
	}

	if(IsMember(target_p, chptr))
	{
		sendto_one(source_p, form_str(ERR_USERONCHANNEL),
			   me.name, parv[0], parv[1], parv[2]);
		return 0;
	}

	/* only store invites for +i channels */
	if(chptr && (chptr->mode.mode & MODE_INVITEONLY))
	{
		/* treat remote clients as chanops */
		if(MyClient(source_p) && !is_chanop(msptr))
		{
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, parv[0], parv[2]);
			return 0;
		}

		store_invite = 1;
	}

	if(MyConnect(source_p))
	{
		sendto_one(source_p, form_str(RPL_INVITING), me.name, parv[0],
			   target_p->name, parv[2]);
		if(target_p->user->away)
			sendto_one(source_p, form_str(RPL_AWAY), me.name, parv[0],
				   target_p->name, target_p->user->away);
	}

	if(MyConnect(target_p))
	{
		sendto_one(target_p, ":%s!%s@%s INVITE %s :%s", 
			   source_p->name, source_p->username, source_p->host, 
			   target_p->name, chptr->chname);

		if(store_invite)
			add_invite(chptr, target_p);
	}
	else if(target_p->from != client_p)
	{
		sendto_one(target_p->from, ":%s INVITE %s :%s", parv[0],
			   target_p->name, chptr->chname);
	}

	return 0;
}

/* add_invite()
 *
 * input	- channel to add invite to, client to add
 * output	-
 * side effects - client is added to invite list.
 */
static void
add_invite(struct Channel *chptr, struct Client *who)
{
	dlink_node *ptr;

	/* already invited? */
	DLINK_FOREACH(ptr, who->user->invited.head)
	{
		if(ptr->data == chptr)
			return;
	}

	/* ok, if their invite list is too long, remove the tail */
	if((int)dlink_list_length(&who->user->invited) >= 
	   ConfigChannel.max_chans_per_user)
	{
		dlink_node *ptr = who->user->invited.tail;
		del_invite(ptr->data, who);
	}

	/* add user to channel invite list */
	dlinkAddAlloc(who, &chptr->invites);

	/* add channel to user invite list */
	dlinkAddAlloc(chptr, &who->user->invited);
}


