/*
 *  ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 *  m_cmessage.c: Handles CPRIVMSG/CNOTICE, target change limitation free
 *                PRIVMSG/NOTICE implementations.
 *
 *  Copyright (C) 2005 Lee Hardy <lee -at- leeh.co.uk>
 *  Copyright (C) 2005 ircd-ratbox development team
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *  1.Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  2.Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  3.The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 *  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  $Id$
 */
#include "stdinc.h"
#include "client.h"
#include "channel.h"
#include "numeric.h"
#include "msg.h"
#include "modules.h"
#include "hash.h"
#include "send.h"

static int m_cmessage(int, const char *, struct Client *, struct Client *, int, const char **);
static int m_cprivmsg(struct Client *, struct Client *, int, const char **);
static int m_cnotice(struct Client *, struct Client *, int, const char **);

struct Message cprivmsg_msgtab = {
	"CPRIVMSG", 0, 0, 0, MFLG_SLOW,
	{mg_ignore, {m_cprivmsg, 4}, mg_ignore, mg_ignore, mg_ignore, {m_cprivmsg, 4}}
};
struct Message cnotice_msgtab = {
	"CNOTICE", 0, 0, 0, MFLG_SLOW,
	{mg_ignore, {m_cnotice, 4}, mg_ignore, mg_ignore, mg_ignore, {m_cnotice, 4}}
};

mapi_clist_av1 cmessage_clist[] = { &cprivmsg_msgtab, &cnotice_msgtab, NULL };
DECLARE_MODULE_AV1(cmessage, NULL, NULL, cmessage_clist, NULL, NULL, "$Revision$");

#define PRIVMSG 0
#define NOTICE 0

static int
m_cprivmsg(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	return m_cmessage(PRIVMSG, "PRIVMSG", client_p, source_p, parc, parv);
}

static int
m_cnotice(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	return m_cmessage(NOTICE, "NOTICE", client_p, source_p, parc, parv);
}

static int
m_cmessage(int p_or_n, const char *command,
		struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	struct Channel *chptr;
	struct membership *msptr;

	if((target_p = find_named_person(parv[1])) == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHNICK,
					form_str(ERR_NOSUCHNICK), parv[1]);
		return 0;
	}

	if((chptr = find_channel(parv[2])) == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL,
					form_str(ERR_NOSUCHCHANNEL), parv[2]);
		return 0;
	}

	if((msptr = find_channel_membership(chptr, source_p)) == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOTONCHANNEL,
					form_str(ERR_NOTONCHANNEL), 
					chptr->chname);
		return 0;
	}

	if(!is_chanop_voiced(msptr))
	{
		sendto_one(source_p, form_str(ERR_VOICENEEDED),
				me.name, source_p->name, chptr->chname);
		return 0;
	}

	if(!IsMember(target_p, chptr))
	{
		sendto_one_numeric(source_p, ERR_USERNOTINCHANNEL,
					form_str(ERR_USERNOTINCHANNEL),
					target_p->name, chptr->chname);
		return 0;
	}

	sendto_anywhere(target_p, source_p, command, ":%s", parv[3]);
	return 0;
}
