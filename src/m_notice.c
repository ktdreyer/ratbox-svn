/************************************************************************
 *   IRC - Internet Relay Chat, src/m_notice.c
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
#include "client.h"
#include "flud.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"

#include "channel.h"
#include "vchannel.h"
#include "irc_string.h"
#include "m_privmsg.h"
#include "hash.h"
#include "class.h"

#include <string.h>

/*
 * m_functions execute protocol messages on this server:
 *
 *      cptr    is always NON-NULL, pointing to a *LOCAL* client
 *              structure (with an open socket connected!). This
 *              identifies the physical socket where the message
 *              originated (or which caused the m_function to be
 *              executed--some m_functions may call others...).
 *
 *      sptr    is the source of the message, defined by the
 *              prefix part of the message if present. If not
 *              or prefix not found, then sptr==cptr.
 *
 *              (!IsServer(cptr)) => (cptr == sptr), because
 *              prefixes are taken *only* from servers...
 *
 *              (IsServer(cptr))
 *                      (sptr == cptr) => the message didn't
 *                      have the prefix.
 *
 *                      (sptr != cptr && IsServer(sptr) means
 *                      the prefix specified servername. (?)
 *
 *                      (sptr != cptr && !IsServer(sptr) means
 *                      that message originated from a remote
 *                      user (not local).
 *
 *              combining
 *
 *              (!IsServer(sptr)) means that, sptr can safely
 *              taken as defining the target structure of the
 *              message in this server.
 *
 *      *Always* true (if 'parse' and others are working correct):
 *
 *      1)      sptr->from == cptr  (note: cptr->from == cptr)
 *
 *      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
 *              *cannot* be a local connection, unless it's
 *              actually cptr!). [MyConnect(x) should probably
 *              be defined as (x == x->from) --msa ]
 *
 *      parc    number of variable parameter strings (if zero,
 *              parv is allowed to be NULL)
 *
 *      parv    a NULL terminated list of parameter pointers,
 *
 *                      parv[0], sender (prefix string), if not present
 *                              this points to an empty string.
 *                      parv[1]...parv[parc-1]
 *                              pointers to additional parameters
 *                      parv[parc] == NULL, *always*
 *
 *              note:   it is guaranteed that parv[0]..parv[parc-1] are all
 *                      non-NULL pointers.
 */

struct entity target_table[MAX_MULTI_MESSAGES];

static void notice_channel( struct Client *cptr,
			    struct Client *sptr,
			    struct Channel *chptr,
			    char *text);

static void notice_channel_flags( struct Client *cptr,
				   struct Client *sptr,
				   struct Channel *chptr,
				   int flags,
				   char *text);

/*
** m_notice
**
**      parv[0] = sender prefix
**      parv[1] = receiver list
**      parv[2] = message text
**
** massive cleanup
** rev argv 6/91
**
*/

static void notice_client(struct Client *sptr, struct Client *acptr,
			  char *text);


int     m_notice(struct Client *cptr,
                          struct Client *sptr,
                          int parc,
                          char *parv[])
{
  int i;
  int ntargets;

  if (!IsPerson(sptr))
    return 0;

#ifndef ANTI_SPAMBOT_WARN_ONLY
  /* if its a spambot, just ignore it */
  if(sptr->join_leave_count >= MAX_JOIN_LEAVE_COUNT)
    return 0;
#endif

  /* notice gives different errors, so still check this */
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NORECIPIENT),
                 me.name, parv[0], "NOTICE");
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  ntargets = build_target_list(cptr,sptr,parv[1],target_table,parv[2]);

  for(i = 0; i < ntargets ; i++)
    {
      switch (target_table[i].type)
	{
	case ENTITY_CHANNEL:
	  notice_channel(cptr,sptr,
			 (struct Channel *)target_table[i].ptr,
			 parv[2]);
	  break;

	case ENTITY_CHANOPS_ON_CHANNEL:
	  notice_channel_flags(cptr,sptr,
			       (struct Channel *)target_table[i].ptr,
			       target_table[i].flags,parv[2]);
	  break;

	case ENTITY_CLIENT:
	  notice_client(sptr,(struct Client *)target_table[i].ptr,parv[2]);
	  break;
	}
    }

  if(ntargets == 0)
    sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name,
	       parv[0], parv[1]);
  return 1;
}

