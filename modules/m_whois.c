/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_whois.c: Shows who a user is.
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
#include "common.h"
#include "handlers.h"
#include "client.h"
#include "hash.h"
#include "channel.h"
#include "hash.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "hook.h"

static void do_whois(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void single_whois(struct Client *source_p, struct Client *target_p);

static int m_whois(struct Client *, struct Client *, int, const char **);
static int ms_whois(struct Client *, struct Client *, int, const char **);
static int mo_whois(struct Client *, struct Client *, int, const char **);

struct Message whois_msgtab = {
	"WHOIS", 0, 0, 0, 0, MFLG_SLOW, 0L,
	{m_unregistered, m_whois, ms_whois, mo_whois}
};

int doing_whois_local_hook;
int doing_whois_global_hook;

mapi_clist_av1 whois_clist[] = { &whois_msgtab, NULL };
mapi_hlist_av1 whois_hlist[] = {
	{ "doing_whois_local",	&doing_whois_local_hook },
	{ "doing_whois_global",	&doing_whois_global_hook },
	{ NULL }
};

DECLARE_MODULE_AV1(whois, NULL, NULL, whois_clist, whois_hlist, NULL, "$Revision$");

/*
 * m_whois
 *      parv[0] = sender prefix
 *      parv[1] = nickname masklist
 */
static int
m_whois(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	static time_t last_used = 0;

	if(parc < 2 || EmptyString(parv[1]))
	{
		sendto_one(source_p, form_str(ERR_NONICKNAMEGIVEN), me.name, source_p->name);
		return 0;
	}

	if(parc > 2)
	{
		/* seeing as this is going across servers, we should limit it */
		if((last_used + ConfigFileEntry.pace_wait_simple) > CurrentTime)
		{
			sendto_one(source_p, form_str(RPL_LOAD2HI),
				   me.name, source_p->name, "WHOIS");
			return 0;
		}
		else
			last_used = CurrentTime;

		if(hunt_server(client_p, source_p, ":%s WHOIS %s :%s", 1, parc, parv) !=
		   HUNTED_ISME)
		{
			return 0;
		}
		parv[1] = parv[2];

	}
	do_whois(client_p, source_p, parc, parv);

	return 0;
}

/*
 * mo_whois
 *      parv[0] = sender prefix
 *      parv[1] = nickname masklist
 */
static int
mo_whois(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(parc < 2 || EmptyString(parv[1]))
	{
		sendto_one(source_p, form_str(ERR_NONICKNAMEGIVEN), me.name, source_p->name);
		return 0;
	}

	if(parc > 2)
	{
		if(hunt_server(client_p, source_p, ":%s WHOIS %s :%s", 1, parc, parv) !=
		   HUNTED_ISME)
		{
			return 0;
		}
		parv[1] = parv[2];
	}

	do_whois(client_p, source_p, parc, parv);

	return 0;
}

/*
 * ms_whois
 *      parv[0] = sender prefix
 *      parv[1] = server to reply
 *      parv[2] = nickname to whois
 */
static int
ms_whois(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;

	if(parc < 3 || EmptyString(parv[2]))
	{
		sendto_one(source_p, form_str(ERR_NONICKNAMEGIVEN), 
			   get_id(&me, source_p), get_id(source_p, source_p));
		return 0;
	}

	if(!IsClient(source_p))
		return 0;

	/* check if parv[1] exists */
	if((target_p = find_client(parv[1])) == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHSERVER,
				   form_str(ERR_NOSUCHSERVER), parv[1]);
		return 0;
	}

	/* if parv[1] isnt my client, or me, someone else is supposed
	 * to be handling the request.. so send it to them 
	 */
	if(!MyClient(target_p) && !IsMe(target_p))
	{
		sendto_one(target_p, ":%s WHOIS %s :%s", 
			   get_id(source_p, target_p), 
			   get_id(target_p, target_p), parv[2]);
		return 0;
	}

	/* ok, the target is either us, or a client on our server, so perform the whois
	 * but first, parv[1] == server to perform the whois on, parv[2] == person
	 * to whois, so make parv[1] = parv[2] so do_whois is ok -- fl_
	 */
	parv[1] = parv[2];
	do_whois(client_p, source_p, parc, parv);

	return 0;
}

