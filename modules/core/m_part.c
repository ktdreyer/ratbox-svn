/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_part.c
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
#include "common.h"   /* bleah */
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "send.h"
#include "s_serv.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void m_part(struct Client*, struct Client*, int, char**);
static void ms_part(struct Client*, struct Client*, int, char**);
static void mo_part(struct Client *, struct Client *, int, char **);

struct Message part_msgtab = {
  "PART", 1, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_part, ms_part, mo_part}
};

void
_modinit(void)
{
  mod_add_cmd(&part_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&part_msgtab);
}

static void part_one_client(struct Client *client_p,
			    struct Client *source_p,
			    char *name, char *reason);

char *_version = "20001122";

/*
** m_part
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = reason
*/
static void m_part(struct Client *client_p,
                  struct Client *source_p,
                  int parc,
                  char *parv[])
{
  int t_delta;
  int decrement_count;
  char  *p, *name;
  char reason[TOPICLEN+1];

  if (*parv[1] == '\0')
    {
      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "PART");
      return;
    }

  reason[0] = '\0';

  if (parc > 2)
    strncpy_irc(reason, parv[2], TOPICLEN);

  name = strtoken( &p, parv[1], ",");

  /* if its my client, and isn't an oper */

  if (name)
    {
      if(GlobalSetOptions.spam_num &&
	 (source_p->localClient->join_leave_count >= GlobalSetOptions.spam_num))
	{
	  sendto_realops_flags(FLAGS_BOTS,
			       "User %s (%s@%s) is a possible spambot",
			       source_p->name,
			       source_p->username, source_p->host);
	  source_p->localClient->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
	}
      else
	{
	  if( (t_delta = (CurrentTime - source_p->localClient->last_leave_time)) >
	      JOIN_LEAVE_COUNT_EXPIRE_TIME)
	    {
	      decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);
	      
	      if(decrement_count > source_p->localClient->join_leave_count)
		source_p->localClient->join_leave_count = 0;
	      else
		source_p->localClient->join_leave_count -= decrement_count;
	    }
	  else
	    {
	      if( (CurrentTime - (source_p->localClient->last_join_time)) < 
		  GlobalSetOptions.spam_time)
		{
		  source_p->localClient->join_leave_count++;
		}
	    }
	  source_p->localClient->last_leave_time = CurrentTime;
	}
     
      while(name)
	{
	  part_one_client(client_p, source_p, name, reason);
	  name = strtoken(&p, (char *)NULL, ",");
	}
      return;
    }
}

/*
 * part_one_client
 *
 * inputs	- pointer to server
 * 		- pointer to source client to remove
 *		- char pointer of name of channel to remove from
 * output	- none
 * side effects	- remove ONE client given the channel name 
 */
static void part_one_client(struct Client *client_p,
			    struct Client *source_p,
			    char *name,
                            char *reason)
{
  struct Channel *chptr;
  struct Channel *bchan;

  chptr = get_channel(source_p, name, 0);
  if (!chptr)
    {
      sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
		 me.name, source_p->name, name);
      return;
    }

  if (IsVchan(chptr) || HasVchans(chptr))
    {
      if(HasVchans(chptr))
        {
          /* Set chptr to actual channel, bchan to the base channel */
          bchan = chptr;
          chptr = map_vchan(bchan,source_p);
        }
      else
        {
          /* chptr = chptr; */
          bchan = find_bchan(chptr);
        }
    }
  else
    bchan = chptr; /* not a vchan */

  if (!chptr || !bchan || !IsMember(source_p, chptr))
    {
      sendto_one(source_p, form_str(ERR_NOTONCHANNEL),
                 me.name, source_p->name, name);
      return;
    }

  /*
   *  Remove user from the old channel (if any)
   *  only allow /part reasons in -m chans
   */
  if (reason[0] && (can_send(chptr, source_p) > 0))
    {
      sendto_channel_remote_prefix(chptr, client_p, source_p, "PART %s :%s",
                              chptr->chname,
                              reason);
      sendto_channel_local(ALL_MEMBERS,
                           chptr, ":%s!%s@%s PART %s :%s",
                           source_p->name,
                           source_p->username,
                           source_p->host,
                           bchan->chname,
                           reason);
    }
  else
    {
      sendto_channel_remote_prefix(chptr, client_p, source_p, "PART %s",
                                   chptr->chname);
      sendto_channel_local(ALL_MEMBERS,
                           chptr, ":%s!%s@%s PART %s",
                           source_p->name,
                           source_p->username,
                           source_p->host,
                           bchan->chname);
    }
  remove_user_from_channel(chptr, source_p);
}


/*
 * ms_part
 *
 * same as m_part
 * but no spam checks
 */

static void ms_part(struct Client *client_p,
                   struct Client *source_p,
                   int parc,
                   char *parv[])
{
  char  *p, *name;
  char reason[TOPICLEN+1];

  if (*parv[1] == '\0')
    {
      return;
    }

  reason[0] = '\0';

  if (parc > 2)
    strncpy_irc(reason, parv[2], TOPICLEN);

  name = strtoken( &p, parv[1], ",");

  while(name)
    {
      part_one_client(client_p, source_p, name, reason);
      name = strtoken(&p, (char *)NULL, ",");
    }
}

/*
 * mo_part
 *
 * same as m_part
 * but no spam checks
 */

static void mo_part(struct Client *client_p,
                   struct Client *source_p,
                   int parc,
                   char *parv[])
{
  char  *p, *name;
  char reason[TOPICLEN+1];

  if (*parv[1] == '\0')
    {
      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "PART");
      return;
    }

  reason[0] = '\0';

  if (parc > 2)
    strncpy_irc(reason, parv[2], TOPICLEN);

  name = strtoken( &p, parv[1], ",");

  while ( name )
    {
      part_one_client(client_p, source_p, name, reason);
      name = strtoken(&p, (char *)NULL, ",");
    }
}

