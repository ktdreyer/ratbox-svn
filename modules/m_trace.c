/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_trace.c
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
#include "class.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

#include <string.h>
#include <time.h>

static void ms_trace(struct Client*, struct Client*, int, char**);
static void mo_trace(struct Client*, struct Client*, int, char**);

struct Message trace_msgtab = {
  "TRACE", 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_ignore, ms_trace, mo_trace}
};

void
_modinit(void)
{
  mod_add_cmd(&trace_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&trace_msgtab);
}

static int report_this_status(struct Client *server_p, struct Client *aclient_p,int dow,
                              int link_u_p, int link_u_s);

char *_version = "20010109";

/*
** mo_trace
**      parv[0] = sender prefix
**      parv[1] = servername
*/
static void mo_trace(struct Client *client_p, struct Client *server_p,
                    int parc, char *parv[])
{
  int   i;
  struct Client       *aclient_p = NULL;
  struct Class        *cltmp;
  char  *tname;
  int   doall, link_s[MAXCONNECTIONS], link_u[MAXCONNECTIONS];
  int   cnt = 0, wilds, dow;
  static time_t now;
  dlink_node *ptr;

  if (parc > 2)
    if (hunt_server(client_p, server_p, ":%s TRACE %s :%s", 2, parc, parv))
      return;
  
  if (parc > 1)
    tname = parv[1];
  else
    {
      tname = me.name;
    }

  switch (hunt_server(client_p, server_p, ":%s TRACE :%s", 1, parc, parv))
    {
    case HUNTED_PASS: /* note: gets here only if parv[1] exists */
      {
        struct Client *ac2ptr;
        
        ac2ptr = next_client_double(GlobalClientList, tname);
        if (ac2ptr)
          sendto_one(server_p, form_str(RPL_TRACELINK), me.name, parv[0],
                     version, debugmode, tname, ac2ptr->from->name);
        else
          sendto_one(server_p, form_str(RPL_TRACELINK), me.name, parv[0],
                     version, debugmode, tname, "ac2ptr_is_NULL!!");
        return;
      }
    case HUNTED_ISME:
      break;
    default:
      return;
    }

  if(MyClient(server_p))
    sendto_realops_flags(FLAGS_SPY, "trace requested by %s (%s@%s) [%s]",
                       server_p->name, server_p->username, server_p->host,
                       server_p->user->server);


  doall = (parv[1] && (parc > 1)) ? match(tname, me.name): TRUE;
  wilds = !parv[1] || strchr(tname, '*') || strchr(tname, '?');
  dow = wilds || doall;
  
  if(!IsOper(server_p) || !dow) /* non-oper traces must be full nicks */
                              /* lets also do this for opers tracing nicks */
    {
      const char* name;
      const char* ip;
      const char* class_name;

      aclient_p = hash_find_client(tname,(struct Client *)NULL);
      if(!aclient_p || !IsPerson(aclient_p)) 
        {
          /* this should only be reached if the matching
             target is this server */
          sendto_one(server_p, form_str(RPL_ENDOFTRACE),me.name,
                     parv[0], tname);
          return;
        }
      name = get_client_name(aclient_p, HIDE_IP);
      ip = inetntoa((char*) &aclient_p->localClient->ip);

      class_name = get_client_class(aclient_p);

      if (IsOper(aclient_p))
        {
          sendto_one(server_p, form_str(RPL_TRACEOPERATOR),
                     me.name, parv[0], class_name,
                     name, 
                     IsOper(server_p)?ip:(IsIPHidden(aclient_p)?"127.0.0.1":ip),
                     now - aclient_p->lasttime,
                     (aclient_p->user)?(now - aclient_p->user->last):0);
        }
      else
        {
          sendto_one(server_p,form_str(RPL_TRACEUSER),
                     me.name, parv[0], class_name,
                     name, 
                     IsOper(server_p)?ip:(IsIPHidden(aclient_p)?"127.0.0.1":ip),
                     now - aclient_p->lasttime,
                     (aclient_p->user)?(now - aclient_p->user->last):0);
        }
      sendto_one(server_p, form_str(RPL_ENDOFTRACE),me.name,
                 parv[0], tname);
      return;
    }

  memset((void *)link_s,0,sizeof(link_s));
  memset((void *)link_u,0,sizeof(link_u));

  /*
   * Count up all the servers and clients in a downlink.
   */
  if (doall)
   {
    for (aclient_p = GlobalClientList; aclient_p; aclient_p = aclient_p->next)
     {
      if (IsPerson(aclient_p))
        {
          link_u[aclient_p->from->fd]++;
        }
      else if (IsServer(aclient_p))
	{
	  link_s[aclient_p->from->fd]++;
	}
     }
   }
  /* report all direct connections */
  for ( i = 0, ptr = lclient_list.head; ptr; ptr = ptr->next)
    {
      aclient_p = ptr->data;

      if (IsInvisible(aclient_p) && dow &&
          !(MyConnect(server_p) && IsOper(server_p)) &&
          !IsOper(aclient_p) && (aclient_p != server_p))
        continue;
      if (!doall && wilds && !match(tname, aclient_p->name))
        continue;
      if (!dow && irccmp(tname, aclient_p->name))
        continue;

      cnt = report_this_status(server_p,aclient_p,dow,link_u[i],link_s[i]);
    }

  for ( i = 0, ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      aclient_p = ptr->data;

      if (IsInvisible(aclient_p) && dow &&
          !(MyConnect(server_p) && IsOper(server_p)) &&
          !IsOper(aclient_p) && (aclient_p != server_p))
        continue;
      if (!doall && wilds && !match(tname, aclient_p->name))
        continue;
      if (!dow && irccmp(tname, aclient_p->name))
        continue;

      cnt = report_this_status(server_p,aclient_p,dow,link_u[i],link_s[i]);
    }

  /* This section is to report the unknowns */
  for ( i = 0, ptr = unknown_list.head; ptr; ptr = ptr->next)
    {
      aclient_p = ptr->data;

      if (IsInvisible(aclient_p) && dow &&
          !(MyConnect(server_p) && IsOper(server_p)) &&
          !IsOper(aclient_p) && (aclient_p != server_p))
        continue;
      if (!doall && wilds && !match(tname, aclient_p->name))
        continue;
      if (!dow && irccmp(tname, aclient_p->name))
        continue;

      cnt = report_this_status(server_p,aclient_p,dow,link_u[i],link_s[i]);
    }

  /*
   * Add these lines to summarize the above which can get rather long
   * and messy when done remotely - Avalon
   */
  if (!SendWallops(server_p) || !cnt)
    {
      if (cnt)
        {
          sendto_one(server_p, form_str(RPL_ENDOFTRACE),me.name,
                     parv[0],tname);
          return;
        }
      /* let the user have some idea that its at the end of the
       * trace
       */
      sendto_one(server_p, form_str(RPL_TRACESERVER),
                 me.name, parv[0], 0, link_s[me.fd],
                 link_u[me.fd], me.name, "*", "*", me.name);
      sendto_one(server_p, form_str(RPL_ENDOFTRACE),me.name,
                 parv[0],tname);
      return;
    }
  for (cltmp = ClassList; doall && cltmp; cltmp = cltmp->next)
    if (Links(cltmp) > 0)
      sendto_one(server_p, form_str(RPL_TRACECLASS), me.name,
                 parv[0], ClassName(cltmp), Links(cltmp));
  sendto_one(server_p, form_str(RPL_ENDOFTRACE),me.name, parv[0],tname);
}


