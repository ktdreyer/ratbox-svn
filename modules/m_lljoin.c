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

static void ms_lljoin(struct Client *,struct Client *,int,char **);

struct Message lljoin_msgtab = {
  "LLJOIN", 0, 3, 0, MFLG_SLOW | MFLG_UNREG, 0L,
  {m_unregistered, m_ignore, ms_lljoin, m_ignore}
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
static void ms_lljoin(struct Client *client_p,
                     struct Client *source_p,
                     int parc,
                     char *parv[])
{
  char *chname = NULL;
  char *nick = NULL;
  char *key = NULL;
  char *vkey = NULL;
  char *pvc = NULL;
  int  vc_ts;
  int  flags;
  int  i;
  struct Client *aclient_p;
  struct Channel *chptr, *vchan_chptr, *root_vchan;
  int cjoin = 0;

  if(uplink && !IsCapable(uplink,CAP_LL))
    {
      sendto_realops_flags(FLAGS_ALL,
			   "*** LLJOIN requested from non LL server %s",
			   client_p->name);
      return;
    }

  chname = parv[1];
  if(chname == NULL)
    return;

  nick = parv[2];
  if(nick == NULL)
    return;

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

  aclient_p = hash_find_client(nick,(struct Client *)NULL);

  if( !aclient_p || !aclient_p->user )
    return;

  if( !MyClient(aclient_p) )
    return;

  chptr = hash_find_channel(chname, NullChn);

  if (cjoin)
  {
    if(!chptr) /* Uhm, bad! */
    {
      sendto_realops_flags(FLAGS_ALL,
        "LLJOIN %s %s called by %s, but root chan doesn't exist!",
        chname, nick, client_p->name);
      return;
    }
    flags = CHFL_CHANOP;

    if(! (vchan_chptr = cjoin_channel(chptr, aclient_p, chname)))
      return;

    root_vchan = chptr;
    chptr = vchan_chptr;
  }
  else
  {
    if (chptr)
    {
      vchan_chptr = select_vchan(chptr, client_p, aclient_p, vkey, chname);
    }
    else
    {
      chptr = vchan_chptr = get_channel( aclient_p, chname, CREATE );
      flags = CHFL_CHANOP;
    }
    
    if (vchan_chptr != chptr)
    {
      root_vchan = chptr;
      chptr = vchan_chptr;
    }
    else
      root_vchan = chptr;

    if(!chptr || !root_vchan)
      return;

    if (chptr->users == 0)
      flags = CHFL_CHANOP;
    else
      flags = 0;

    /* XXX in m_join.c :( */
    /* check_spambot_warning(aclient_p, chname); */

    /* They _could_ join a channel twice due to lag */
    if(chptr)
    {
      if (IsMember(aclient_p, chptr))    /* already a member, ignore this */
        return;
    }
    else
    {
      sendto_one(aclient_p, form_str(ERR_UNAVAILRESOURCE),
                 me.name, nick, root_vchan->chname);
      return;
    }

    if( (i = can_join(aclient_p, chptr, key)) )
    {
      sendto_one(aclient_p,
                 form_str(i), me.name, nick, root_vchan->chname);
      return;
    }
  }

  if ((aclient_p->user->joined >= MAXCHANNELSPERUSER) &&
      (!IsOper(aclient_p) || (aclient_p->user->joined >= MAXCHANNELSPERUSER*3)))
    {
      sendto_one(aclient_p, form_str(ERR_TOOMANYCHANNELS),
		 me.name, nick, root_vchan->chname );
      return; 
    }
  
  if(flags == CHFL_CHANOP)
    {
      chptr->channelts = CurrentTime;
      /*
       * XXX - this is a rather ugly hack.
       *
       * Unfortunately, there's no way to pass
       * the fact that it is a vchan through SJOIN...
       */
      /* Prevent users creating a fake vchan */
      if (chname[0] == '#' && chname[1] == '#')
        {
          if ((pvc = strrchr(chname+3, '_')))
          {
            /*
             * OK, name matches possible vchan:
             * ##channel_blah
             */
            pvc++; /*  point pvc after last _ */
            vc_ts = atol(pvc);
            /*
             * if blah is the same as the TS, up the TS
             * by one, to prevent this channel being
             * seen as a vchan
             */
            if (vc_ts == CurrentTime)
              chptr->channelts++;
          }
        }

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

  add_user_to_channel(chptr, aclient_p, flags);

  if ( chptr != root_vchan )
    add_vchan_to_client_cache(aclient_p,root_vchan,chptr);
 
  sendto_channel_local(ALL_MEMBERS, chptr,
		       ":%s!%s@%s JOIN :%s",
		       aclient_p->name,
		       aclient_p->username,
		       aclient_p->host,
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

  (void)channel_member_names(aclient_p, chptr, chname);
}
