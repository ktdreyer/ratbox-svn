/************************************************************************
 *   IRC - Internet Relay Chat, src/m_topic.c
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
  {m_unregistered, m_topic, ms_topic, m_topic}
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
  char  *chname;
  char  *topic = (char *)NULL, *name, *p = (char *)NULL;
  
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  /* multi channel topic's are now known to be used by cloners
   * trying to flood off servers.. so disable it *sigh* - Dianora
   */

  if (name && IsChannelName(name))
    {
      chptr = hash_find_channel(name, NullChn);
      if (!chptr)
        {
          sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      chname = chptr->chname;
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
	      is_chan_op(chptr,sptr))
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
	      
	      sendto_match_servs(chptr, cptr,":%s TOPIC %s :%s",
				 parv[0], chptr->chname,
				 chptr->topic);
	      if(GlobalSetOptions.hide_chanops)
		{
		  sendto_channel_butserv(ONLY_CHANOPS,
					 chptr, sptr, ":%s TOPIC %s :%s",
					 parv[0],
					 chname, chptr->topic);
		}
	      else
		{
		  sendto_channel_butserv(ALL_MEMBERS,
					 chptr, sptr, ":%s TOPIC %s :%s",
					 parv[0],
					 chname, chptr->topic);
		}
	    }
	  else
            sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, parv[0], chptr->chname);
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
		       me.name, parv[0], chptr->chname);
          else
	    {
              sendto_one(sptr, form_str(RPL_TOPIC),
                         me.name, parv[0],
                         chptr->chname, chptr->topic);
              sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
                         me.name, parv[0], chname,
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

int     ms_topic(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Channel *chptr = NullChn;
  struct Channel *vchan;
  char  *chname;
  char  *topic = (char *)NULL, *name, *p = (char *)NULL;
  
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  /* multi channel topic's are now known to be used by cloners
   * trying to flood off servers.. so disable it *sigh* - Dianora
   */

  if (name && IsChannelName(name))
    {
      chptr = hash_find_channel(name, NullChn);
      if (!chptr)
        {
          sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      chname = chptr->chname;
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
	      is_chan_op(chptr,sptr))
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
		  chptr->topic_info = MyMalloc(strlen(sptr->name) + 1);
		  strncpy_irc(chptr->topic_info, sptr->name, NICKLEN);
		}
	      
	      chptr->topic_time = CurrentTime;
	      
	      sendto_match_servs(chptr, cptr,":%s TOPIC %s :%s",
				 parv[0], chptr->chname,
				 chptr->topic);
	      if(GlobalSetOptions.hide_chanops)
		{
		  sendto_channel_butserv(ONLY_CHANOPS,
					 chptr, sptr, ":%s TOPIC %s :%s",
					 parv[0],
					 chptr->chname, chptr->topic);
		}
	      else
		{
		  sendto_channel_butserv(ALL_MEMBERS,
					 chptr, sptr, ":%s TOPIC %s :%s",
					 parv[0],
					 chptr->chname, chptr->topic);
		}

	    }
	  else
            sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, parv[0], chptr->chname);
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
		       me.name, parv[0], chptr->chname);
          else
	    {
              sendto_one(sptr, form_str(RPL_TOPIC),
                         me.name, parv[0],
                         chptr->chname, chptr->topic);
              sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
                         me.name, parv[0], chptr->chname,
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
