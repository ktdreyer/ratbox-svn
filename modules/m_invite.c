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
#include "channel_mode.h"
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
DECLARE_MODULE_AV1(NULL, NULL, invite_clist, NULL, NULL, "$Revision$");

/*
** m_invite
**      parv[0] - sender prefix
**      parv[1] - user to invite
**      parv[2] - channel number
*/
static int
m_invite(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	struct Channel *chptr;
	int chop = 1;

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
		sendto_one(source_p, form_str(ERR_NOSUCHNICK), me.name, parv[0], parv[1]);
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
	/* Possibly should be an error sent to source_p */
	/* done .. there should be no problem because MyConnect(source_p) should
	 * always be true if parse() and such is working correctly --is
	 */

	if(!MyConnect(target_p) && (parv[2][0] == '&'))
	{
		if(ConfigServerHide.hide_servers == 0)
			sendto_one(source_p, form_str(ERR_USERNOTONSERV),
				   me.name, parv[0], parv[1]);
		return 0;
	}

	if((chptr = find_channel(parv[2])) == NULL)
	{
		sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
			   me.name, parv[0], parv[2]);
		return 0;
	}

	if(MyClient(source_p) && !IsMember(source_p, chptr))
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

	/* remote clients are always 'chop' */
	if(MyClient(source_p))
		chop = is_chan_op(chptr, source_p);

	if(chptr && (chptr->mode.mode & MODE_INVITEONLY))
	{
		if(!chop)
		{
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, parv[0], parv[2]);
			return 0;
		}
	}
	else
		chop = 0;

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
		if(chop)
			add_invite(chptr, target_p);
		sendto_one(target_p, ":%s!%s@%s INVITE %s :%s", source_p->name,
			   source_p->username, source_p->host, target_p->name, chptr->chname);
	}
	else if(target_p->from != client_p)
	{
		sendto_one(target_p->from, ":%s INVITE %s :%s", parv[0],
			   target_p->name, chptr->chname);
	}

	return 0;
}
