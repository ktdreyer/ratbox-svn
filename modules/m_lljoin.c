/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_lljoin.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Co Center
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
 * $Id$
 */
#include "tools.h"
#include "channel.h"
#include "vchannel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "handlers.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

struct Message lljoin_msgtab = {
  MSG_LLJOIN, 0, 3, 0, MFLG_SLOW | MFLG_UNREG, 0L,
  {m_unregistered, m_error, ms_lljoin, m_error}
};

void
_modinit(void)
{
  mod_add_cmd(&lljoin_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&lljoin_msgtab);
}

char *_version = "20001122";

/*
 * m_lljoin
 *      parv[0] = sender prefix
 *      parv[1] = channel
 *      parv[2] = nick ("!nick" == cjoin)
 *      parv[3] = vchan/key (optional)
 *      parv[4] = key (optional)
 *
 * If a lljoin is received, from our uplink, join
 * the requested client to the given channel, or ignore it
 * if there is an error.
 *
 *   Ok, the way this works. Leaf client tries to join a channel, 
 * it doesn't exist so the join does a cburst request on behalf of the
 * client, and aborts that join. The cburst sjoin's the channel if it
 * exists on the hub, and sends back an LLJOIN to the leaf. Thats where
 * this is now..
 *
 */
int     ms_lljoin(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  char *chname = NULL;
  char *nick = NULL;
  char *key = NULL;
  char *vkey = NULL;
  int  flags;
  int  i;
  struct Client *acptr;
  struct Channel *chptr, *vchan_chptr, *root_vchan;
  int cjoin = 0;

  if(uplink && !IsCapable(uplink,CAP_LL))
    {
      sendto_realops_flags(FLAGS_ALL,
			   "*** LLJOIN requested from non LL server %s",
			   cptr->name);
      return 0;
    }

  chname = parv[1];
  if(chname == NULL)
    return 0;

  nick = parv[2];
  if(nick == NULL)
    return 0;

  if (nick[0] == '!')
  {
    cjoin = 1;
    nick++;
  }
 
  if(parc > 4)
  {
    key = parv[4];
    vkey = parv[3];
  }
  else if(parc >3)
  {
    key = vkey = parv[3];
  }

  flags = 0;

  acptr = hash_find_client(nick,(struct Client *)NULL);

  if( !acptr || !acptr->user )
    return 0;

  if( !MyClient(acptr) )
    return 0;

  chptr = hash_find_channel(chname, NullChn);

  if (cjoin)
  {
    if(!chptr) /* Uhm, bad! */
    {
      sendto_realops_flags(FLAGS_ALL,
        "LLJOIN %s %s called by %s, but root chan doesn't exist!",
        chname, nick, cptr->name);
      return 0;
    }
    flags = CHFL_CHANOP;

    if(! (vchan_chptr = cjoin_channel(chptr, acptr, chname)))
      return 0;

    root_vchan = chptr;
    chptr = vchan_chptr;
  }
  else
  {
    if (chptr)
      vchan_chptr = select_vchan(chptr, cptr, acptr, vkey, chname);
    else
    {
      chptr = vchan_chptr = get_channel( acptr, chname, CREATE );
      flags = CHFL_CHANOP;
    }
    
    if (!chptr)
      return 0;
      
    if (vchan_chptr != chptr)
    {
      root_vchan = chptr;
      chptr = vchan_chptr;
    }
    else
      root_vchan = chptr;

    if (chptr->users == 0)
      flags = CHFL_CHANOP;
    else
      flags = 0;

    /* XXX in m_join.c :( */
    /* check_spambot_warning(acptr, chname); */

    /* They _could_ join a channel twice due to lag */
    if(chptr)
    {
      if (IsMember(acptr, chptr))    /* already a member, ignore this */
        return 0;
    }
    else
    {
      sendto_one(acptr, form_str(ERR_UNAVAILRESOURCE),
                 me.name, nick, root_vchan->chname);
      return 0;
    }

    if( (i = can_join(acptr, chptr, key)) )
    {
      sendto_one(acptr,
                 form_str(i), me.name, nick, root_vchan->chname);
      return 0;
    }
  }

  if ((acptr->user->joined >= MAXCHANNELSPERUSER) &&
      (!IsOper(acptr) || (acptr->user->joined >= MAXCHANNELSPERUSER*3)))
    {
      sendto_one(acptr, form_str(ERR_TOOMANYCHANNELS),
		 me.name, nick, root_vchan->chname );
      return 0;
    }
  
  if(flags == CHFL_CHANOP)
    {
      sendto_one(uplink,
		 ":%s SJOIN %lu %s + :@%s", me.name,
		 chptr->channelts, chptr->chname, nick);
    }
  else if ((flags == CHFL_HALFOP) && (IsCapable(uplink, CAP_HOPS)))
    {
      sendto_one(uplink,
		 ":%s SJOIN %lu %s + :%%%s", me.name,
		 chptr->channelts, chptr->chname, nick);      
    }
  else
    {
      sendto_one(uplink,
		 ":%s SJOIN %lu %s + :%s", me.name,
		 chptr->channelts, chptr->chname, nick);
    }

  add_user_to_channel(chptr, acptr, flags);
 
  sendto_channel_local(ALL_MEMBERS, chptr,
		       ":%s!%s@%s JOIN :%s",
		       acptr->name,
		       acptr->username,
		       acptr->host,
		       root_vchan->chname);
  
  if( flags & CHFL_CHANOP )
  {
    chptr->mode.mode |= MODE_TOPICLIMIT;
    chptr->mode.mode |= MODE_NOPRIVMSGS;
      
    sendto_channel_local(ALL_MEMBERS,chptr,
                         ":%s MODE %s +nt",
                         me.name, root_vchan->chname);
    sendto_one(uplink, 
               ":%s MODE %s +nt",
               me.name, chptr->chname);
  }

  (void)channel_member_names(acptr, chptr, chname);

  return 0;
}
