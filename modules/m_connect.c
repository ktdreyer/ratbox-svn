/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_connect.c
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
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "hash.h"
#include "modules.h"

#include <assert.h>
#include <stdlib.h>     /* atoi */

static void mo_connect(struct Client*, struct Client*, int, char**);
static void ms_connect(struct Client*, struct Client*, int, char**);

struct Message connect_msgtab = {
  "CONNECT", 0, 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, ms_connect, mo_connect}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&connect_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&connect_msgtab);
}

char *_version = "20001122";
#endif
/*
 * mo_connect - CONNECT command handler
 * 
 * Added by Jto 11 Feb 1989
 *
 * m_connect
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = port number
 *      parv[3] = remote server
 */
static void mo_connect(struct Client* client_p, struct Client* source_p,
                      int parc, char* parv[])
{
  int              port;
  int              tmpport;
  struct ConfItem* aconf;
  struct Client*   target_p;

  /* always privileged with handlers */

  if (MyConnect(source_p) && !IsOperRemote(source_p) && parc > 3)
    {
      sendto_one(source_p,":%s NOTICE %s :You need remote = yes;", me.name, parv[0]);
      return;
    }

  if (hunt_server(client_p, source_p,
                  ":%s CONNECT %s %s :%s", 3, parc, parv) != HUNTED_ISME)
    {
      return;
    }

  if (*parv[1] == '\0')
    {
      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "CONNECT");
      return;
    }

  if ((target_p = find_server(parv[1])))
    {
      sendto_one(source_p, ":%s NOTICE %s :Connect: Server %s already exists from %s.",
                 me.name, parv[0], parv[1],
                 target_p->from->name);
      return;
    }

  /*
   * try to find the name, then host, if both fail notify ops and bail
   */
  if (!(aconf = find_conf_by_name(parv[1], CONF_SERVER)))
    {
      if (!(aconf = find_conf_by_host(parv[1], CONF_SERVER)))
	{
	  sendto_one(source_p,
		     "NOTICE %s :Connect: Host %s not listed in ircd.conf",
		     parv[0], parv[1]);
	  return;
	}
    }
  assert(0 != aconf);
  /*
   * Get port number from user, if given. If not specified,
   * use the default form configuration structure. If missing
   * from there, then use the precompiled default.
   */
  tmpport = port = aconf->port;
  if (parc > 2 && !EmptyString(parv[2]))
    {
      if ((port = atoi(parv[2])) <= 0)
        {
          sendto_one(source_p, "NOTICE %s :Connect: Illegal port number",
                     parv[0]);
          return;
        }
    }
  else if (port <= 0 && (port = PORTNUM) <= 0)
    {
      sendto_one(source_p, ":%s NOTICE %s :Connect: missing port number",
                 me.name, parv[0]);
      return;
    }
  /*
   * Notify all operators about remote connect requests
   */

  ilog(L_TRACE, "CONNECT From %s : %s %s", 
       parv[0], parv[1], parv[2] ? parv[2] : "");

  aconf->port = port;
  /*
   * at this point we should be calling connect_server with a valid
   * C:line and a valid port in the C:line
   */
  if (serv_connect(aconf, source_p))
    {
      if (IsOperAdmin(source_p))
	sendto_one(source_p, ":%s NOTICE %s :*** Connecting to %s[%s].%d",
		   me.name, parv[0], aconf->host, aconf->name, aconf->port);
      else
	sendto_one(source_p, ":%s NOTICE %s :*** Connecting to %s.%d",
		   me.name, parv[0], aconf->name, aconf->port);

    }
  else
    {
      sendto_one(source_p, ":%s NOTICE %s :*** Couldn't connect to %s.%d",
		 me.name, parv[0], aconf->name,aconf->port);

    }
  /*
   * client is either connecting with all the data it needs or has been
   * destroyed
   */
  aconf->port = tmpport;
}

/*
 * ms_connect - CONNECT command handler
 * 
 * Added by Jto 11 Feb 1989
 *
 * m_connect
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = port number
 *      parv[3] = remote server
 */
static void ms_connect(struct Client* client_p, struct Client* source_p,
                      int parc, char* parv[])
{
  int              port;
  int              tmpport;
  struct ConfItem* aconf;
  struct Client*   target_p;

  if (hunt_server(client_p, source_p,
                  ":%s CONNECT %s %s :%s", 3, parc, parv) != HUNTED_ISME)
    {
      return;
    }

  if (*parv[1] == '\0')
    {
      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "CONNECT");
      return;
    }

  if ((target_p = find_server(parv[1])))
    {
      sendto_one(source_p, ":%s NOTICE %s :Connect: Server %s %s %s.",
                 me.name, parv[0], parv[1], "already exists from",
                 target_p->from->name);
      return;
    }

  /*
   * try to find the name, then host, if both fail notify ops and bail
   */
  if (!(aconf = find_conf_by_name(parv[1], CONF_SERVER))) {
    if (!(aconf = find_conf_by_host(parv[1], CONF_SERVER))) {
      sendto_one(source_p,
                 ":%s NOTICE %s :Connect: Host %s not listed in ircd.conf",
                 me.name, parv[0], parv[1]);
      return;
    }
  }
  assert(0 != aconf);
  /*
   * Get port number from user, if given. If not specified,
   * use the default form configuration structure. If missing
   * from there, then use the precompiled default.
   */
  tmpport = port = aconf->port;
  if (parc > 2 && !EmptyString(parv[2]))
    {
      port = atoi(parv[2]);

      /* if someone sends port 0, and we have a config port.. use it */
      if(port == 0 && aconf->port)
        port = aconf->port;
      else if(port <= 0)
      {
        sendto_one(source_p, ":%s NOTICE %s :Connect: Illegal port number",
                   me.name, parv[0]);
        return;
      }
    }
  else if (port <= 0 && (port = PORTNUM) <= 0)
    {
      sendto_one(source_p, ":%s NOTICE %s :Connect: missing port number",
                 me.name, parv[0]);
      return;
    }
  /*
   * Notify all operators about remote connect requests
   */
  sendto_wallops_flags(FLAGS_WALLOP, &me,
			  "Remote CONNECT %s %d from %s",
			  parv[1], port,
			  get_client_name(source_p, MASK_IP));
  sendto_server(NULL, NULL, NULL, NOCAPS, NOCAPS, NOFLAGS,
                ":%s WALLOPS :Remote CONNECT %s %d from %s",
                me.name, parv[1], port,
                get_client_name(source_p, MASK_IP));


  ilog(L_TRACE, "CONNECT From %s : %s %d", 
       parv[0], parv[1], port);

  aconf->port = port;
  /*
   * at this point we should be calling connect_server with a valid
   * C:line and a valid port in the C:line
   */
  if (serv_connect(aconf, source_p))
    sendto_one(source_p, ":%s NOTICE %s :*** Connecting to %s.%d",
		 me.name, parv[0], aconf->name, aconf->port);
  else
      sendto_one(source_p, ":%s NOTICE %s :*** Couldn't connect to %s.%d",
                 me.name, parv[0], aconf->name,aconf->port);
  /*
   * client is either connecting with all the data it needs or has been
   * destroyed
   */
  aconf->port = tmpport;
}


