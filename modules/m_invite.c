/************************************************************************
 *   IRC - Internet Relay Chat, src/m_invite.c
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
#include "msg.h"

struct Message invite_msgtab = {
  MSG_INVITE, 0, 2, MFLG_SLOW, 0,
  {m_unregistered, m_invite, ms_invite, m_invite}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_INVITE, &invite_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_INVITE);
}

char *_version = "20001122";

/*
** m_invite
**      parv[0] - sender prefix
**      parv[1] - user to invite
**      parv[2] - channel number
*/
int     m_invite(struct Client *cptr,
                 struct Client *sptr,
                 int parc,
                 char *parv[])
{
  struct Client *acptr;
  struct Channel *chptr;
  struct Channel *vchan;
  int need_invite=NO;

  /* A little sanity test here */
  if(!sptr->user)
    return 0;

  if (!(acptr = find_person(parv[1], (struct Client *)NULL)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                 me.name, parv[0], parv[1]);
      return 0;
    }

  if (!check_channel_name(parv[2]))
    { 
      sendto_one(sptr, form_str(ERR_BADCHANNAME),
                 me.name, parv[0], (unsigned char *)parv[2]);
      return 0;
    }

  if (!IsChannelName(parv[2]))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  /* Do not send local channel invites to users if they are not on the
   * same server as the person sending the INVITE message. 
   */
  /* Possibly should be an error sent to sptr */
  /* done .. there should be no problem because MyConnect(sptr) should
	 always be true if parse() and such is working correctly --is */
  if (!MyConnect(acptr) && (parv[2][0] == '&')) {
	  sendto_one(sptr, form_str(ERR_USERNOTONSERV),
				 me.name, parv[0], parv[1]);
	  return 0;
  }
	  
  if (!(chptr = hash_find_channel(parv[2], NullChn)))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  /* By this point, chptr is non NULL */  

  if (HasVchans(chptr))
    {
      if (map_vchan(chptr,acptr))
	{
	  if (MyClient(sptr))
	    sendto_one(sptr, form_str(ERR_USERONCHANNEL),
		       me.name, parv[0], parv[1], parv[2]);
	  return 0;
	}

      if ((vchan = map_vchan(chptr,sptr)))
	chptr = vchan;
    }

  if (!IsMember(sptr, chptr))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOTONCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  if (IsMember(acptr, chptr))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_USERONCHANNEL),
                   me.name, parv[0], parv[1], parv[2]);
      return 0;
    }

  if (chptr && (chptr->mode.mode & MODE_INVITEONLY))
    {
      need_invite = YES;

      if (!is_chan_op(chptr, sptr))
        {
          if (MyClient(sptr))
            sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, parv[0], parv[2]);
          return -1;
        }
    }

  /*
   * due to some whining I've taken out the need for the channel
   * being +i before sending an INVITE. It was intentionally done this
   * way, it makes no sense (to me at least) letting the server send
   * an unnecessary invite when a channel isn't +i !
   * bah. I can't be bothered arguing it
   * -Dianora
   * this is now settable in the config file  --is 
   */
  if (MyConnect(sptr) && (ConfigFileEntry.invite_plus_i_only && need_invite))
    {
      sendto_one(sptr, form_str(RPL_INVITING), me.name, parv[0],
                 acptr->name, ((chptr) ? (chptr->chname) : parv[2]));
      if (acptr->user->away)
        sendto_one(sptr, form_str(RPL_AWAY), me.name, parv[0],
                   acptr->name, acptr->user->away);
      
      if( need_invite )
        {
          /* Send a NOTICE to all channel operators concerning chanops who  *
           * INVITE other users to the channel when it is invite-only (+i). *
           * The NOTICE is sent from the local server.                      */

          /* Only allow this invite notice if the channel is +p
           * i.e. "paranoid"
           * -Dianora
           */

          if (chptr && (chptr->mode.mode & MODE_PRIVATE))
            { 
              char message[NICKLEN*2+CHANNELLEN+USERLEN+HOSTLEN+30];

              /* bit of paranoia, be a shame if it cored for this -Dianora */
              if(acptr->user)
                {
                  ircsprintf(message,
                             "INVITE: %s (%s invited %s [%s@%s])",
                             chptr->chname,
                             sptr->name,
                             acptr->name,
                             acptr->username,
                             acptr->host);

		  /* XXX bchan needed for vchans */
                  sendto_channel_type(cptr, sptr,
				      &chptr->chanops,
                                      '@',
                                      chptr->chname,
                                      "PRIVMSG",
                                      message);
                }
            }
        }
    }

  if(MyConnect(acptr) && need_invite)
    add_invite(chptr, acptr);

  if (!ConfigFileEntry.invite_plus_i_only || 
	  (ConfigFileEntry.invite_plus_i_only && need_invite)) 
	  sendto_prefix_one(acptr, sptr, ":%s INVITE %s :%s",
						parv[0], acptr->name, parv[2]);
  return 0;
}

/*
** ms_invite
**      parv[0] - sender prefix
**      parv[1] - user to invite
**      parv[2] - channel number
*/
int     ms_invite(struct Client *cptr,
                 struct Client *sptr,
                 int parc,
                 char *parv[])
{
  /* XXX Consider just mapping this to m_invite in parse.c -db */
  return (m_invite(cptr,sptr,parc,parv));
  /* NOT REACHED */
  return 0;
}

