/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_kick.c
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
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "msg.h"
#include "modules.h"
#include "parse.h"

#include <string.h>

static int m_kick(struct Client*, struct Client*, int, char**);
static int ms_kick(struct Client*, struct Client*, int, char**);

struct Message kick_msgtab = {
  MSG_KICK, 0, 3, 0, MFLG_SLOW, 0,
  {m_unregistered, m_kick, ms_kick, m_kick}
};

void
_modinit(void)
{
  mod_add_cmd(&kick_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&kick_msgtab);
}

char *_version = "20001122";

/*
** m_kick
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = client to kick
**      parv[3] = kick comment
*/
static int m_kick(struct Client *cptr,
                  struct Client *sptr,
                  int parc,
                  char *parv[])
{
  struct Client *who;
  struct Channel *chptr;
  struct Channel *vchan;
  int   chasing = 0;
  char  *comment;
  char  *name;
  char  *p = (char *)NULL;
  char  *user;
  static char     buf[BUFSIZE];

  if (*parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "KICK");
      return 0;
    }

  comment = (BadPtr(parv[3])) ? parv[0] : parv[3];
  if (strlen(comment) > (size_t) TOPICLEN)
    comment[TOPICLEN] = '\0';

  *buf = '\0';
  if( (p = strchr(parv[1],',')) )
    *p = '\0';

  name = parv[1];

  chptr = get_channel(sptr, name, !CREATE);
  if (!chptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                 me.name, parv[0], name);
      return(0);
    }

  if (HasVchans(chptr))
    {
      vchan = map_vchan(chptr,sptr);
      if(vchan != 0)
	{
	  chptr = vchan;
	}
    }

  if (!IsServer(sptr) && !is_any_op(chptr, sptr) ) 
    { 
      /* was a user, not a server, and user isn't seen as a chanop here */
      
      if(MyConnect(sptr))
        {
          /* user on _my_ server, with no chanops.. so go away */
          
          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], name);
          return(0);
        }

      if(chptr->channelts == 0)
        {
          /* If its a TS 0 channel, do it the old way */
          
          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], name);
          return(0);
        }

      /* Its a user doing a kick, but is not showing as chanop locally
       * its also not a user ON -my- server, and the channel has a TS.
       * There are two cases we can get to this point then...
       *
       *     1) connect burst is happening, and for some reason a legit
       *        op has sent a KICK, but the SJOIN hasn't happened yet or 
       *        been seen. (who knows.. due to lag...)
       *
       *     2) The channel is desynced. That can STILL happen with TS
       *        
       *     Now, the old code roger wrote, would allow the KICK to 
       *     go through. Thats quite legit, but lets weird things like
       *     KICKS by users who appear not to be chanopped happen,
       *     or even neater, they appear not to be on the channel.
       *     This fits every definition of a desync, doesn't it? ;-)
       *     So I will allow the KICK, otherwise, things are MUCH worse.
       *     But I will warn it as a possible desync.
       *
       *     -Dianora
       */
    }

  if( (p = strchr(parv[2],',')) )
    *p = '\0';

  user = parv[2]; /* strtoken(&p2, parv[2], ","); */

  if (!(who = find_chasing(sptr, user, &chasing)))
    {
      return(0);
    }

  if (IsMember(who, chptr))
    {
      /* half ops cannot kick full chanops */
      if (is_half_op(chptr,sptr) && is_chan_op(chptr,who))
	{
	  sendto_one(who, ":%s NOTICE %s half-op %s tried to kick you",
		     me.name, who->name, sptr->name);

          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], name);
	  return 0;
	}

      if(chptr->mode.mode & MODE_HIDEOPS)
	{
	  sendto_channel_local(NON_CHANOPS, chptr,
			       ":%s KICK %s %s :%s", 
			       who->name,
			       name, who->name, comment);

	  sendto_channel_local(ONLY_CHANOPS, chptr,
			       ":%s!%s@%s KICK %s %s :%s",
			       sptr->name,
			       sptr->username,
			       sptr->host,
			       name,
			       who->name, comment);
	}
      else
	{
	  sendto_channel_local(ALL_MEMBERS, chptr,
			       ":%s!%s@%s KICK %s %s :%s",
			       sptr->name,
			       sptr->username,
			       sptr->host,
			       name, who->name, comment);
	}

      sendto_channel_remote(chptr, cptr,
			    ":%s KICK %s %s :%s",
			    parv[0], chptr->chname,
			    who->name, comment);
      remove_user_from_channel(chptr, who);
    }
  else
    sendto_one(sptr, form_str(ERR_USERNOTINCHANNEL),
               me.name, parv[0], user, name);

  return (0);
}

static int ms_kick(struct Client *cptr,
                   struct Client *sptr,
                   int parc,
                   char *parv[])
{
  if (*parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "KICK");
      return 0;
    }

  return (m_kick(cptr, sptr, parc, parv));
}
