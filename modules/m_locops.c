/************************************************************************
 *   IRC - Internet Relay Chat, src/m_locops.c
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
#include "handlers.h"
#include "client.h"
#include "ircd.h"
#include "irc_string.h"
#include "numeric.h"
#include "send.h"
#include "s_user.h"
#include "s_conf.h"
#include "hash.h"
#include "msg.h"

struct Message locops_msgtab = {
  MSG_LOCOPS, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, m_locops}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_LOCOPS, &locops_msgtab);
}

char *_version = "20001122";

/*
 * m_locops - LOCOPS message handler
 * (write to *all* local opers currently online)
 *      parv[0] = sender prefix
 *      parv[1] = message text
 */
int m_locops(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char *message = NULL;
#ifdef SLAVE_SERVERS
  char *slave_oper;
  struct Client *acptr;
#endif

#ifdef SLAVE_SERVERS
  if(IsServer(sptr))
    {
      if(!find_special_conf(sptr->name,CONF_ULINE))
        {
          sendto_realops("received Unauthorized locops from %s",sptr->name);
          return 0;
        }

      if(parc > 2)
        {
          slave_oper = parv[1];

          parc--;
          parv++;

          if ((acptr = hash_find_client(slave_oper,(struct Client *)NULL)))
            {
              if(!IsPerson(acptr))
                return 0;
            }
          else
            return 0;

          if(parv[1])
            {
              message = parv[1];
              send_operwall(acptr, "SLOCOPS", message);
            }
          else
            return 0;

          if(ConfigFileEntry.hub)
            sendto_slaves(sptr,"LOCOPS",slave_oper,parc,parv);
          return 0;
        }
    }
  else
    {
      message = parc > 1 ? parv[1] : NULL;
    }
#else
  if(IsServer(sptr))
    return 0;
  message = parc > 1 ? parv[1] : NULL;
#endif

  if (EmptyString(message))
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "LOCOPS");
      return 0;
    }

  if(MyConnect(sptr) && IsAnyOper(sptr))
    {

#ifdef SLAVE_SERVERS
      sendto_slaves(NULL,"LOCOPS",sptr->name,parc,parv);
#endif
      send_operwall(sptr, "LOCOPS", message);
    }
  else
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return(0);
    }

  return(0);
}


