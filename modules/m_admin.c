/************************************************************************
 *   IRC - Internet Relay Chat, module/m_admin.c
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
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

static void m_admin(struct Client*, struct Client*, int, char**);
static void mr_admin(struct Client*, struct Client*, int, char**);
static void ms_admin(struct Client*, struct Client*, int, char**);
static void do_admin( struct Client *server_p );

struct Message admin_msgtab = {
  "ADMIN", 0, 0, 0, MFLG_SLOW | MFLG_UNREG, 0, 
  {mr_admin, m_admin, ms_admin, ms_admin}
};

void
_modinit(void)
{
  mod_add_cmd(&admin_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&admin_msgtab);
}

char *_version = "20001202";

/*
 * mr_admin - ADMIN command handler
 *      parv[0] = sender prefix   
 *      parv[1] = servername   
 */
static void mr_admin(struct Client *client_p, struct Client *server_p,
                    int parc, char *parv[])
{
  static time_t last_used=0L;
 
  if((last_used + ConfigFileEntry.pace_wait) > CurrentTime)
    {
      sendto_one(server_p,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return;
    }
  else
    last_used = CurrentTime;

  do_admin(server_p);
}

/*
 * m_admin - ADMIN command handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 */
static void m_admin(struct Client *client_p, struct Client *server_p, int parc,
                   char *parv[])
{
  static time_t last_used=0L;

  if((last_used + ConfigFileEntry.pace_wait) > CurrentTime)
    {
      sendto_one(server_p,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return;
    }
  else
    last_used = CurrentTime;

  if (hunt_server(client_p,server_p,":%s ADMIN :%s",1,parc,parv) != HUNTED_ISME)
    return;

  do_admin(server_p);
}


/*
 * ms_admin - ADMIN command handler, used for OPERS as well
 *      parv[0] = sender prefix
 *      parv[1] = servername
 */
static void ms_admin(struct Client *client_p, struct Client *server_p,
                    int parc, char *parv[])
{
  if (hunt_server(client_p,server_p,":%s ADMIN :%s",1,parc,parv) != HUNTED_ISME)
    return;

  do_admin( server_p );
}


/*
 * do_admin
 *
 * inputs	- pointer to client to report to
 * output	- none
 * side effects	- admin info is sent to client given
 */
static void do_admin( struct Client *server_p )
{
  if (IsPerson(server_p))
    sendto_realops_flags(FLAGS_SPY,
                         "ADMIN requested by %s (%s@%s) [%s]", server_p->name,
                         server_p->username, server_p->host, server_p->user->server);

  sendto_one(server_p, form_str(RPL_ADMINME),
	     me.name, server_p->name, me.name);

  if (AdminInfo.name != NULL)
    sendto_one(server_p, form_str(RPL_ADMINLOC1),
	       me.name, server_p->name, AdminInfo.name);
  if (AdminInfo.description != NULL)
    sendto_one(server_p, form_str(RPL_ADMINLOC2),
	       me.name, server_p->name, AdminInfo.description);

  if (AdminInfo.email != NULL)
    sendto_one(server_p, form_str(RPL_ADMINEMAIL),
	       me.name, server_p->name, AdminInfo.email);
}
