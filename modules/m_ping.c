/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_ping.c
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
#include "send.h"
#include "irc_string.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

static void m_ping(struct Client*, struct Client*, int, char**);
static void ms_ping(struct Client*, struct Client*, int, char**);

struct Message ping_msgtab = {
  "PING", 0, 1, 0, MFLG_SLOW, 0,
  {m_unregistered, m_ping, ms_ping, m_ping}
};

void
_modinit(void)
{
  mod_add_cmd(&ping_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&ping_msgtab);
}

char *_version = "20001122";

/*
** m_ping
**      parv[0] = sender prefix
**      parv[1] = origin
**      parv[2] = destination
*/
static void m_ping(struct Client *client_p,
                  struct Client *source_p,
                  int parc,
                  char *parv[])
{
  struct Client *target_p;
  char  *origin, *destination;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(source_p, form_str(ERR_NOORIGIN), me.name, parv[0]);
      return;
    }
  origin = parv[1];
  destination = parv[2]; /* Will get NULL or pointer (parc >= 2!!) */

  target_p = find_client(origin, NULL);
  if (!target_p)
    target_p = find_server(origin);
  if (target_p && target_p != source_p)
    origin = client_p->name;
  if (!EmptyString(destination) && irccmp(destination, me.name) != 0)
    {
      if ((target_p = find_server(destination)))
        sendto_one(target_p,":%s PING %s :%s", parv[0],
                   origin, destination);
      else
        {
          sendto_one(source_p, form_str(ERR_NOSUCHSERVER),
                     me.name, parv[0], destination);
          return;
        }
    }
  else
    sendto_one(source_p,":%s PONG %s :%s", me.name,
               (destination) ? destination : me.name, origin);
}

static void ms_ping(struct Client *client_p,
                   struct Client *source_p,
                   int parc,
                   char *parv[])
{
  struct Client *target_p;
  char  *origin, *destination;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(source_p, form_str(ERR_NOORIGIN), me.name, parv[0]);
      return;
    }
  origin = parv[1];
  destination = parv[2]; /* Will get NULL or pointer (parc >= 2!!) */

  target_p = find_client(origin, NULL);
  if (!target_p)
    target_p = find_server(origin);
  if (target_p && target_p != source_p)
    origin = client_p->name;
  if (!EmptyString(destination) && irccmp(destination, me.name) != 0)
    {
      if ((target_p = find_server(destination)))
        sendto_one(target_p,":%s PING %s :%s", parv[0],
                   origin, destination);
      else
        {
          sendto_one(source_p, form_str(ERR_NOSUCHSERVER),
                     me.name, parv[0], destination);
          return;
        }
    }
  else
    sendto_one(source_p,":%s PONG %s :%s", me.name,
               (destination) ? destination : me.name, origin);
}

