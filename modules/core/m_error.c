/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_error.c: Handles error messages from the other end.
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
#include "client.h"
#include "common.h"		/* FALSE */
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "msg.h"
#include "memory.h"
#include "s_log.h"

static int m_error(struct Client *, struct Client *, int, const char **);
static int ms_error(struct Client *, struct Client *, int, const char **);

struct Message error_msgtab = {
	"ERROR", 0, 0, 0, MFLG_SLOW | MFLG_UNREG,
	{{m_error, 0}, mg_ignore, mg_ignore, {ms_error, 0}, mg_ignore}
};

mapi_clist_av1 error_clist[] = {
        &error_msgtab, NULL
};

DECLARE_MODULE_AV1(error, NULL, NULL, error_clist, NULL, NULL, "$Revision$");


/*
 * Note: At least at protocol level ERROR has only one parameter,
 * although this is called internally from other functions
 * --msa
 *
 *      parv[0] = sender prefix
 *      parv[*] = parameters
 */
int
m_error(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *para;

	para = (parc > 1 && *parv[1] != '\0') ? parv[1] : "<>";

	ilog(L_SERVER, "Received ERROR message from %s: %s", source_p->name, para);

	if(client_p == source_p)
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "ERROR :from %s -- %s",
				     get_client_name(client_p, HIDE_IP), para);

#ifndef HIDE_SERVERS_IPS
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "ERROR :from %s -- %s",
				     get_client_name(client_p, MASK_IP), para);
#endif
	}
	else
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "ERROR :from %s via %s -- %s",
				     source_p->name, get_client_name(client_p, HIDE_IP), para);

#ifndef HIDE_SERVERS_IPS
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "ERROR :from %s via %s -- %s",
				     source_p->name, get_client_name(client_p, MASK_IP), para);
#endif
	}

	if(MyClient(source_p))
		exit_client(client_p, source_p, source_p, "ERROR");

	return 0;
}

static int
ms_error(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *para;

	para = (parc > 1 && *parv[1] != '\0') ? parv[1] : "<>";

	ilog(L_SERVER, "Received ERROR message from %s: %s", source_p->name, para);

	if(client_p == source_p)
		sendto_realops_flags(UMODE_ALL,
#ifndef HIDE_SERVERS_IPS
				     L_ALL,
#else
				     L_ADMIN,
#endif
				     "ERROR :from %s -- %s",
				     get_client_name(client_p, MASK_IP), para);
	else
		sendto_realops_flags(UMODE_ALL,
#ifndef HIDE_SERVERS_IPS
				     L_ALL,
#else
				     L_ADMIN,
#endif
				     "ERROR :from %s via %s -- %s",
				     source_p->name, get_client_name(client_p, MASK_IP), para);

	return 0;
}
