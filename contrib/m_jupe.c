/************************************************************************
 *   IRC - Internet Relay Chat, contrib/m_jupe.c
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

/*
 * WARNING: This is unfinished, and not likely to work :P
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
#include "class.h"
#include "common.h"
#include "event.h"

#include "fdlist.h"
#include "list.h"
#include "s_conf.h"
#include "scache.h"
#include <string.h>


int mo_jupe(struct Client *cptr, struct Client *sptr,
		 int parc, char *parv[]);

struct Message jupe_msgtab = {
  "JUPE", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, mo_jupe, mo_jupe}
};

void
_modinit(void)
{
  mod_add_cmd(&jupe_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&jupe_msgtab);
}

char *_version = "20010104";

/*
** mo_jupe
**      parv[0] = sender prefix
**      parv[1] = server we're juping
**      parv[2] = reason for jupe
*/
int mo_jupe(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client *acptr;
  struct Client *ajupe;


  if( parc < 3 )
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		 me.name, sptr->name, "JUPE");
      return 0;
    }

  if(!ConfigFileEntry.hub)
    return 0;

  if(!IsAdmin(sptr))
    {
      sendto_one(sptr, ":%s NOTICE %s :You must be an admin to use this command",
               me.name, parv[0]);
      return 0;
    }

  sendto_all_local_opers(&me, NULL, "JUPE for %s requested by %s: %s",
			 parv[1], sptr->name, parv[2]);
  sendto_ll_serv_butone(NULL, sptr, 1,
			":%s WALLOPS :JUPE for %s requested by %s: %s",
			&me, parv[1], sptr->name, parv[2]);

  acptr= find_server(parv[1]);

  if(acptr)
    {
      if(MyConnect(acptr))
	exit_client(cptr, acptr, &me, parv[2]);
      else
	sendto_serv_butone(&me, ":%s SQUIT %s :[juped] %s",
			   me.name, parv[1], parv[2]);          
    }

  sendto_serv_butone(&me, ":%s SERVER %s 1 :[Juped server] %s",
		     me.name, parv[1], parv[2]);

  sendto_realops_flags(FLAGS_ALL,
                       "Link with %s established: (JUPED) link",
		       parv[1]);

  ajupe = make_client(NULL);
  make_server(ajupe);
  ajupe->hopcount = 1;
  strncpy_irc(ajupe->name,parv[1],HOSTLEN);
  strncpy_irc(ajupe->info, "(JUPED)", REALLEN);
  ajupe->serv->up = me.name;
  ajupe->servptr = &me;
  SetServer(ajupe);

  Count.server++;
  Count.myserver++;

  /* Some day, all these lists will be consolidated *sigh* */
  add_client_to_list(ajupe);
  add_client_to_llist(&(me.serv->servers), ajupe);
  add_to_client_hash_table(ajupe->name, ajupe);

  return 0;
}


