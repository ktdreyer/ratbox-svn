/************************************************************************
 *   IRC - Internet Relay Chat, src/m_capab.c
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
#include "s_serv.h"
#include "s_conf.h"
#include "msg.h"

#include <assert.h>

struct Message capab_msgtab = {
  MSG_CAPAB, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_error, ms_capab, m_error}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_CAPAB, &capab_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_CAPAB);
}

char *_version = "20001122";

/*
 * ms_capab - CAPAB message handler
 *      parv[0] = sender prefix
 *      parv[1] = space-separated list of capabilities
 */
int ms_capab(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Capability *cap;
  char* p;
  char* s;

  if ((!IsUnknown(cptr) && !IsHandshake(cptr)) || parc < 2)
    return 0;

  if (cptr->caps)
    return exit_client(cptr, cptr, cptr, "CAPAB received twice");
  else
    cptr->caps |= CAP_CAP;

  for (s = strtoken(&p, parv[1], " "); s; s = strtoken(&p, NULL, " "))
    {
      for (cap = captab; cap->name; cap++)
        {
          if (0 == strcmp(cap->name, s))
            {
              cptr->caps |= cap->cap;
              break;
            }
         }
    }
  return 0;
}

