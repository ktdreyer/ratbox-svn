/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_clearchan.c
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
#include "client.h"
#include "ircd.h"
#include "numeric.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "whowas.h"
#include "irc_string.h"
#include "hash.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

#include <string.h>

#define MSG_CLEARCHAN "CLEARCHAN"


int mo_clearchan(struct Client *cptr, struct Client *sptr,
		 int parc, char *parv[]);

void kick_list(struct Client *cptr, struct Channel *chptr,
	       dlink_list *list,char *chname);

struct Message clearchan_msgtab = {
  MSG_CLEARCHAN, 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, mo_clearchan, mo_clearchan}
};

void
_modinit(void)
{
  mod_add_cmd(&clearchan_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&clearchan_msgtab);
}

char *_version = "20010104";

/*
** mo_clearchan
**      parv[0] = sender prefix
**      parv[1] = channel
*/
int mo_clearchan(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Channel *chptr;

  if( parc < 2 )
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		 me.name, sptr->name, "CLEARCHAN");
      return 0;
    }

  chptr= hash_find_channel(parv[1], NullChn);

  if( chptr == NULL )
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
		 me.name, parv[0], parv[1]);
      return 0;
    }

  sendto_all_local_opers(sptr, NULL, "CLEARCHAN called for %s by %s",
			 parv[1], sptr->name);
  sendto_ll_serv_butone(NULL,sptr, 1,
			":%s WALLOPS :CLEARCHAN called for %s by %s",
			parv[1], sptr->name);
  kick_list(cptr, chptr, &chptr->chanops, parv[1]);
  kick_list(cptr, chptr, &chptr->voiced, parv[1]);
  kick_list(cptr, chptr, &chptr->halfops, parv[1]);
  kick_list(cptr, chptr, &chptr->peons, parv[1]);

  return 0;
}

void kick_list(struct Client *cptr, struct Channel *chptr,
	       dlink_list *list,char *chname)
{
  struct Client *who;
  dlink_node *ptr;

  for (ptr= list->head; ptr; ptr = ptr->next)
    {
      who = ptr->data;

      sendto_channel_local(ALL_MEMBERS, chptr,
			   ":%s!%s@%s KICK %s %s :CLEARCHAN",
			   who->name,
			   who->username,
			   who->host,
			   chname, who->name);

      sendto_channel_remote(chptr, cptr,
			    ":%s KICK %s %s :%s",
			    who->name, chname,
			    who->name, "CLEARCHAN");

      remove_user_from_channel(chptr, who);

    }
}


