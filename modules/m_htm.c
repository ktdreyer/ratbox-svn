/************************************************************************
 *   IRC - Internet Relay Chat, src/m_htm.c
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
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "msg.h"

#include <stdlib.h>

struct Message htm_msgtab = {
  MSG_HTM, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_htm}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_HTM, &htm_msgtab);
}

#define LOADCFREQ 5
/*
 * mo_htm - HTM command handler
 * high traffic mode info
 */
int mo_htm(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char *command;

  if (!MyClient(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  sendto_one(sptr,
        ":%s NOTICE %s :HTM is %s(%d), %s. Max rate = %dk/s. Current = %.1fk/s",
          me.name, parv[0], GlobalSetOptions.lifesux ? "ON" : "OFF",
	     GlobalSetOptions.lifesux,
          GlobalSetOptions.noisy_htm ? "NOISY" : "QUIET",
          LRV, currlife);
  if (parc > 1)
    {
      command = parv[1];
      if (!irccmp(command,"TO"))
        {
          if (parc > 2)
            {
              int new_value = atoi(parv[2]);
              if (new_value < 10)
                {
                  sendto_one(sptr, ":%s NOTICE %s :\002Cannot set LRV < 10!\002",
                             me.name, parv[0]);
                }
              else
                LRV = new_value;
              sendto_one(sptr, ":%s NOTICE %s :NEW Max rate = %dk/s. Current = %.1fk/s",
                         me.name, parv[0], LRV, currlife);
              sendto_realops("%s!%s@%s set new HTM rate to %dk/s (%.1fk/s current)",
                             parv[0], sptr->username, sptr->host,
                             LRV, currlife);
            }
          else 
            sendto_one(sptr, ":%s NOTICE %s :LRV command needs an integer parameter",me.name, parv[0]);
        }
      else
        {
          if (!irccmp(command,"ON"))
            {
              GlobalSetOptions.lifesux = 1;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now ON.", me.name, parv[0]);
              sendto_ops("Entering high-traffic mode: Forced by %s!%s@%s",
                         parv[0], sptr->username, sptr->host);
              LCF = 30; /* 30s */
            }
          else if (!irccmp(command,"OFF"))
            {
              GlobalSetOptions.lifesux = 0;
              LCF = LOADCFREQ;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now OFF.", me.name, parv[0]);
              sendto_ops("Resuming standard operation: Forced by %s!%s@%s",
                         parv[0], sptr->username, sptr->host);
            }
          else if (!irccmp(command,"QUIET"))
            {
              sendto_ops("HTM is now QUIET");
              GlobalSetOptions.noisy_htm = NO;
            }
          else if (!irccmp(command,"NOISY"))
            {
              sendto_ops("HTM is now NOISY");
              GlobalSetOptions.noisy_htm = YES;
            }
          else
            sendto_one(sptr,
                       ":%s NOTICE %s :Commands are:HTM [ON] [OFF] [TO int] [QUIET] [NOISY]",
                       me.name, parv[0]);
        }
    }
  return 0;
}

