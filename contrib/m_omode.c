/*
 *  ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 *  m_omode.c: Sets a user or channel mode.
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
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_user.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "packet.h"

static int mo_omode(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);

#ifndef STATIC_MODULES

struct Message omode_msgtab = {
	"OMODE", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {mo_omode, 2}}
};

mapi_clist_av1 omode_clist[] = { &omode_msgtab, NULL };

DECLARE_MODULE_AV1(omode, NULL, NULL, omode_clist, NULL, NULL, "$Revision$");

#endif

/*
 * m_omode - MODE command handler
 * parv[0] - sender
 * parv[1] - channel
 */
static int
mo_omode(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr = NULL;
	struct membership *msptr;
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	int n = 2;

	if(EmptyString(parv[1]))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS), me.name, parv[0], "MODE");
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

	if((chptr = find_channel(parv[1])) == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL),
				   parv[1]);
		return 0;
	}

	sendto_wallops_flags(UMODE_WALLOP, &me,
			     "OMODE called for [%s] by %s!%s@%s",
			     parv[1], source_p->name, source_p->username, source_p->host);
	ilog(L_MAIN, "OMODE called for [%s] by %s!%s@%s",
	     parv[1], source_p->name, source_p->username, source_p->host);

	if(*chptr->chname != '&')
		sendto_server(NULL, NULL, NOCAPS, NOCAPS,
			      ":%s WALLOPS :OMODE called for [%s] by %s!%s@%s",
			      me.name, parv[1], source_p->name, source_p->username, source_p->host);

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
				 parc - n, parv + n, chptr->chname);
	}
	else
	{
		msptr = find_channel_membership(chptr, source_p);

		set_channel_mode(client_p, source_p->servptr, chptr, msptr,
				 parc - n, parv + n, chptr->chname);
	}

	return 0;
}
