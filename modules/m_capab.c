/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_capab.c
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
#include "parse.h"
#include "modules.h"

static void mr_capab(struct Client*, struct Client*, int, char**);

struct Message capab_msgtab = {
  "CAPAB", 0, 0, 0, MFLG_SLOW | MFLG_UNREG, 0,
  {mr_capab, m_error, mr_capab, m_error}
};
#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&capab_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&capab_msgtab);
}

char *_version = "20001122";
#endif

/*
 * mr_capab - CAPAB message handler
 *      parv[0] = sender prefix
 *      parv[1] = space-separated list of capabilities
 *
 */
static void mr_capab(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  struct Capability *cap;
  char* p;
  char* s;
#ifdef HAVE_LIBCRYPTO
  struct EncCapability *ecap;
  char* enc_s;
  char* enc_p;
#endif

  /* ummm, this shouldn't happen. Could argue this should be logged etc. */
  if (client_p->localClient == NULL)
    return;

  if (client_p->localClient->caps)
    {
      exit_client(client_p, client_p, client_p, "CAPAB received twice");
      return;
    }
  else
    client_p->localClient->caps |= CAP_CAP;

  for (s = strtoken(&p, parv[1], " "); s; s = strtoken(&p, NULL, " "))
    {
#ifdef HAVE_LIBCRYPTO
      if (0 == strncmp(s, "ENC:", 4))
      {
        s += 4; /* skip ENC: */
        SetCapable(client_p, CAP_ENC);
        /* parse list of ciphers */
        for (enc_s = strtoken(&enc_p, s, ","); enc_s;
             enc_s = strtoken(&enc_p, NULL, ","))
        {
          /* parse 'ENC:C1,C2,C3,C4' */
          for (ecap = enccaptab; ecap->name; ecap++)
          {
            if ((0 == strcmp(ecap->name, enc_s)) &&
                (ecap->cap & CAP_ENC_MASK))
            {
              client_p->localClient->enc_caps |= ecap->cap;
              break;
            }
          }
        }
      }
      else /* normal capab */
#endif
        for (cap = captab; cap->name; cap++)
        {
          if (0 == strcmp(cap->name, s))
          {
            client_p->localClient->caps |= cap->cap;
            break;
          }
        }
    }
}

