/************************************************************************
 *   IRC - Internet Relay Chat, src/m_squit.c
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
#include "common.h"      /* FALSE bleah */
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"

#include <assert.h>

struct Message squit_msgtab = {
  MSG_SQUIT, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, ms_squit, mo_squit}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_SQUIT, &squit_msgtab);
}

char *_version = "20001122";

/*
 * ms_squit - SQUIT message handler
 *      parv[0] = sender prefix
 *      parv[1] = server name
 *      parv[2] = comment
 */
int ms_squit(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct ConfItem* aconf;
  char*            server;
  struct Client*   acptr;
  char  *comment = (parc > 2 && parv[2]) ? parv[2] : cptr->name;

  if (!IsPrivileged(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      server = parv[1];
      /*
      ** To accomodate host masking, a squit for a masked server
      ** name is expanded if the incoming mask is the same as
      ** the server name for that link to the name of link.
      */
      while ((*server == '*') && IsServer(cptr))
        {
          aconf = cptr->serv->nline;
          if (!aconf)
            break;
          if (!irccmp(server, my_name_for_link(me.name, aconf)))
            server = cptr->name;
          break; /* WARNING is normal here */
          /* NOTREACHED */
        }
      /*
      ** The following allows wild cards in SQUIT. Only useful
      ** when the command is issued by an oper.
      */
      for (acptr = GlobalClientList; (acptr = next_client(acptr, server));
           acptr = acptr->next)
        if (IsServer(acptr) || IsMe(acptr))
          break;
      if (acptr && IsMe(acptr))
        {
          acptr = cptr;
          server = cptr->host;
        }
    }
  else
    {
      /*
      ** This is actually protocol error. But, well, closing
      ** the link is very proper answer to that...
      **
      ** Closing the client's connection probably wouldn't do much
      ** good.. any oper out there should know that the proper way
      ** to disconnect is /QUIT :)
      **
      ** its still valid if its not a local client, its then
      ** a protocol error for sure -Dianora
      */
      if(MyClient(sptr))
        {
          sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
               me.name, parv[0], "SQUIT");
          return 0;
        }
      else
        {
          server = cptr->host;
          acptr = cptr;
        }
    }

  /*
  ** SQUIT semantics is tricky, be careful...
  **
  ** The old (irc2.2PL1 and earlier) code just cleans away the
  ** server client from the links (because it is never true
  ** "cptr == acptr".
  **
  ** This logic here works the same way until "SQUIT host" hits
  ** the server having the target "host" as local link. Then it
  ** will do a real cleanup spewing SQUIT's and QUIT's to all
  ** directions, also to the link from which the orinal SQUIT
  ** came, generating one unnecessary "SQUIT host" back to that
  ** link.
  **
  ** One may think that this could be implemented like
  ** "hunt_server" (e.g. just pass on "SQUIT" without doing
  ** nothing until the server having the link as local is
  ** reached). Unfortunately this wouldn't work in the real life,
  ** because either target may be unreachable or may not comply
  ** with the request. In either case it would leave target in
  ** links--no command to clear it away. So, it's better just
  ** clean out while going forward, just to be sure.
  **
  ** ...of course, even better cleanout would be to QUIT/SQUIT
  ** dependant users/servers already on the way out, but
  ** currently there is not enough information about remote
  ** clients to do this...   --msa
  */
  if (!acptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHSERVER),
                 me.name, parv[0], server);
      return 0;
    }
  if (IsLocalOper(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (MyClient(sptr) && !IsOperRemote(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no R flag",me.name,parv[0]);
      return 0;
    }

  /*
  **  Notify all opers, if my local link is remotely squitted
  */
  if (MyConnect(acptr) && !IsAnyOper(cptr))
    {
      sendto_realops_butone(NULL, &me,
                        ":%s WALLOPS :Received SQUIT %s from %s (%s)",
                        me.name, server, get_client_name(sptr,FALSE), comment);
      log(L_TRACE, "SQUIT From %s : %s (%s)", parv[0], server, comment);
    }
  else if (MyConnect(acptr))
    sendto_realops("Received SQUIT %s from %s (%s)",
               acptr->name, get_client_name(sptr,FALSE), comment);
  
  return exit_client(cptr, acptr, sptr, comment);
}

/*
 * mo_squit - SQUIT message handler
 *      parv[0] = sender prefix
 *      parv[1] = server name
 *      parv[2] = comment
 */
int mo_squit(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct ConfItem* aconf;
  char*            server;
  struct Client*   acptr;
  char  *comment = (parc > 2 && parv[2]) ? parv[2] : cptr->name;

  if (!IsPrivileged(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      server = parv[1];
      /*
      ** To accomodate host masking, a squit for a masked server
      ** name is expanded if the incoming mask is the same as
      ** the server name for that link to the name of link.
      */
      while ((*server == '*') && IsServer(cptr))
        {
          aconf = cptr->serv->nline;
          if (!aconf)
            break;
          if (!irccmp(server, my_name_for_link(me.name, aconf)))
            server = cptr->name;
          break; /* WARNING is normal here */
          /* NOTREACHED */
        }
      /*
      ** The following allows wild cards in SQUIT. Only useful
      ** when the command is issued by an oper.
      */
      for (acptr = GlobalClientList; (acptr = next_client(acptr, server));
           acptr = acptr->next)
        if (IsServer(acptr) || IsMe(acptr))
          break;
      if (acptr && IsMe(acptr))
        {
          acptr = cptr;
          server = cptr->host;
        }
    }
  else
    {
      /*
      ** This is actually protocol error. But, well, closing
      ** the link is very proper answer to that...
      **
      ** Closing the client's connection probably wouldn't do much
      ** good.. any oper out there should know that the proper way
      ** to disconnect is /QUIT :)
      **
      ** its still valid if its not a local client, its then
      ** a protocol error for sure -Dianora
      */
      if(MyClient(sptr))
        {
          sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
               me.name, parv[0], "SQUIT");
          return 0;
        }
      else
        {
          server = cptr->host;
          acptr = cptr;
        }
    }

  /*
  ** SQUIT semantics is tricky, be careful...
  **
  ** The old (irc2.2PL1 and earlier) code just cleans away the
  ** server client from the links (because it is never true
  ** "cptr == acptr".
  **
  ** This logic here works the same way until "SQUIT host" hits
  ** the server having the target "host" as local link. Then it
  ** will do a real cleanup spewing SQUIT's and QUIT's to all
  ** directions, also to the link from which the orinal SQUIT
  ** came, generating one unnecessary "SQUIT host" back to that
  ** link.
  **
  ** One may think that this could be implemented like
  ** "hunt_server" (e.g. just pass on "SQUIT" without doing
  ** nothing until the server having the link as local is
  ** reached). Unfortunately this wouldn't work in the real life,
  ** because either target may be unreachable or may not comply
  ** with the request. In either case it would leave target in
  ** links--no command to clear it away. So, it's better just
  ** clean out while going forward, just to be sure.
  **
  ** ...of course, even better cleanout would be to QUIT/SQUIT
  ** dependant users/servers already on the way out, but
  ** currently there is not enough information about remote
  ** clients to do this...   --msa
  */
  if (!acptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHSERVER),
                 me.name, parv[0], server);
      return 0;
    }
  if (IsLocalOper(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (MyClient(sptr) && !IsOperRemote(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no R flag",me.name,parv[0]);
      return 0;
    }

  /*
  **  Notify all opers, if my local link is remotely squitted
  */
  if (MyConnect(acptr) && !IsAnyOper(cptr))
    {
      sendto_ops_butone(NULL, &me,
			    ":%s WALLOPS :Received SQUIT %s from %s (%s)",
			    me.name, server, get_client_name(sptr,FALSE), comment);
      log(L_TRACE, "SQUIT From %s : %s (%s)", parv[0], server, comment);
    }
  else if (MyConnect(acptr))
    sendto_realops("Received SQUIT %s from %s (%s)",
		   acptr->name, get_client_name(sptr,FALSE), comment);
  
  return exit_client(cptr, acptr, sptr, comment);
}

