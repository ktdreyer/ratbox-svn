/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_topic.c: Sets a channel topic.
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
#include "handlers.h"
#include "channel.h"
#include "channel_mode.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "s_serv.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "packet.h"

static void m_topic(struct Client *, struct Client *, int, char **);
static void ms_topic(struct Client *, struct Client *, int, char **);

struct Message topic_msgtab = {
	"TOPIC", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_topic, ms_topic, m_topic}
};

#ifndef STATIC_MODULES
mapi_clist_av1 topic_clist[] = { &topic_msgtab, NULL };
DECLARE_MODULE_AV1(NULL, NULL, topic_clist, NULL, NULL, "$Revision$");
#endif

/*
 * m_topic
 *      parv[0] = sender prefix
 *      parv[1] = channel name
 *	parv[2] = new topic, if setting topic
 */
static void
m_topic(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	struct Channel *chptr = NULL;
	char *p = NULL;

	if((p = strchr(parv[1], ',')))
		*p = '\0';

	if(MyClient(source_p) && !IsFloodDone(source_p))
		flood_endgrace(source_p);

	if(parv[1] && IsChannelName(parv[1]))
	{
		chptr = hash_find_channel(parv[1]);

		if(chptr == NULL)
		{
			sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
				   me.name, parv[0], parv[1]);
			return;
		}

		/* setting topic */
		if(parc > 2)
		{
			if(!IsMember(source_p, chptr))
			{
				sendto_one(source_p, form_str(ERR_NOTONCHANNEL), me.name, parv[0],
					   parv[1]);
				return;
			}
			if((chptr->mode.mode & MODE_TOPICLIMIT) == 0 || is_chan_op(chptr, source_p))
			{
				char topic_info[USERHOST_REPLYLEN];
				ircsprintf(topic_info, "%s!%s@%s",
					   source_p->name, source_p->username, source_p->host);
				set_channel_topic(chptr, parv[2], topic_info, CurrentTime);

				sendto_server(client_p, chptr, NOCAPS, NOCAPS,
					      ":%s TOPIC %s :%s",
					      parv[0], chptr->chname,
					      chptr->topic == NULL ? "" : chptr->topic);
				sendto_channel_local(ALL_MEMBERS,
						     chptr, ":%s!%s@%s TOPIC %s :%s",
						     source_p->name, source_p->username,
						     source_p->host, chptr->chname,
						     chptr->topic == NULL ? "" : chptr->topic);
			}
			else
				sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
					   me.name, parv[0], parv[1]);
		}
		else		/* only asking  for topic  */
		{
			if(!IsMember(source_p, chptr) && SecretChannel(chptr))
			{
				sendto_one(source_p, form_str(ERR_NOTONCHANNEL), me.name, parv[0],
					   parv[1]);
				return;
			}
			if(chptr->topic == NULL)
				sendto_one(source_p, form_str(RPL_NOTOPIC),
					   me.name, parv[0], parv[1]);
			else
			{
				sendto_one(source_p, form_str(RPL_TOPIC),
					   me.name, parv[0], chptr->chname, chptr->topic);

				sendto_one(source_p, form_str(RPL_TOPICWHOTIME),
					   me.name, parv[0], chptr->chname,
					   chptr->topic_info, chptr->topic_time);
			}
		}
	}
	else
	{
		sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0], parv[1]);
	}
}

/*
 * ms_topic
 *      parv[0] = sender prefix
 *      parv[1] = channel name
 *	parv[2] = topic_info
 *	parv[3] = topic_info time
 *	parv[4] = new channel topic
 *
 * Let servers always set a topic
 */
static void
ms_topic(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	struct Channel *chptr = NULL;

	if(!IsServer(source_p))
	{
		m_topic(client_p, source_p, parc, parv);
		return;
	}

	if(parc < 5)
		return;

	if(parv[1] && IsChannelName(parv[1]))
	{
		if((chptr = hash_find_channel(parv[1])) == NULL)
			return;

		set_channel_topic(chptr, parv[4], parv[2], atoi(parv[3]));

		if(ConfigServerHide.hide_servers)
		{
			sendto_channel_local(ALL_MEMBERS,
					     chptr, ":%s TOPIC %s :%s",
					     me.name,
					     parv[1], chptr->topic == NULL ? "" : chptr->topic);

		}
		else
		{
			sendto_channel_local(ALL_MEMBERS,
					     chptr, ":%s TOPIC %s :%s",
					     source_p->name,
					     parv[1], chptr->topic == NULL ? "" : chptr->topic);
		}
	}
}
