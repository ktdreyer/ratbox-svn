/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_llnick.c
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

static int ms_llnick(struct Client*, struct Client*, int, char**);

struct Message llnick_msgtab = {
  "LLNICK", 0, 3, 0, MFLG_SLOW | MFLG_UNREG, 0L,
  {m_unregistered, m_ignore, ms_llnick, m_ignore}
};

void
_modinit(void)
{
  mod_add_cmd(&llnick_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&llnick_msgtab);
}

char *_version = "20001122";

/*
 * m_llnick
 *      parv[0] = sender prefix
 *      parv[1] = status 
 *      parv[2] = nick
 *      parv[3] = old nick
 *
 */
static int  ms_llnick(struct Client *cptr,
                      struct Client *sptr,
                      int parc,
                      char *parv[])
{
  char *nick;
  char *nick_old = NULL;
  struct Client *acptr = NULL;
  int exists = 0;
  int new = 0;
  dlink_node *ptr;
  
  if(!IsCapable(cptr,CAP_LL))
    {
      sendto_realops_flags(FLAGS_ALL,
			   "*** LLNICK requested from non LL server %s",
			   cptr->name);
      return 0;
    }

  if (parc < 4)
    return 0;

  if (*parv[1] == 'Y')
    exists = 1;
  
  nick = parv[2];
  nick_old = parv[3];

  if (*nick_old == '!')
    new = 1;

  if (new)
  {
    /* New user -- find them */
    for( ptr = unknown_list.head; ptr; ptr = ptr->next )
    {
      if( !strcmp(nick_old, ((struct Client *)ptr->data)->name) )
      {
        acptr = ptr->data;
        *acptr->name = '\0'; /* unset their peudo-nick */
        break;
      }
    }
    if (!acptr) /* Can't find them -- maybe they got a different nick */
      return 0;
  }
  else
  {
    /* Existing user changing nickname */
    acptr = hash_find_client(nick_old,(struct Client *)NULL);
  
    if (!acptr) /* Can't find them -- maybe they got a different nick */
      return 0;
  }
  
  if(hash_find_client(nick,(struct Client *)NULL) || exists)
  {
    /* The nick they want is in use. complain */
    sendto_one(acptr, form_str(ERR_NICKNAMEINUSE), me.name,
               new ? "*" : nick_old,
               nick);
    return 0;
  }

  if(new)
    return(set_initial_nick(acptr, acptr, nick));
  else
    return(change_local_nick(acptr, acptr, nick));

  return 0;
}
