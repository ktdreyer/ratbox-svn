/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_operwall.c
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
#include "msg.h"

struct Message operwall_msgtab = {
  MSG_OPERWALL, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, ms_operwall, mo_operwall}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_OPERWALL, &operwall_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_OPERWALL);
}

char *_version = "20001122";

/*
 * mo_operwall - OPERWALL message handler
 *  (write to *all* local opers currently online)
 *      parv[0] = sender prefix
 *      parv[1] = message text
 */

int mo_operwall(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char *message = parc > 1 ? parv[1] : NULL;

  if (check_registered_user(sptr))
    return 0;

  if (EmptyString(message))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                   me.name, parv[0], "OPERWALL");
      return 0;
    }

  sendto_serv_butone( NULL, ":%s OPERWALL :%s", parv[0], message);
  sendto_all_local_opers(sptr, "OPERWALL", "%s", message);
  return 0;
}

/*
 * ms_operwall - OPERWALL message handler
 *  (write to *all* local opers currently online)
 *      parv[0] = sender prefix
 *      parv[1] = message text
 */

int ms_operwall(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char *message = parc > 1 ? parv[1] : NULL;

  if (check_registered_user(sptr))
    return 0;

  if (!IsAnyOper(sptr) || IsServer(sptr))
    {
      if (MyClient(sptr) && !IsServer(sptr))
        sendto_one(sptr, form_str(ERR_NOPRIVILEGES),
                   me.name, parv[0]);
      return 0;
    }

  if (EmptyString(message))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                   me.name, parv[0], "OPERWALL");
      return 0;
    }

  sendto_serv_butone(IsServer(cptr) ? cptr : NULL, ":%s OPERWALL :%s",
                     parv[0], message);
  sendto_all_local_opers(sptr, "OPERWALL", "%s", message);
  return 0;
}


