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

#include <assert.h>
#include <string.h>
#include <stdlib.h>

struct Message lljoin_msgtab = {
  MSG_LLJOIN, 0, 3, MFLG_SLOW | MFLG_UNREG, 0L,
  {m_unregistered, m_error, ms_lljoin, m_error}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_LLJOIN, &lljoin_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_LLJOIN);
}

char *_version = "20001122";

/*
** m_lljoin
**      parv[0] = sender prefix
**      parv[1] = channel
*
* If a lljoin is received, from our uplink, join
* the requested client to the given channel, or ignore it
* if there is an error.
*/
int     ms_lljoin(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  char *name;
  char *nick;
  char can_join_flag;
  int  flags;
  struct Client *acptr;
  struct Channel *chptr;
  /* XXX global uplink */
  dlink_node *ptr;
  struct Client *uplink=NULL;
  if( ptr = serv_list.head )
    uplink = ptr->data;

  if( parc < 4 )
    return 0;

  /* If not a server just ignore it */
  if ( !IsServer(cptr) )
    return 0;

  name = parv[1];
  if(!name)
    return 0;

  nick = parv[2];
  if(!nick)
    return 0;

  can_join_flag = parv[3][0];

  flags = 0;

  acptr = hash_find_client(nick,(struct Client *)NULL);

  if( !acptr && !acptr->user )
    return 0;

  if( !MyClient(acptr) )
    return 0;

  if(!(chptr=hash_find_channel(name, NullChn)))
    {
      flags = CHFL_CHANOP;
      chptr = get_channel( acptr, name, CREATE );
    }

  if(uplink && IsCapable(uplink,CAP_LL))
    {
      switch(can_join_flag)
        {
        case 'J':

          if ((acptr->user->joined >= MAXCHANNELSPERUSER) &&
             (!IsAnyOper(acptr) || (acptr->user->joined >= MAXCHANNELSPERUSER*3)))
            {
              sendto_one(acptr, form_str(ERR_TOOMANYCHANNELS),
                         me.name, parv[0], name );
              return 0;
            }

          if(flags == CHFL_CHANOP)
            {
              sendto_one(uplink,
			 ":%s SJOIN %lu %s + :@%s", me.name,
			 chptr->channelts, name, nick);
            }
          else
            {
              sendto_one(uplink,
			 ":%s SJOIN %lu %s + :%s", me.name,
			 chptr->channelts, name, nick);
            }

          add_user_to_channel(chptr, acptr, flags);
 
          sendto_channel_local(ALL_MEMBERS, chptr,
			       ":%s!%s@%s JOIN :%s",
			       acptr->name,
			       acptr->username,
			       acptr->host,
			       name);
      
          if( flags & CHFL_CHANOP )
            {
              chptr->mode.mode |= MODE_TOPICLIMIT;
              chptr->mode.mode |= MODE_NOPRIVMSGS;

	      if(GlobalSetOptions.hide_chanops)
		{
		  sendto_channel_local(ONLY_CHANOPS,chptr,
				       ":%s MODE %s +nt",
				       me.name, chptr->chname);
		}
	      else
		{
		  sendto_channel_local(ALL_MEMBERS,chptr,
				       ":%s MODE %s +nt",
				       me.name, chptr->chname);
		}

              sendto_one(uplink, 
			 ":%s MODE %s +nt",
			 me.name, chptr->chname);
            }
        break;

        case 'I':
           sendto_one(acptr, form_str(ERR_INVITEONLYCHAN),
                      me.name, parv[0], name);
        break;

        case 'B':
           sendto_one(acptr, form_str(ERR_BANNEDFROMCHAN),
                      me.name, parv[0], name);
        break;

        case 'F':
           sendto_one(acptr, form_str(ERR_CHANNELISFULL),
                      me.name, parv[0], name);
        break;

        case 'K':
           sendto_one(acptr, form_str(ERR_BADCHANNELKEY),
                      me.name, parv[0], name);
        break;
      
        default:
        break;
	}
    }
  else
    {
      sendto_realops("*** LLJOIN request received from non LL capable server!");
    }
  return 0;
}
