/************************************************************************
 *   IRC - Internet Relay Chat, doc/example_module.c
 *   Copyright (C) 2001 Hybrid Development Team
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

/* List of ircd includes from ../include/ */
#include "handlers.h"
#include "client.h"
#include "common.h"     /* FALSE bleah */
#include "ircd.h"
#include "irc_string.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

#include <string.h>

extern char *crypt();

static void m_mkpasswd(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[]);
static void mo_mkpasswd(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[]);
static char *make_salt(void);

static char saltChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

struct Message test_msgtab = {
  "MKPASSWD", 0, 1, 2, MFLG_SLOW, 0,
  {m_unregistered, m_mkpasswd, m_ignore, mo_mkpasswd}
};

void _modinit(void)
{
  mod_add_cmd(&test_msgtab);
}

void _moddeinit(void)
{
  mod_del_cmd(&test_msgtab);
}

char *_version = "20010825";

static void m_mkpasswd(struct Client *client_p, struct Client *source_p,
                   int parc, char *parv[])
{
  static time_t last_used = 0;

  if ((last_used + ConfigFileEntry.pace_wait) > CurrentTime)
    {
      /* safe enough to give this on a local connect only */
      sendto_one(source_p,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return;
    }
  else
    {
      last_used = CurrentTime;
    }

  if(parc == 1)
    sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
               me.name, parv[0], "MKPASSWD");
  else
    sendto_one(source_p, ":%s NOTICE %s :Encryption for [%s]:  %s",
               me.name, parv[0], parv[1], crypt(parv[1], make_salt()));
}

/*
** mo_test
**      parv[0] = sender prefix
**      parv[1] = parameter
*/
static void mo_mkpasswd(struct Client *client_p, struct Client *source_p,
                   int parc, char *parv[])
{		 
  if(parc == 1)
    sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
               me.name, parv[0], "MKPASSWD");
  else
    sendto_one(source_p, ":%s NOTICE %s :Encryption for [%s]:  %s",
               me.name, parv[0], parv[1], crypt(parv[1], make_salt()));
}

static char *make_salt(void)
{
  static char salt[3];
  salt[0] = saltChars[random() % 64];
  salt[1] = saltChars[random() % 64];
  salt[2] = '\0';
  return salt;
}
