/* modules/m_monitor.c
 * 
 *  Copyright (C) 2005 Lee Hardy <lee@leeh.co.uk>
 *  Copyright (C) 2005 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 */

#include "stdinc.h"
#include "tools.h"
#include "client.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "monitor.h"
#include "numeric.h"

static int m_monitor(struct Client *, struct Client *, int, const char **);

struct Message monitor_msgtab = {
	"MONITOR", 0, 0, 0, MFLG_SLOW,
	{{m_monitor, 2}, {m_monitor, 2}, mg_ignore, mg_ignore, mg_ignore, {m_monitor, 2}}
};

mapi_clist_av1 monitor_clist[] = { &monitor_msgtab, NULL };
DECLARE_MODULE_AV1(monitor, NULL, NULL, monitor_clist, NULL, NULL, "$Revision$");

static int
m_monitor(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	switch(parv[1][0])
	{
		case '+':
			if(parc < 3 || EmptyString(parv[2]))
			{
				sendto_one(client_p, form_str(ERR_NEEDMOREPARAMS),
						me.name, source_p->name, "MONITOR");
				return 0;
			}

			break;
		case '-':
			if(parc < 3 || EmptyString(parv[2]))
			{
				sendto_one(client_p, form_str(ERR_NEEDMOREPARAMS),
						me.name, source_p->name, "MONITOR");
				return 0;
			}

			break;

		case 'C':
		case 'c':
			clear_monitor(source_p);
			break;

		case 'L':
		case 'l':
			break;

		case 'S':
		case 's':
			break;

		default:
			break;
	}

	return 0;
}

