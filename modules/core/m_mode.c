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
#include "handlers.h"
#include "channel.h"
#include "channel_mode.h"
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

static void m_mode(struct Client *, struct Client *, int, char **);

struct Message mode_msgtab = {
	"MODE", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_mode, m_mode, m_mode}
};

#ifndef STATIC_MODULES
mapi_clist_av1 mode_clist[] = { &mode_msgtab, NULL };
DECLARE_MODULE_AV1(NULL, NULL, mode_clist, NULL, "$Revision$");
#endif

/*
 * m_mode - MODE command handler
 * parv[0] - sender
 * parv[1] - channel
 */
static void
m_mode(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	struct Channel *chptr = NULL;
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	dlink_node *ptr;
	int n = 2;

	/* Now, try to find the channel in question */
	if(!IsChanPrefix(parv[1][0]))
	{
		/* if here, it has to be a non-channel name */
		user_mode(client_p, source_p, parc, parv);
		return;
	}

	if(!check_channel_name(parv[1]))
	{
		sendto_one(source_p, form_str(ERR_BADCHANNAME),
			   me.name, parv[0], (unsigned char *) parv[1]);
		return;
	}

	chptr = hash_find_channel(parv[1]);

	if(chptr == NULL)
	{
		sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0], parv[1]);
		return;
	}

	/* Now known the channel exists */


	if(parc < n + 1)
	{
		channel_modes(chptr, source_p, modebuf, parabuf);
		sendto_one(source_p, form_str(RPL_CHANNELMODEIS),
			   me.name, parv[0], parv[1], modebuf, parabuf);

		sendto_one(source_p, form_str(RPL_CREATIONTIME),
			   me.name, parv[0], parv[1], chptr->channelts);
	}
	/* bounce all modes from people we deop on sjoin */
	else if((ptr = find_user_link(&chptr->deopped, source_p)) == NULL)
	{
		/* Finish the flood grace period... */
		if(MyClient(source_p) && !IsFloodDone(source_p))
		{
			if((parc == 3) && (parv[2][0] == 'b') && (parv[2][1] == '\0'))
				;
			else
				flood_endgrace(source_p);
		}

		set_channel_mode(client_p, source_p, chptr, parc - n, parv + n, chptr->chname);
	}
}
