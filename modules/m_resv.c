/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_resv.c
 *   Copyright (C) 2001 Hybrid Development Team
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
#include "channel.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "s_conf.h"

static void mo_resv(struct Client *, struct Client *, int, char **);
static void mo_unresv(struct Client *, struct Client *, int, char **);

struct Message resv_msgtab = {
  "RESV", 0, 1, 0, MFLG_SLOW | MFLG_UNREG, 0,
  {m_ignore, m_not_oper, m_ignore, mo_resv}
};

struct Message unresv_msgtab = {
  "UNRESV", 0, 2, 0, MFLG_SLOW | MFLG_UNREG, 0,
  {m_ignore, m_not_oper, m_ignore, mo_unresv}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&resv_msgtab);
  mod_add_cmd(&unresv_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&resv_msgtab);
  mod_del_cmd(&unresv_msgtab);
}

char *_version = "20010626";
#endif

/*
 * mo_resv()
 *      parv[0] = sender prefix
 *      parv[1] = channel/nick to forbid
 */

static void mo_resv(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  struct JupedChannel *jptr;
  int len;

  if(parc > 1)
  {
    if(BadPtr(parv[1]))
      return;

    if((parv[1][0] != '#') && (parv[1][0] != '&'))
      return;
    
    len = strlen(parv[1]);
 
    if(len > CHANNELLEN)
    {
      len = CHANNELLEN;
      *(parv[1] + CHANNELLEN) = '\0';
    }

    if(jptr = find_resv(parv[1]))
    {
      sendto_one(source_p,
                 ":%s NOTICE %s :A RESV has already been placed on channel: %s",
		 me.name, source_p->name, jptr->chname);
      return;
    }
    
    jptr = (struct JupedChannel *)MyMalloc(sizeof(struct JupedChannel) + len + 1);
    strcpy(jptr->chname, parv[1]);
    
    if(JupedChannelList)
      JupedChannelList->prev = jptr;
    
    jptr->next = JupedChannelList;
    jptr->prev = NULL;

    JupedChannelList = jptr;

    sendto_one(source_p,
               ":%s NOTICE %s :A local RESV has been placed on channel: %s",
	       me.name, source_p->name, jptr->chname);

    sendto_realops_flags(FLAGS_ALL,
                         "%s has placed a local RESV on channel: %s",
			 get_oper_name(source_p), jptr->chname);
             
    
  }
  else
  {
    if(!JupedChannelList)
      sendto_realops_flags(FLAGS_ALL, "no resv channels");
    else
    {
      for(jptr = JupedChannelList; jptr; jptr = jptr->next)
      {
        sendto_realops_flags(FLAGS_ALL, "RESV: %s", jptr->chname);
      }
    }
  }
  

}

/*
 * mo_unresv()
 *     parv[0] = sender prefix
 *     parv[1] = channel/nick to unforbid
 */

static void mo_unresv(struct Client *client_p, struct Client *source_p,
                      int parc, char *parv[])
{
  struct JupedChannel *jptr;

  if(!JupedChannelList || !(jptr = find_resv(parv[1])))
#if 0
return;

  if(!(jptr = find_resv(parv[1])))
#endif  
  {
    sendto_one(source_p, 
               ":%s NOTICE %s :A RESV does not exist for channel %s",
	       me.name, source_p->name, parv[1]);
    return;
  }
  else
  {
    if(jptr->prev)
      jptr->prev->next = jptr->next;
    else
      JupedChannelList = jptr->next;

    if(jptr->next)
      jptr->next->prev = jptr->prev;

    MyFree((char *)jptr);

    sendto_one(source_p,
               ":%s NOTICE %s :The local RESV has been removed on channel: %s",
	       me.name, source_p->name, parv[1]);
    sendto_realops_flags(FLAGS_ALL,
                         "%s has removed the local RESV for channel: %s",
			 get_oper_name(source_p), parv[1]);
	      
  }
}
