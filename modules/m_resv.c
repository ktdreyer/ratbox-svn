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
#include "resv.h"
#include "hash.h"

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
  struct Resv *resv_p;
  char ctype[BUFSIZE];
  int type;

  if(parc > 2)
  {
    if(BadPtr(parv[1]))
      return;
      
    if(IsChannelName(parv[1]))
    {
      type = RESV_CHANNEL;
      ircsprintf(ctype, "channel");
    }
    else if(clean_nick_name(parv[1]))
    {
      type = RESV_NICK;
      ircsprintf(ctype, "nick");
    }
    else
    {
      sendto_one(source_p, ":%s NOTICE %s :You have specified an invalid resv: [%s]",
                 me.name, source_p->name, parv[1]);
      return;
    }
    
    resv_p = create_resv(parv[1], parv[2], type, 0);
    if(!(resv_p))
    {
      sendto_one(source_p,
                 ":%s NOTICE %s :A RESV has already been placed on %s: %s",
		 me.name, source_p->name, ctype, parv[1]);
      return;
    }
    
    sendto_one(source_p,
               ":%s NOTICE %s :A local RESV has been placed on %s: %s [%s]",
	       me.name, source_p->name, ctype,
	       resv_p->name, resv_p->reason);

    sendto_realops_flags(FLAGS_ALL,
                         "%s has placed a local RESV on %s: %s [%s]",
			 get_oper_name(source_p), ctype, 
			 resv_p->name, resv_p->reason);
             
    
  }
  else
  {
    if(!ResvList)
      sendto_realops_flags(FLAGS_ALL, "no resv channels");
    else
    {
      for(resv_p = ResvList; resv_p; resv_p=resv_p->next)
      {
        sendto_realops_flags(FLAGS_ALL, "RESV: %s [%s]", 
	                     resv_p->name, resv_p->reason);
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
  struct Resv *resv_p;
  char ctype[BUFSIZE];
  int type;

  if(IsChannelName(parv[1]))
  {
    type = RESV_CHANNEL;
    ircsprintf(ctype, "channel");
  }
  else if(clean_nick_name(parv[1]))
  {
    type = RESV_NICK;
    ircsprintf(ctype, "nick");
  }
  else
    return;
							    
  if(!ResvList || !(resv_p = (struct Resv *)hash_find_resv(parv[1], (struct Resv *)NULL, type)))
  {
    sendto_one(source_p, 
               ":%s NOTICE %s :A RESV does not exist for %s: %s",
	       me.name, source_p->name, ctype, parv[1]);
    return;
  }
  else if(resv_p->conf)
  {
    sendto_one(source_p,
       ":%s NOTICE %s :The RESV for %s: %s is in the config file and must be removed by hand.",
               me.name, source_p->name, ctype, parv[1]);
    return;	       
  }
  else
  {
    delete_resv(resv_p);

    sendto_one(source_p,
               ":%s NOTICE %s :The local RESV has been removed on %s: %s",
	       me.name, source_p->name, ctype, parv[1]);
    sendto_realops_flags(FLAGS_ALL,
                         "%s has removed the local RESV for %s: %s",
			 get_oper_name(source_p), ctype, parv[1]);
	      
  }
}
