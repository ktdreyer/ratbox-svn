/************************************************************************
 *   IRC - Internet Relay Chat, src/m_mode.c
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
#include "s_user.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"

struct Message mode_msgtab = {
  MSG_MODE, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_mode, m_mode, m_mode}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_MODE, &mode_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_MODE);
}


char *_version = "20001122";

/*
 * m_mode - MODE command handler
 * parv[0] - sender
 * parv[1] - channel
 */
int m_mode(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Channel* chptr;
  struct Channel* vchan;
  static char     modebuf[MODEBUFLEN];
  static char     parabuf[MODEBUFLEN];

  /* Now, try to find the channel in question */
  if( IsChanPrefix(parv[1][0]) )
    {
      /* Don't do any of this stuff at all
       * unless it looks like a channel name 
       */
      
      if (!check_channel_name(parv[1]))
	  { 
	    sendto_one(sptr, form_str(ERR_BADCHANNAME),
		       me.name, parv[0], (unsigned char *)parv[1]);
	    return 0;
	  }
	  
      chptr = hash_find_channel(parv[1], NullChn);
      if(!chptr)
	{
	  /* XXX global uplink */
	  dlink_node *ptr;
	  struct Client *uplink=NULL;
	  if( ptr = serv_list.head )
	    uplink = ptr->data;

	  /* LazyLinks */
	  /* this was segfaulting if we had no servers linked.
	   *  -pro
	   */
	  if ( !ConfigFileEntry.hub && uplink &&
	       IsCapable( uplink, CAP_LL) )
	    {
	      /* cache the channel if it exists on uplink */
	      
	      sendto_one( uplink, ":%s CBURST %s",
			  me.name, parv[1] );
	      
	      /* meanwhile, ask for channel mode */
	      sendto_one( uplink, ":%s MODE %s",
			  sptr->name, parv[1] );
	      return 0;
	    }
	}
    }
  else
    {
      /* if here, it has to be a non-channel name */
      return user_mode(cptr, sptr, parc, parv);
    }
  
  if (parc < 3 && chptr)
    {
      *modebuf = *parabuf = '\0';
      modebuf[1] = '\0';

      if (HasVchans(chptr))
	{
	  vchan = map_vchan(chptr,sptr);
	  if(vchan == 0)
	    {
	      channel_modes(chptr, sptr, modebuf, parabuf);
	      sendto_one(sptr, form_str(RPL_CHANNELMODEIS), me.name, parv[0],
			 chptr->chname, modebuf, parabuf);
	      sendto_one(sptr, form_str(RPL_CREATIONTIME), me.name, parv[0],
			 chptr->chname, chptr->channelts);
	    }
	  else
	    {
	      channel_modes(vchan, sptr, modebuf, parabuf);
	      sendto_one(sptr, form_str(RPL_CHANNELMODEIS), me.name, parv[0],
			 chptr->chname, modebuf, parabuf);
	      sendto_one(sptr, form_str(RPL_CREATIONTIME), me.name, parv[0],
			 chptr->chname, vchan->channelts);
	    }
	}
      else
	{
	  channel_modes(chptr, sptr, modebuf, parabuf);
	  sendto_one(sptr, form_str(RPL_CHANNELMODEIS), me.name, parv[0],
		     chptr->chname, modebuf, parabuf);
	  sendto_one(sptr, form_str(RPL_CREATIONTIME), me.name, parv[0],
		     chptr->chname, chptr->channelts);
	}
      return 0;
    } 
  else
    if (!chptr)
      {
	sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0], parv[1]);
	return 0;
      }

  /* LazyLinks - can't do mode "#channel +o" unless user is on channel
   * and in that case, channel has been cached using CBURST
   */

  if (HasVchans(chptr))
    {
      vchan = map_vchan(chptr,sptr);
      if(vchan == 0)
	{
	  set_channel_mode(cptr, sptr, chptr, parc - 2, parv + 2, 
			   chptr->chname );
	}
      else
	{
	  set_channel_mode(cptr, sptr, vchan, parc - 2, parv + 2,
			   chptr->chname);
	}
    }
  else
    {
      set_channel_mode(cptr, sptr, chptr, parc - 2, parv + 2,
		       chptr->chname);
    }
  
  return 0;
}




