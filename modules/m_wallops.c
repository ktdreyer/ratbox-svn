/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_wallops.c: Sends a message to all operators.
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
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "s_serv.h"

static int ms_wallops(struct Client *, struct Client *, int, const char **);
static int mo_wallops(struct Client *, struct Client *, int, const char **);

struct Message wallops_msgtab = {
	"WALLOPS", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_not_oper, ms_wallops, mo_wallops}
};

mapi_clist_av1 wallops_clist[] = { &wallops_msgtab, NULL };
DECLARE_MODULE_AV1(wallops, NULL, NULL, wallops_clist, NULL, NULL, "$Revision$");

/*
 * mo_wallops (write to *all* opers currently online)
 *      parv[1] = message text
 */
static int
mo_wallops(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *message;

	message = parv[1];

	if(EmptyString(message))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS), 
			   me.name, source_p->name, "WALLOPS");
		return 0;
	}

	sendto_wallops_flags(UMODE_OPERWALL, source_p, "OPERWALL - %s", message);
	sendto_server(NULL, NULL, CAP_TS6, NOCAPS, ":%s WALLOPS :%s", 
		      use_id(source_p), message);
	sendto_server(NULL, NULL, NOCAPS, CAP_TS6, ":%s WALLOPS :%s", 
		      source_p->name, message);

	return 0;
}

/*
 * ms_wallops (write to *all* opers currently online)
 *      parv[1] = message text
 */
static int
ms_wallops(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *message;

	message = parv[1];

	if(EmptyString(message))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS), 
			   get_id(&me, source_p), get_id(source_p, source_p),
			   "WALLOPS");
		return 0;
	}

	if(IsClient(source_p))
		sendto_wallops_flags(UMODE_OPERWALL, source_p, "OPERWALL - %s", message);
	else
		sendto_wallops_flags(UMODE_WALLOP, source_p, "%s", message);

	sendto_server(client_p, NULL, CAP_TS6, NOCAPS, ":%s WALLOPS :%s", 
		      use_id(source_p), message);
	sendto_server(client_p, NULL, NOCAPS, CAP_TS6, ":%s WALLOPS :%s", 
		      source_p->name, message);

	return 0;
}