/*
 * notice_channel
 *
 * inputs	- pointer to cptr
 *		- pointer to sptr
 *		- pointer to channel
 * output	- YES if duplicate pointer in table, NO if not.
 *		  note, this is canonilization
 * side effects	- message given channel
 */
static void notice_channel( struct Client *cptr,
			     struct Client *sptr,
			     struct Channel *chptr,
			     char *text)
{
  struct Channel *vchan;
  char *channel_name=NULL;

  if (HasVchans(chptr))
    {
      if( (vchan = map_vchan(chptr,sptr)) )
	{
	  channel_name = chptr->chname;
	  chptr = vchan;
	}
    }
  else
    channel_name = chptr->chname;

  if(MyClient(sptr) && sptr->user)
    sptr->user->last = CurrentTime;

  if(check_for_ctcp(text))
    check_for_flud(sptr, NULL, chptr, 1);

  if (can_send(chptr, sptr) == 0)
    sendto_channel_butone(cptr, sptr, chptr,
			  ":%s %s %s :%s",
			  sptr->name, "NOTICE", channel_name, text);
  else
    sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
	       me.name, sptr->name, channel_name);
}

/*
 * notice_channel_flags
 *
 * inputs	- pointer to cptr
 *		- pointer to sptr
 *		- pointer to channel
 *		- pointer to text to send
 * output	- NONE
 * side effects	- message given channel either chanop or voice
 */
static void notice_channel_flags( struct Client *cptr,
				   struct Client *sptr,
				   struct Channel *chptr,
				   int flags,
				   char *text)
{
  struct Channel *vchan;
  char *channel_name=NULL;

  if (HasVchans(chptr))
    {
      if( (vchan = map_vchan(chptr,sptr)) )
	{
	  channel_name = chptr->chname;
	  chptr = vchan;
	}
    }
  else
    channel_name = chptr->chname;

  if(MyClient(sptr) && sptr->user)
    sptr->user->last = CurrentTime;

  if(check_for_ctcp(text))
    check_for_flud(sptr, NULL, chptr, 1);

  if (can_send(chptr, sptr) == 0)
    sendto_channel_type(cptr,
			sptr,
			chptr,
			flags,
			channel_name,
			"NOTICE",
			text);
  else
    sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
	       me.name, sptr->name, channel_name);
}

/*
 * notice_client
 *
 * inputs	- pointer to sptr
 *		- pointer to acptr (struct Client *)
 *		- pointer to text
 * output	- NONE
 * side effects	- message given channel either chanop or voice
 */
static void notice_client(struct Client *sptr, struct Client *acptr,
			  char *text)
{
  /* reset idle time for message only if its not to self */
  if (sptr != acptr)
    {
      if(sptr->user)
	sptr->user->last = CurrentTime;
    }

  /* reset idle time for message only if target exists */
  if(MyClient(sptr) && sptr->user)
    sptr->user->last = CurrentTime;

  if(check_for_ctcp(text))
    check_for_flud(sptr, acptr, NULL, 1);

  if (MyConnect(sptr) &&
      acptr->user && acptr->user->away)
    sendto_one(sptr, form_str(RPL_AWAY), me.name,
	       sptr->name, acptr->name,
	       acptr->user->away);

  sendto_prefix_one(acptr, sptr, ":%s %s %s :%s",
		    sptr->name, "NOTICE", acptr->name, text);


  return;
}

int     mo_notice(struct Client *cptr,
                          struct Client *sptr,
                          int parc,
                          char *parv[])
{
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NORECIPIENT),
                 me.name, parv[0], "NOTICE");
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  m_notice(cptr,sptr,parc,parv);

  return 0;
}

int     ms_notice(struct Client *cptr,
                          struct Client *sptr,
                          int parc,
                          char *parv[])
{
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NORECIPIENT),
                 me.name, parv[0], "NOTICE");
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  m_notice(cptr,sptr,parc,parv);

  return 0;
}
