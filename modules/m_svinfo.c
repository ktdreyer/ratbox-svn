/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_svinfo.c: Sends TS information for clock & compatibility checks.
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
#include "common.h"		/* TRUE bleah */
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "s_log.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "hash.h"

static int ms_svinfo(struct Client *, struct Client *, int, const char **);

struct Message svinfo_msgtab = {
	"SVINFO", 0, 0, 4, 0, MFLG_SLOW, 0,
	{m_unregistered, m_ignore, ms_svinfo, m_ignore}
};

mapi_clist_av1 svinfo_clist[] = { &svinfo_msgtab, NULL };
DECLARE_MODULE_AV1(svinfo, NULL, NULL, svinfo_clist, NULL, NULL, "$Revision$");

/*
 * ms_svinfo - SVINFO message handler
 *      parv[0] = sender prefix
 *      parv[1] = TS_CURRENT for the server
 *      parv[2] = TS_MIN for the server
 *      parv[3] = servers sid (if ts6), else 0
 *      parv[4] = server's idea of UTC time
 */
static int
ms_svinfo(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	time_t deltat;
	time_t theirtime;
	int tsver = atoi(parv[1]);

	/* SVINFO isnt remote. */
	if(source_p != client_p)
		return 0;

	if(TS_CURRENT < atoi(parv[2]) || tsver < TS_MIN)
	{
		/* TS version is too low on one of the sides, drop the link */
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s dropped, wrong TS protocol version (%s,%s)",
				     get_client_name(source_p, SHOW_IP), parv[1], parv[2]);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s dropped, wrong TS protocol version (%s,%s)",
				     get_client_name(source_p, MASK_IP), parv[1], parv[2]);
		exit_client(source_p, source_p, source_p, "Incompatible TS version");
		return 0;
	}

	/*
	 * since we're here, might as well set CurrentTime while we're at it
	 */
	set_time();
	theirtime = atol(parv[4]);
	deltat = abs(theirtime - CurrentTime);

	if(deltat > ConfigFileEntry.ts_max_delta)
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Link %s dropped, excessive TS delta (my TS=%lu, their TS=%lu, delta=%d)",
				     get_client_name(source_p, SHOW_IP),
				     (unsigned long) CurrentTime,
				     (unsigned long) theirtime, (int) deltat);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Link %s dropped, excessive TS delta (my TS=%lu, their TS=%lu, delta=%d)",
				     get_client_name(source_p, MASK_IP),
				     (unsigned long) CurrentTime,
				     (unsigned long) theirtime, (int) deltat);
		ilog(L_NOTICE,
		     "Link %s dropped, excessive TS delta (my TS=%lu, their TS=%lu, delta=%d)",
		     log_client_name(source_p, SHOW_IP), CurrentTime, theirtime, (int) deltat);
		exit_client(source_p, source_p, source_p, "Excessive TS delta");
		return 0;
	}

	if(deltat > ConfigFileEntry.ts_warn_delta)
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Link %s notable TS delta (my TS=%lu, their TS=%lu, delta=%d)",
				     source_p->name, CurrentTime, theirtime, (int) deltat);
	}

	client_p->serv->tsver = tsver;

	if(DoesTS6(client_p))
	{
		/* SVINFO received twice? erk! */
		if(client_p->id[0] != '\0')
		{
			exit_client(NULL, client_p, client_p, 
				    "SVINFO received twice.");
			return 0;
		}

		/* invalid sid? */
		if(!IsDigit(parv[3][0]) || !IsIdChar(parv[3][1]) ||
		   !IsIdChar(parv[3][2]) || parv[3][3] != '\0')
		{
			sendto_realops_flags(UMODE_ALL, L_ADMIN,
					     "Link %s dropped, invalid SID: %s",
					     get_client_name(source_p, SHOW_IP),
					     parv[3]);
			sendto_realops_flags(UMODE_ALL, L_OPER,
					     "Link %s dropped, invalid SID: %s",
					     get_client_name(source_p, MASK_IP),
					     parv[3]);
			ilog(L_NOTICE, "Link %s dropped, invalid SID: %s",
			     log_client_name(source_p, SHOW_IP), parv[3]);
			exit_client(source_p, source_p, source_p, 
				    "Invalid SID");
		}
		else
		{
			strcpy(client_p->id, parv[3]);
			add_to_id_hash(client_p->id, client_p);
		}
	}		

	return 0;
}

