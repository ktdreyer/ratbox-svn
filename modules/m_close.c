/************************************************************************
 *   IRC - Internet Relay Chat, src/m_version.c
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
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "send.h"
#include "msg.h"

struct Message close_msgtab = {
  MSG_CLOSE, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_close}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_CLOSE, &close_msgtab);
}

char *_version = "20001122";

/*
 * mo_close - CLOSE message handler
 *  - added by Darren Reed Jul 13 1992.
 */
int mo_close(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client* acptr;
  int            i;
  int            closed = 0;

  for (i = highest_fd; i; i--)
    {
      if (!(acptr = local[i]))
        continue;
      if (!IsUnknown(acptr) && !IsConnecting(acptr) &&
          !IsHandshake(acptr))
        continue;
      sendto_one(sptr, form_str(RPL_CLOSING), me.name, parv[0],
                 get_client_name(acptr, SHOW_IP), acptr->status);
      (void)exit_client(acptr, acptr, acptr, "Oper Closing");
      closed++;
    }
  sendto_one(sptr, form_str(RPL_CLOSEEND), me.name, parv[0], closed);
  return 0;
}

