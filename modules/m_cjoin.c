/************************************************************************
 *   IRC - Internet Relay Chat, src/m_cjoin.c
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
#include "s_conf.h"
#include "msg.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct Message cjoin_msgtab = {
  MSG_CJOIN, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_cjoin, m_error, m_cjoin}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_CJOIN, &cjoin_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_CJOIN);
}

char *_version = "20001122";

/*
** m_cjoin
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = channel password (key)
*/
int     m_cjoin(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  static char   jbuf[BUFSIZE];
  struct Channel *chptr = NULL;
  struct Channel *vchan_chptr = NULL;
  char  *name;
  char  vchan_name[CHANNELLEN];
  char  *p = NULL;
  dlink_node *m;

  if (!(sptr->user))
    {
      /* something is *fucked* - bail */
      return 0;
    }

  /* Silently ignore non local cjoin requests */
  if (!MyClient(sptr))
    {
      return 0;
    }

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "CJOIN");
      return 0;
    }

  /* Ok, only allowed to CJOIN already existing channels
   * so first part simply verifies the "root" channel exists first
   */

  *jbuf = '\0';

  name = parv[1];
  if ( (p = strchr(name,',')) )
    *p = '\0';

  if (!check_channel_name(name))
    {
      sendto_one(sptr, form_str(ERR_BADCHANNAME),
		 me.name, parv[0], (unsigned char*) name);
      return 0;
    }

  if (*name == '&')
    {
      sendto_one(sptr, form_str(ERR_BADCHANNAME),
		 me.name, parv[0], (unsigned char*) name);
      return 0;
    }

  if (!IsChannelName(name))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
		 me.name, parv[0], name);
      return 0;
    }

  (void)strncpy(jbuf, name, sizeof(jbuf) - 1);

  if( (chptr = hash_find_channel(name, NullChn)) == NULL )
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
		 me.name, parv[0], name);
      return 0;
    }

  /* don't cjoin a vchan, only the top is allowed */
  if (IsVchan(chptr))
    {
      /* could send a notice here, but on a vchan aware server
       * they shouldn't see the sub chans anyway
       */
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                 me.name, parv[0], name);
      return 0;
    }

  if( on_sub_vchan(chptr,sptr) )
    {
      sendto_one(sptr,":%s NOTICE %s :*** You are on a sub chan of %s already",
		 me.name, sptr->name, name);
      sendto_one(sptr, form_str(ERR_BADCHANNAME),
		 me.name, parv[0], (unsigned char*) name);
      return 0;
    }

  /* "root" channel name exists, now create a new copy of it */
  /* ZZZ XXX N.B. 
   * Following to be added 
   *
   * 1. detect if channel already exist (remote chance)
   */

  if (strlen(name) > CHANNELLEN-15)
    {
      sendto_one(sptr, form_str(ERR_BADCHANNAME),me.name, parv[0], name); 
      return 0;
    }

  if ((sptr->user->joined >= MAXCHANNELSPERUSER) &&
     (!IsAnyOper(sptr) || (sptr->user->joined >= MAXCHANNELSPERUSER*3)))
     {
       sendto_one(sptr, form_str(ERR_TOOMANYCHANNELS),
                  me.name, parv[0], name);
       return 0;
     }

  ircsprintf( vchan_name, "##%s_%lu", name+1, CurrentTime );
  vchan_chptr = get_channel(sptr, vchan_name, CREATE);

  if( vchan_chptr == NULL )
    {
      sendto_one(sptr, form_str(ERR_BADCHANNAME),
		 me.name, parv[0], (unsigned char*) name);
      return 0;
    }

  m = make_dlink_node();
  m->data = chptr;
  dlinkAdd(vchan_chptr, m, &chptr->vchan_list);

  /*
  **  Complete user entry to the new channel
  */

  add_user_to_channel(vchan_chptr, sptr, CHFL_CHANOP);

  add_vchan_to_client_cache(sptr,chptr,vchan_chptr);

  /*
  **  Set timestamp
  */
  
  vchan_chptr->channelts = CurrentTime;
  sendto_match_servs(vchan_chptr, cptr,
		     ":%s SJOIN %lu %s + :@%s", me.name,
		     vchan_chptr->channelts, vchan_chptr->chname, parv[0]);
  /*
  ** notify all other users on the new channel
  */
  sendto_channel_butserv(ALL_MEMBERS, vchan_chptr, sptr, ":%s JOIN :%s",
			 parv[0], chptr->chname);


  vchan_chptr->mode.mode |= MODE_TOPICLIMIT;
  vchan_chptr->mode.mode |= MODE_NOPRIVMSGS;

  sendto_channel_butserv(ONLY_CHANOPS,vchan_chptr, sptr,
			 ":%s MODE %s +nt",
			 me.name, chptr->chname);

  sendto_match_servs(vchan_chptr, sptr, 
		     ":%s MODE %s +nt",
		     me.name, vchan_chptr->chname);

  del_invite(vchan_chptr, sptr);
  (void)channel_member_names(sptr, vchan_chptr, chptr->chname);

  return 0;
}
