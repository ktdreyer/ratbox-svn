/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_pong.c: The reply to a ping message.
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
#include "ircd.h"
#include "handlers.h"
#include "s_user.h"
#include "client.h"
#include "hash.h"		/* for find_client() */
#include "numeric.h"
#include "s_conf.h"
#include "send.h"
#include "channel.h"
#include "irc_string.h"
#include "s_debug.h"
#include "msg.h"
#include "parse.h"
#include "hash.h"
#include "modules.h"

static void mr_pong(struct Client *, struct Client *, int, char **);
static void ms_pong(struct Client *, struct Client *, int, char **);

struct Message pong_msgtab = {
	"PONG", 0, 0, 1, 0, MFLG_SLOW | MFLG_UNREG, 0,
	{mr_pong, m_ignore, ms_pong, m_ignore}
};

#ifndef STATIC_MODULES
mapi_clist_av1 pong_clist[] = { &pong_msgtab, NULL };
DECLARE_MODULE_AV1(NULL, NULL, pong_clist, NULL, NULL, "$Revision$");
#endif

static void
ms_pong(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	struct Client *target_p;
	char *origin, *destination;

	if(parc < 2 || *parv[1] == '\0')
	{
		sendto_one(source_p, form_str(ERR_NOORIGIN), me.name, parv[0]);
		return;
	}

	origin = parv[1];
	destination = parv[2];
	source_p->flags &= ~FLAGS_PINGSENT;

	/* Now attempt to route the PONG, comstud pointed out routable PING
	 * is used for SPING.  routable PING should also probably be left in
	 *        -Dianora
	 * That being the case, we will route, but only for registered clients (a
	 * case can be made to allow them only from servers). -Shadowfax
	 */
	if(!EmptyString(destination) && !match(destination, me.name))
	{
		if((target_p = find_client(destination)) || (target_p = find_server(destination)))
			sendto_one(target_p, ":%s PONG %s %s", parv[0], origin, destination);
		else
		{
			sendto_one(source_p, form_str(ERR_NOSUCHSERVER),
				   me.name, parv[0], destination);
			return;
		}
	}

#ifdef  DEBUGMODE
	else
		Debug((DEBUG_NOTICE, "PONG: %s %s", origin, destination ? destination : "*"));
#endif
	return;
}

static void
mr_pong(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	if(parc == 2 && *parv[1] != '\0')
	{
		if(ConfigFileEntry.ping_cookie && source_p->user && source_p->name[0])
		{
			unsigned long incoming_ping = strtoul(parv[1], NULL, 10);
			if(incoming_ping)
			{
				if(source_p->localClient->random_ping == incoming_ping)
				{
					char buf[USERLEN + 1];
					strlcpy(buf, source_p->username, sizeof(buf));
					source_p->flags2 |= FLAGS2_PING_COOKIE;
					register_local_user(client_p, source_p, source_p->name,
							    buf);
				}
				else
				{
					sendto_one(source_p, form_str(ERR_WRONGPONG), me.name,
						   source_p->name,
						   source_p->localClient->random_ping);
					return;
				}
			}
		}

	}
	else
		sendto_one(source_p, form_str(ERR_NOORIGIN), me.name, parv[0]);

	source_p->flags &= ~FLAGS_PINGSENT;
}
