/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_topic.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers. 
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   $Id$
 */
#include "tools.h"
#include "handlers.h"
#include "channel.h"
#include "vchannel.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "msg.h"

#include <string.h>
#include <stdlib.h>

struct Message topic_msgtab = {
  MSG_TOPIC, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_topic, m_topic, m_topic}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_TOPIC, &topic_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_TOPIC);
}

char *_version = "20001122";

/*
** m_topic
**      parv[0] = sender prefix
**      parv[1] = topic text
*/
int     m_topic(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Channel *chptr = NullChn;
  struct Channel *vchan;
  char  *topic = NULL;
  char  *name;
  char  *p = NULL;
  
  if ((p = strchr(parv[1],',')))
    *p = '\0';
  name = parv[1];

  if (name && IsChannelName(name))
    {
      chptr = hash_find_channel(name, NullChn);
      if (!chptr)
        {
          sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      if (HasVchans(chptr))
	{
	  vchan = map_vchan(chptr,sptr);
	  if(vchan != NULL)
	    chptr = vchan;
	}

      if (parc > 2)
	{ /* setting topic */
	  topic = parv[2];

	  if (!IsMember(sptr, chptr))
	    {
	      sendto_one(sptr, form_str(ERR_NOTONCHANNEL), me.name, parv[0],
			 name);
	      return 0;
	    }
	  if ((chptr->mode.mode & MODE_TOPICLIMIT) == 0 ||
	      is_any_op(chptr,sptr))
	    {
	      /* setting a topic */
	      /*
	       * chptr zeroed
	       */
	      strncpy_irc(chptr->topic, topic, TOPICLEN);
	      
              MyFree(chptr->topic_info);
	      
	      if (ConfigFileEntry.topic_uh)
		{
		  chptr->topic_info = 
		    (char *)MyMalloc(strlen(sptr->name)+
				     strlen(sptr->username)+
				     strlen(sptr->host)+3);
		  ircsprintf(chptr->topic_info, "%s!%s@%s",
			     sptr->name, sptr->username, sptr->host);
		}
	      else
		{
		  chptr->topic_info =
		    (char *)MyMalloc(strlen(sptr->name) + 1);
		  strncpy_irc(chptr->topic_info, sptr->name, NICKLEN);
		}

	      chptr->topic_time = CurrentTime;
	      
	      sendto_channel_remote(chptr, cptr,":%s TOPIC %s :%s",
				 parv[0], name,
				 chptr->topic);
	      if(GlobalSetOptions.hide_chanops)
		{
		  sendto_channel_local(ONLY_CHANOPS,
				       chptr, ":%s!%s@%s TOPIC %s :%s",
				       sptr->name,
				       sptr->user,
				       sptr->host,
				       name,
				       chptr->topic);
	  /* XXX could send something to NON_CHANOPS suppressing prefix */
		}
	      else
		{
		  sendto_channel_local(ALL_MEMBERS,
				       chptr, ":%s!%s@%s TOPIC %s :%s",
				       sptr->name,
				       sptr->user,
				       sptr->host,
				       name, chptr->topic);
		}
	    }
	  else
            sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, parv[0], name);
	}
      else  /* only asking  for topic  */
	{
	  if (!IsMember(sptr, chptr) && SecretChannel(chptr))
	    {
	      sendto_one(sptr, form_str(ERR_NOTONCHANNEL), me.name, parv[0],
			 name);
	      return 0;
	    }
          if (chptr->topic[0] == '\0')
	    sendto_one(sptr, form_str(RPL_NOTOPIC),
		       me.name, parv[0], name);
          else
	    {
              sendto_one(sptr, form_str(RPL_TOPIC),
                         me.name, parv[0],
                         name, chptr->topic);
              sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
                         me.name, parv[0], name,
                         chptr->topic_info,
                         chptr->topic_time);
	    }
	}
    }
  else
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                 me.name, parv[0], name);
    }
  
  return 0;
}
