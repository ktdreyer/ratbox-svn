/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_invite.c
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
#include "common.h"
#include "channel.h"
#include "list.h"
#include "vchannel.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "s_serv.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

static void m_invite(struct Client*, struct Client*, int, char**);
static void ms_invite(struct Client*, struct Client*, int, char**);

struct Message invite_msgtab = {
  "INVITE", 0, 3, 0, MFLG_SLOW, 0,
  {m_unregistered, m_invite, ms_invite, m_invite}
};
#ifndef STATIC_MODULES

void
_modinit(void)
{
  mod_add_cmd(&invite_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&invite_msgtab);
}

char *_version = "20001122";
#endif
/*
** m_invite
**      parv[0] - sender prefix
**      parv[1] - user to invite
**      parv[2] - channel number
*/
static void m_invite(struct Client *client_p,
                    struct Client *source_p,
                    int parc,
                    char *parv[])
{
  struct Client *target_p;
  struct Channel *chptr;
  struct Channel *vchan;
  char   *chname;
  int    chop;			/* Is channel op */

  if (*parv[2] == '\0')
    {
      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "INVITE");
      return;
    }

  /* A little sanity test here */
  if(!source_p->user)
    return;

  if (!(target_p = find_person(parv[1], (struct Client *)NULL)))
    {
      sendto_one(source_p, form_str(ERR_NOSUCHNICK),
                 me.name, parv[0], parv[1]);
      return;
    }

  if (!check_channel_name(parv[2]))
    { 
      sendto_one(source_p, form_str(ERR_BADCHANNAME),
                 me.name, parv[0], (unsigned char *)parv[2]);
      return;
    }

  if (!IsChannelName(parv[2]))
    {
      if (MyClient(source_p))
        sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return;
    }

  /* Do not send local channel invites to users if they are not on the
   * same server as the person sending the INVITE message. 
   */
  /* Possibly should be an error sent to source_p */
  /* done .. there should be no problem because MyConnect(source_p) should
   * always be true if parse() and such is working correctly --is
   */

  if (!MyConnect(target_p) && (parv[2][0] == '&'))
    {
      sendto_one(source_p, form_str(ERR_USERNOTONSERV),
		 me.name, parv[0], parv[1]);
      return;
    }
	  
  if (!(chptr = hash_find_channel(parv[2], NullChn)))
    {
      if (MyClient(source_p))
        sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return;
    }

  /* By this point, chptr is non NULL */  

  if (HasVchans(chptr))
    {
      if (map_vchan(chptr,target_p))
	{
	  if (MyClient(source_p))
	    sendto_one(source_p, form_str(ERR_USERONCHANNEL),
		       me.name, parv[0], parv[1], parv[2]);
	  return;
	}

      if ((vchan = map_vchan(chptr,source_p)))
	chptr = vchan;
    }

  chname = chptr->chname;

  if (!IsMember(source_p, chptr))
    {
      if (MyClient(source_p))
        sendto_one(source_p, form_str(ERR_NOTONCHANNEL),
                   me.name, parv[0], parv[2]);
      return;
    }

  if (IsMember(target_p, chptr))
    {
      if (MyClient(source_p))
        sendto_one(source_p, form_str(ERR_USERONCHANNEL),
                   me.name, parv[0], parv[1], parv[2]);
      return;
    }

  chop = is_chan_op(chptr, source_p);

  if (chptr && (chptr->mode.mode & MODE_INVITEONLY))
    {
      if (!chop)
        {
          if (MyClient(source_p))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, parv[0], parv[2]);
          return;
        }
    }

  if (MyConnect(source_p))
    {
      sendto_one(source_p, form_str(RPL_INVITING), me.name, parv[0],
                 target_p->name, ((chname) ? (chname) : parv[2]));
      if (target_p->user->away)
        sendto_one(source_p, form_str(RPL_AWAY), me.name, parv[0],
                   target_p->name, target_p->user->away);
    }

  if(MyConnect(target_p) && chop)
    add_invite(chptr, target_p);

  
  if(!MyConnect(target_p) && ServerInfo.hub &&
     IsCapable(target_p->from, CAP_LL))
  {
    /* target_p is connected to a LL leaf, connected to us */
    if(IsPerson(source_p))
      client_burst_if_needed(target_p->from, source_p);

    if ( (chptr->lazyLinkChannelExists &
          target_p->from->localClient->serverMask) == 0 )
      burst_channel( target_p->from, chptr );
  }

  sendto_anywhere(target_p, source_p, "INVITE %s :%s",
		  target_p->name, parv[2]);
}

/*
** ms_invite
**      parv[0] - sender prefix
**      parv[1] - user to invite
**      parv[2] - channel number
*/
static void ms_invite(struct Client *client_p,
                     struct Client *source_p,
                     int parc,
                     char *parv[])
{
  m_invite(client_p,source_p,parc,parv);
}
