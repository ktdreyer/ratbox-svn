/************************************************************************
 *   IRC - Internet Relay Chat, contrib/m_map.c
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

#include <assert.h>

#include "config.h"
#include "client.h"
#include "modules.h"
#include "handlers.h"
#include "send.h"

static void mo_map(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[]);
static void map_server(struct Client *source_p, struct Client *server,
                       int depth, int prefix);

struct Message map_msgtab = {
  "MAP", 0, 1, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_map}
};

void
_modinit(void)
{
  mod_add_cmd(&map_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&map_msgtab);
}

char *_version = "20010818";

/*
** mo_opme
**      parv[0] = sender prefix
**      parv[1] = channel
*/
static void mo_map(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  sendto_one(source_p, ":%s NOTICE %s :%-59.59s Hops Users",
             me.name, source_p->name, "Server");

  map_server(source_p, &me, 0, 0);
}

static void map_server(struct Client *source_p, struct Client *server,
                       int depth, int prefix)
{
  struct Client *leaf;
  char buf[BUFSIZE];
  int padding;
  int prefix_next = 1;
  int users = 0;
  struct Client *user;
 
  /* XXX - sigh, is there a quicker way to count the number of users? */
  for( user = server->serv->users; user; user = user->lnext )
    users++;

  /* left align "PaddingServername" in a 60 char collumn */
  padding = 60 - strlen(server->name) - (depth * 3);
  if ( padding < 0 )
    padding = 0;
 
  /* Produce :%s NOTICE %s :%A.21s%.Bs%Cs%d    %d
   * where A = size of indent
   *       B = max length of server name (i.e. 60 - size of indent)
   *       C = size of padding to right of server name
   */
  snprintf(buf, BUFSIZE, ":%%s NOTICE %%s :%%%d.21s%%.%ds%%%ds%%d    %%d",
           (depth * 3), (60 - (depth * 3)), padding);

  /* display server */
  sendto_one( source_p, buf, me.name, source_p ->name,
              (prefix ? "-> " : ""), server->name,
              "", depth, users );

  /* decend into each server linked to this server */
  for ( leaf = server->serv->servers; leaf; leaf = leaf->lnext )
  {
    map_server( source_p, leaf, depth + 1, prefix_next );
    prefix_next = 0; /* only prefix "->" on the first link to each server */
  }
}

