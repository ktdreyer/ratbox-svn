/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_pass.c: Used to send a password for a server or client{} block.
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
#include "handlers.h"		/* m_pass prototype */
#include "client.h"		/* client struct */
#include "irc_string.h"
#include "send.h"		/* sendto_one */
#include "numeric.h"		/* ERR_xxx */
#include "ircd.h"		/* me */
#include "msg.h"
#include "parse.h"
#include "modules.h"

static void mr_pass(struct Client *, struct Client *, int, char **);

struct Message pass_msgtab = {
	"PASS", 0, 0, 2, 0, MFLG_SLOW | MFLG_UNREG, 0,
	{mr_pass, m_registered, m_ignore, m_registered}
};

#ifndef STATIC_MODULES
mapi_clist_av1 pass_clist[] = { &pass_msgtab, NULL };
DECLARE_MODULE_AV1(NULL, NULL, pass_clist, NULL, NULL, "$Revision$");
#endif

/*
 * m_pass() - Added Sat, 4 March 1989
 *
 *
 * mr_pass - PASS message handler
 *      parv[0] = sender prefix
 *      parv[1] = password
 *      parv[2] = optional extra version information
 */
static void
mr_pass(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	const char *password = parv[1];

	if(EmptyString(password))
	{
		sendto_one(client_p, form_str(ERR_NEEDMOREPARAMS),
			   me.name, EmptyString(parv[0]) ? "*" : parv[0], "PASS");
		return;
	}

	strlcpy(client_p->localClient->passwd, password, sizeof(client_p->localClient->passwd));

	if(parc > 2)
	{
		/* 
		 * It looks to me as if orabidoo wanted to have more
		 * than one set of option strings possible here...
		 * i.e. ":AABBTS" as long as TS was the last two chars
		 * however, as we are now using CAPAB, I think we can
		 * safely assume if there is a ":TS" then its a TS server
		 * -Dianora
		 */
		if(0 == irccmp(parv[2], "TS") && client_p->tsinfo == 0)
			client_p->tsinfo = TS_DOESTS;
	}
}