/*
** ms_trace
**      parv[0] = sender prefix
**      parv[1] = servername
*/
static void ms_trace(struct Client *client_p, struct Client *server_p,
                    int parc, char *parv[])
{
  if (hunt_server(client_p, server_p, ":%s TRACE %s :%s", 2, parc, parv))
    return;

  if( IsOper(server_p) )
    mo_trace(client_p,server_p,parc,parv);
  return;
}


/*
 * report_this_status
 *
 * inputs	- pointer to client to report to
 * 		- pointer to client to report about
 * output	- counter of number of hits
 * side effects - NONE
 */
static int report_this_status(struct Client *server_p, struct Client *aclient_p,
                              int dow, int link_u_p, int link_s_p)
{
  const char* name;
  const char* class_name;
  const char* ip;
  int cnt=0;
  static time_t now;

  ip = inetntoa((const char*) &aclient_p->localClient->ip);
  name = get_client_name(aclient_p, HIDE_IP);
  class_name = get_client_class(aclient_p);

  now = time(NULL);  

  switch(aclient_p->status)
    {
    case STAT_CONNECTING:
      sendto_one(server_p, form_str(RPL_TRACECONNECTING), me.name,
		 server_p->name, class_name, name);
      cnt++;
      break;
    case STAT_HANDSHAKE:
      sendto_one(server_p, form_str(RPL_TRACEHANDSHAKE), me.name,
		 server_p->name, class_name, name);
      cnt++;
      break;
    case STAT_ME:
      break;
    case STAT_UNKNOWN:
      /* added time -Taner */
      sendto_one(server_p, form_str(RPL_TRACEUNKNOWN),
		 me.name, server_p->name, class_name, name, ip,
		 aclient_p->firsttime ? CurrentTime - aclient_p->firsttime : -1);
      cnt++;
      break;
    case STAT_CLIENT:
      /* Only opers see users if there is a wildcard
       * but anyone can see all the opers.
       */
      if ((IsOper(server_p) &&
	   (MyClient(server_p) || !(dow && IsInvisible(aclient_p))))
	  || !dow || IsOper(aclient_p))
	{
	  if (IsOper(aclient_p))
	    sendto_one(server_p,
		       form_str(RPL_TRACEOPERATOR),
		       me.name,
		       server_p->name, class_name,
		       name, IsOper(server_p)?ip:(IsIPHidden(aclient_p)?"127.0.0.1":ip),
		       now - aclient_p->lasttime,
		       (aclient_p->user)?(now - aclient_p->user->last):0);
	  else
	    sendto_one(server_p,form_str(RPL_TRACEUSER),
		       me.name, server_p->name, class_name,
		       name,
		       IsOper(server_p)?ip:(IsIPHidden(aclient_p)?"127.0.0.1":ip),
		       now - aclient_p->lasttime,
		       (aclient_p->user)?(now - aclient_p->user->last):0);
	  cnt++;
	}
      break;
    case STAT_SERVER:
      sendto_one(server_p, form_str(RPL_TRACESERVER),
		 me.name, server_p->name, class_name, link_s_p,
		 link_u_p, name, *(aclient_p->serv->by) ?
		 aclient_p->serv->by : "*", "*",
		 me.name, now - aclient_p->lasttime);
      cnt++;
      break;
    default: /* ...we actually shouldn't come here... --msa */
      sendto_one(server_p, form_str(RPL_TRACENEWTYPE), me.name,
		 server_p->name, name);
      cnt++;
      break;
    }

  return(cnt);
}
