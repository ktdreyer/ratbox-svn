/************************************************************************
 *   IRC - Internet Relay Chat, src/m_links.c
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
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "s_conf.h"
#include "msg.h"

#include <assert.h>
#include <string.h>

struct Message links_msgtab = {
  MSG_LINKS, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_links, ms_links, m_links}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_LINKS, &links_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_LINKS);
}

char *_version = "20001122";

/*
 * m_links - LINKS message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername mask
 * or
 *      parv[0] = sender prefix
 *      parv[1] = server to query 
 *      parv[2] = servername mask
 */
int m_links(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  const char*    mask = "";
  struct Client* acptr;
  char           clean_mask[2 * HOSTLEN + 4];
  char*          p;
  char*          s;
  int            bogus_server = 0;

  if (parc > 2)
    {
      if (hunt_server(cptr, sptr, ":%s LINKS %s :%s", 1, parc, parv)
          != HUNTED_ISME)
        return 0;
      mask = parv[2];
    }
  else if (parc == 2)
    mask = parv[1];

  assert(0 != mask);

  if (*mask)       /* only necessary if there is a mask */
    mask = collapse(clean_string(clean_mask, mask, 2 * HOSTLEN));

  if (MyConnect(sptr))
    sendto_realops_flags(FLAGS_SPY,
                       "LINKS '%s' requested by %s (%s@%s) [%s]",
                       mask, sptr->name, sptr->username,
                       sptr->host, sptr->user->server);
  
  for (acptr = GlobalClientList; acptr; acptr = acptr->next) 
    {
      if (!IsServer(acptr) && !IsMe(acptr))
        continue;
      if (*mask && !match(mask, acptr->name))
        continue;
      if(IsAnyOper(sptr))
         sendto_one(sptr, form_str(RPL_LINKS),
                    me.name, parv[0], acptr->name, acptr->serv->up,
                    acptr->hopcount, (acptr->info[0] ? acptr->info :
                                      "(Unknown Location)"));
      else
        {
          if(acptr->info[0])
            {
              p = strchr(acptr->info,']');
              if(p)
                p += 2; /* skip the nasty [IP] part */
              else
                p = acptr->info;
            }
          else
            p = "(Unknown Location)";

          sendto_one(sptr, form_str(RPL_LINKS),
                    me.name, parv[0], acptr->name, acptr->serv->up,
                    IsAnyOper(sptr) ? acptr->hopcount : 0, p);
        }

    }
  
  sendto_one(sptr, form_str(RPL_ENDOFLINKS), me.name, parv[0],
             EmptyString(mask) ? "*" : mask);
  return 0;
}

/*
 * ms_links - LINKS message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername mask
 * or
 *      parv[0] = sender prefix
 *      parv[1] = server to query 
 *      parv[2] = servername mask
 */
int ms_links(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  if (hunt_server(cptr, sptr, ":%s LINKS %s :%s", 1, parc, parv)
      != HUNTED_ISME)
    return 0;

  if(IsAnyOper(sptr))
    return(m_links(cptr,sptr,parc,parv));
  return 0;
}

