/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_operwall.c: Sends a message to all IRCOps.
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
#include "handlers.h"
#include "client.h"
#include "ircd.h"
#include "irc_string.h"
#include "numeric.h"
#include "send.h"
#include "s_user.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

static int mo_operwall(struct Client *, struct Client *, int, const char **);
static int ms_operwall(struct Client *, struct Client *, int, const char **);

struct Message operwall_msgtab = {
	"OPERWALL", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_not_oper, ms_operwall, mo_operwall}
};

mapi_clist_av1 operwall_clist[] = { &operwall_msgtab, NULL };
DECLARE_MODULE_AV1(NULL, NULL, operwall_clist, NULL, NULL, "$Revision$");

/*
 * mo_operwall - OPERWALL message handler
 *  (write to *all* local opers currently online)
 *      parv[0] = sender prefix
 *      parv[1] = message text
 */

static int
mo_operwall(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *message = parv[1];

	if(!IsOperOperwall(source_p))
	{
		sendto_one(source_p, ":%s NOTICE %s :You need operwall = yes;",
			   me.name, source_p->name);
		return 0;
	}
	if(EmptyString(message))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS), me.name, parv[0], "OPERWALL");
		return 0;
	}

	sendto_server(NULL, NULL, NOCAPS, NOCAPS, ":%s OPERWALL :%s", parv[0], message);
	sendto_wallops_flags(UMODE_OPERWALL, source_p, "OPERWALL - %s", message);

	return 0;
}

/*
 * ms_operwall - OPERWALL message handler
 *  (write to *all* local opers currently online)
 *      parv[0] = sender prefix
 *      parv[1] = message text
 */
static int
ms_operwall(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *message = parv[1];

	if(EmptyString(message))
	{
		if(MyClient(source_p))
			sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
				   me.name, parv[0], "OPERWALL");
		return 0;
	}

	sendto_server(client_p, NULL, NOCAPS, NOCAPS, ":%s OPERWALL :%s", parv[0], message);
	sendto_wallops_flags(UMODE_OPERWALL, source_p, "OPERWALL - %s", message);

	return 0;
}