/* do_whois
 *
 * inputs	- pointer to 
 * output	- 
 * side effects -
 */
static void
do_whois(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	char *nick;
	char *p = NULL;

	nick = LOCAL_COPY(parv[1]);
	if((p = strchr(parv[1], ',')))
		*p = '\0';

	if((target_p = find_client(nick)) != NULL && IsPerson(target_p))
		single_whois(source_p, target_p);
	else
		sendto_one_numeric(source_p, ERR_NOSUCHNICK,
				   form_str(ERR_NOSUCHNICK), nick);

	sendto_one_numeric(source_p, RPL_ENDOFWHOIS, 
			   form_str(RPL_ENDOFWHOIS), parv[1]);
	return;
}

/*
 * single_whois()
 *
 * Inputs	- source_p client to report to
 *		- target_p client to report on
 * Output	- if found return 1
 * Side Effects	- do a single whois on given client
 * 		  writing results to source_p
 */
static void
single_whois(struct Client *source_p, struct Client *target_p)
{
	char buf[BUFSIZE];
	dlink_node *ptr;
	struct Client *a2client_p;
	struct membership *msptr;
	struct Channel *chptr;
	int cur_len = 0;
	int mlen;
	char *t;
	int tlen;
	struct hook_mfunc_data hd;
	char *name;
	char quest[] = "?";

	if(target_p->name[0] == '\0')
		name = quest;
	else
		name = target_p->name;

	if(target_p->user == NULL)
	{
		s_assert(0);
		return;
	}

	a2client_p = find_server(target_p->user->server);

	sendto_one_numeric(source_p, RPL_WHOISUSER, form_str(RPL_WHOISUSER),
			   target_p->name, target_p->username, 
			   target_p->host, target_p->info);

	cur_len = mlen = ircsprintf(buf, form_str(RPL_WHOISCHANNELS), 
				    get_id(&me, source_p), get_id(source_p, source_p), 
				    target_p->name);

	t = buf + mlen;

	DLINK_FOREACH(ptr, target_p->user->channel.head)
	{
		msptr = ptr->data;
		chptr = msptr->chptr;

		if(ShowChannel(source_p, chptr))
		{
			if((cur_len + strlen(chptr->chname) + 3) > (BUFSIZE - 4))
			{
				sendto_one(source_p, "%s", buf);
				cur_len = mlen;
				t = buf + mlen;
			}

			tlen = ircsprintf(t, "%s%s ", 
					  find_channel_status(msptr, 1),
					  chptr->chname);
			t += tlen;
			cur_len += tlen;
		}
	}

	if(cur_len > mlen)
		sendto_one(source_p, "%s", buf);

	sendto_one_numeric(source_p, RPL_WHOISSERVER, form_str(RPL_WHOISSERVER),
			   target_p->name, target_p->user->server,
			   a2client_p ? a2client_p->info : "*Not On This Net*");

	if(target_p->user->away)
		sendto_one_numeric(source_p, RPL_AWAY, form_str(RPL_AWAY),
				   target_p->name, target_p->user->away);

	if(IsOper(target_p))
	{
		sendto_one_numeric(source_p, RPL_WHOISOPERATOR, form_str(RPL_WHOISOPERATOR),
				   target_p->name,
				   IsAdmin(target_p) ? GlobalSetOptions.adminstring :
				    GlobalSetOptions.operstring);
	}

	if(MyClient(target_p))
	{
		if(ConfigFileEntry.use_whois_actually && show_ip(source_p, target_p))
			sendto_one_numeric(source_p, RPL_WHOISACTUALLY,
					   form_str(RPL_WHOISACTUALLY),
					   target_p->name, target_p->sockhost);

		sendto_one_numeric(source_p, RPL_WHOISIDLE, form_str(RPL_WHOISIDLE),
				   target_p->name, 
				   CurrentTime - target_p->user->last, 
				   target_p->firsttime);
	}

	hd.client_p = target_p;
	hd.source_p = source_p;

	/* although we should fill in parc and parv, we don't ..
	 * be careful of this when writing whois hooks
	 */
	if(MyClient(source_p))
		hook_call_event(doing_whois_local_hook, &hd);
	else
		hook_call_event(doing_whois_global_hook, &hd);

	return;
}

