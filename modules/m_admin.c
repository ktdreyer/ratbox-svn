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
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"

#include "msg.h"

void do_admin( struct Client *sptr );

struct Message admin_msgtab = {
  MSG_ADMIN, 0, 0, MFLG_SLOW | MFLG_UNREG, 0, 
  {mr_admin, m_admin, ms_admin, mo_admin}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_ADMIN, &admin_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_ADMIN);
}

char *_version = "20001202";

/*
 * mr_admin - ADMIN command handler
 *      parv[0] = sender prefix   
 *      parv[1] = servername   
 */
int mr_admin(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  static time_t last_used=0L;
 
  if((last_used + ConfigFileEntry.pace_wait) > CurrentTime)
    {
      sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }
  else
    last_used = CurrentTime;

  do_admin(sptr);

  return 0;
}

/*
 * m_admin - ADMIN command handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 */
int m_admin(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  static time_t last_used=0L;

  if((last_used + ConfigFileEntry.pace_wait) > CurrentTime)
    {
      sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }
  else
    last_used = CurrentTime;

  if (hunt_server(cptr,sptr,":%s ADMIN :%s",1,parc,parv) != HUNTED_ISME)
    return 0;

  do_admin(sptr);

  return 0;
}

/*
 * mo_admin - ADMIN command handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 */
int mo_admin(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  if (hunt_server(cptr,sptr,":%s ADMIN :%s",1,parc,parv) != HUNTED_ISME)
    return 0;

  do_admin( sptr );

  if(ConfigFileEntry.hub)
    sendto_one(sptr, ":%s NOTICE %s :Server is a HUB",
                     me.name,sptr->name);
  else
    sendto_one(sptr, ":%s NOTICE %s :Server is a LEAF",
                     me.name,sptr->name);

  return 0;
}

/*
 * ms_admin - ADMIN command handler, used for OPERS as well
 *      parv[0] = sender prefix
 *      parv[1] = servername
 */
int ms_admin(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  if (hunt_server(cptr,sptr,":%s ADMIN :%s",1,parc,parv) != HUNTED_ISME)
    return 0;

  do_admin( sptr );

  return 0;
}


/*
 * do_admin
 *
 * inputs	- pointer to client to report to
 * output	- none
 * side effects	- admin info is sent to client given
 */
static void do_admin( struct Client *sptr )
{
  struct ConfItem *aconf;

  if (IsPerson(sptr))
    sendto_realops_flags(FLAGS_SPY,
                         "ADMIN requested by %s (%s@%s) [%s]", sptr->name,
                         sptr->username, sptr->host, sptr->user->server);
  if ((aconf = find_admin()))
    {
      sendto_one(sptr, form_str(RPL_ADMINME),
                 me.name, sptr->name, me.name);
      sendto_one(sptr, form_str(RPL_ADMINLOC1),
                 me.name, sptr->name, aconf->host);
      sendto_one(sptr, form_str(RPL_ADMINLOC2),
                 me.name, sptr->name, aconf->passwd);
      sendto_one(sptr, form_str(RPL_ADMINEMAIL),
                 me.name, sptr->name, aconf->user);
    }
  else
    sendto_one(sptr, form_str(ERR_NOADMININFO),
               me.name, sptr->name, me.name);
}
