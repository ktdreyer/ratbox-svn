/***********************************************************************
 *   IRC - Internet Relay Chat, modules/m_server.c
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
#include "tools.h"
#include "handlers.h"  /* m_server prototype */
#include "client.h"      /* client struct */
#include "common.h"      /* TRUE bleah */
#include "hash.h"        /* add_to_client_hash_table */
#include "irc_string.h"  /* strncpy_irc */
#include "ircd.h"        /* me */
#include "list.h"        /* make_server */
#include "numeric.h"     /* ERR_xxx */
#include "s_conf.h"      /* struct ConfItem */
#include "s_serv.h"      /* server_estab, check_server, my_name_for_link */
#include "s_stats.h"     /* ServerStats */
#include "scache.h"      /* find_or_add */
#include "send.h"        /* sendto_one */
#include "msg.h"

#include <string.h>
#include <stdlib.h>

struct Message server_msgtab = {
  MSG_SERVER, 0, 3, MFLG_SLOW | MFLG_UNREG, 0,
  {mr_server, m_registered, ms_server, m_registered}
};

void 
_modinit(void)
{
  mod_add_cmd(MSG_SERVER, &server_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_SERVER);
}

char *parse_server_args(char *parv[], int parc, char *info, int *hop);
int bogus_host(char *host);

char *_version = "20001122";

/*
 * mr_server - SERVER message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = serverinfo/hopcount
 *      parv[3] = serverinfo
 */
int mr_server(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int              i;
  char             info[REALLEN + 1];
  char*            host;
  struct Client*   acptr;
  struct Client*   bcptr;
  struct ConfItem* aconf;
  int              hop;
  dlink_node       *ptr;

  if ( (host = parse_server_args(parv, parc, info, &hop)) == NULL )
    {
      sendto_one(cptr,"ERROR :No servername");
      return 0;
    }

  if (bogus_host(host))
    return exit_client(cptr, cptr, cptr, "Bogus server name");

  /* 
   * *WHEN* can it be that "cptr != sptr" ????? --msa
   * When SERVER command (like now) has prefix. -avalon
   * 
   */
  if (find_conf_by_name(host, CONF_NOCONNECT_SERVER) == NULL)
    {
      if (ConfigFileEntry.warn_no_nline)
	sendto_realops("Link %s Server %s dropped, no N: line",
		       get_client_name(cptr, TRUE), host);
      return exit_client(cptr, cptr, cptr, "NO N line");
    }

  if (GlobalSetOptions.autoconn == 0)
    {
      sendto_realops("WARNING AUTOCONN is 0, Closing %s",
                 get_client_name(cptr, TRUE));
      return exit_client(cptr, cptr, cptr, "AUTOCONNS off");
    }

  if ((acptr = find_server(host)))
    {
      /*
       * This link is trying feed me a server that I already have
       * access through another path -- multiple paths not accepted
       * currently, kill this link immediately!!
       *
       * Rather than KILL the link which introduced it, KILL the
       * youngest of the two links. -avalon
       */
      char nbuf[HOSTLEN * 2 + USERLEN + 5]; /* same size as in s_misc.c */

      bcptr = (cptr->firsttime > acptr->from->firsttime) ? cptr : acptr->from;
      sendto_one(bcptr,"ERROR :Server %s already exists", host);
      if (bcptr == cptr)
      {
        sendto_realops("Link %s cancelled, server %s already exists",
                   get_client_name(bcptr, TRUE), host);
        return exit_client(bcptr, bcptr, &me, "Server Exists");
      }
      /*
       * in this case, we are not dropping the link from
       * which we got the SERVER message.  Thus we canNOT
       * `return' yet! -krys
       *
       *
       * get_client_name() can return ptr to static buffer...can't use
       * 2 times in same sendto_realops(), so we have to strcpy one =(
       *  - comstud
       */
      strcpy(nbuf, get_client_name(bcptr, TRUE));
      sendto_realops("Link %s cancelled, server %s reintroduced by %s",
                nbuf, host, get_client_name(cptr, TRUE));
      exit_client(bcptr, bcptr, &me, "Server Exists");
    }

  if ( strchr(host,'.') == NULL )
    {
      /*
       * Server trying to use the same name as a person. Would
       * cause a fair bit of confusion. Enough to make it hellish
       * for a while and servers to send stuff to the wrong place.
       */
      sendto_one(cptr,"ERROR :Nickname %s already exists!", host);
      sendto_realops("Link %s cancelled: Server/nick collision on %s",
		     get_client_name(cptr,FALSE), host);
      return exit_client(cptr, cptr, cptr, "Nick as Server");
    }

  if (!IsUnknown(cptr) && !IsHandshake(cptr))
    return 0;
  /*
   * A local link that is still in undefined state wants
   * to be a SERVER, or we have gotten here as a result of a connect
   * Check if this is allowed and change status accordingly...
   */

  /* 
   * Reject a direct nonTS server connection if we're TS_ONLY -orabidoo
   */
  if (!DoesTS(cptr))
    {
      sendto_realops("Link %s dropped, non-TS server",
                 get_client_name(cptr, TRUE));
      return exit_client(cptr, cptr, cptr, "Non-TS server");
    }

  /*
   * if we are connecting (Handshake), we already have the name from the
   * C:line in cptr->name
   */
  strncpy_irc(cptr->name, host, HOSTLEN);
  strncpy_irc(cptr->info, info[0] ? info : me.name, REALLEN);
  cptr->hopcount = hop;

  if (check_server(cptr))
    return server_estab(cptr);

  ++ServerStats->is_ref;
  sendto_realops("Received unauthorized connection from %s.",
              get_client_host(cptr));
  return exit_client(cptr, cptr, cptr, "No C/N conf lines");
}

/*
 * ms_server - SERVER message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = serverinfo/hopcount
 *      parv[3] = serverinfo
 */
int ms_server(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int              i;
  char             info[REALLEN + 1];
  char*            host;
  struct Client*   acptr;
  struct Client*   bcptr;
  struct ConfItem* aconf;
  int              hop;
  dlink_node	   *ptr;

  if ( (host = parse_server_args(parv, parc, info, &hop)) == NULL )
    {
      sendto_one(cptr,"ERROR :No servername");
      return 0;
    }

  /* 
   * *WHEN* can it be that "cptr != sptr" ????? --msa
   * When SERVER command (like now) has prefix. -avalon
   */

  if ((acptr = find_server(host)))
    {
      /*
       * This link is trying feed me a server that I already have
       * access through another path -- multiple paths not accepted
       * currently, kill this link immediately!!
       *
       * Rather than KILL the link which introduced it, KILL the
       * youngest of the two links. -avalon
       */
      char nbuf[HOSTLEN * 2 + USERLEN + 5]; /* same size as in s_misc.c */

      bcptr = (cptr->firsttime > acptr->from->firsttime) ? cptr : acptr->from;
      sendto_one(bcptr,"ERROR :Server %s already exists", host);
      if (bcptr == cptr)
	{
	  sendto_realops("Link %s cancelled, server %s already exists",
			 get_client_name(bcptr, TRUE), host);
	  return exit_client(bcptr, bcptr, &me, "Server Exists");
	}
      /*
       * in this case, we are not dropping the link from
       * which we got the SERVER message.  Thus we canNOT
       * `return' yet! -krys
       *
       *
       * get_client_name() can return ptr to static buffer...can't use
       * 2 times in same sendto_realops(), so we have to strcpy one =(
       *  - comstud
       */
      strcpy(nbuf, get_client_name(bcptr, TRUE));
      sendto_realops("Link %s cancelled, server %s reintroduced by %s",
                nbuf, host, get_client_name(cptr, TRUE));
      exit_client(bcptr, bcptr, &me, "Server Exists");
    }

  /* 
   * User nicks never have '.' in them and servers
   * must always have '.' in them.
   */
  if ( strchr(host,'.') == NULL )
    {
      /*
       * Server trying to use the same name as a person. Would
       * cause a fair bit of confusion. Enough to make it hellish
       * for a while and servers to send stuff to the wrong place.
       */
      sendto_one(cptr,"ERROR :Nickname %s already exists!", host);
      sendto_realops("Link %s cancelled: Server/nick collision on %s",
                 /* inpath */ get_client_name(cptr,FALSE), host);
      return exit_client(cptr, cptr, cptr, "Nick as Server");
    }

  /*
   * Server is informing about a new server behind
   * this link. Create REMOTE server structure,
   * add it to list and propagate word to my other
   * server links...
   */
  if (parc == 1 || info[0] == '\0')
    {
      sendto_one(cptr, "ERROR :No server info specified for %s", host);
      return 0;
    }

  /*
   * See if the newly found server is behind a guaranteed
   * leaf (L-line). If so, close the link.
   */
  if ((aconf = find_conf_host(&cptr->localClient->confs, host, CONF_LEAF)) &&
      (!aconf->port || (hop > aconf->port)))
    {
      sendto_realops("Leaf-only link %s->%s - Closing",
                     get_client_name(cptr,  TRUE),
                     aconf->host ? aconf->host : "*");
      sendto_one(cptr, "ERROR :Leaf-only link, sorry.");
      return exit_client(cptr, cptr, cptr, "Leaf Only");
    }

  if (!(aconf = find_conf_host(&cptr->localClient->confs, host, CONF_HUB)) ||
      (aconf->port && (hop > aconf->port)) )
    {
      sendto_realops("Non-Hub link %s introduced %s(%s).",
                     get_client_name(cptr,  TRUE), host,
                     aconf ? (aconf->host ? aconf->host : "*") : "!");
      sendto_one(cptr, "ERROR :%s has no H: line for %s.",
		 get_client_name(cptr,  TRUE), host);
      return exit_client(cptr, cptr, cptr, "Too many servers");
    }

  acptr = make_client(cptr);
  make_server(acptr);
  acptr->hopcount = hop;
  strncpy_irc(acptr->name, host, HOSTLEN);
  strncpy_irc(acptr->info, info, REALLEN);
  acptr->serv->up = find_or_add(parv[0]);
  acptr->servptr = sptr;

  SetServer(acptr);

  Count.server++;

  add_client_to_list(acptr);
  add_to_client_hash_table(acptr->name, acptr);
  add_client_to_llist(&(acptr->servptr->serv->servers), acptr);

  /*
   * Old sendto_serv_but_one() call removed because we now
   * need to send different names to different servers
   * (domain name matching)
   */
  for (ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      bcptr = ptr->data;

      if (bcptr == cptr)
	continue;
      if (!(aconf = bcptr->serv->nline))
	{
	  sendto_realops("Lost N-line for %s on %s. Closing",
                         get_client_name(cptr, TRUE), host);
	  return exit_client(cptr, cptr, cptr, "Lost N line");
	}
      if (match(my_name_for_link(me.name, aconf), acptr->name))
	continue;

      sendto_one(bcptr, ":%s SERVER %s %d :%s",
		 parv[0], acptr->name, hop + 1, acptr->info);
                         
    }
      
  sendto_realops_flags(FLAGS_EXTERNAL, "Server %s being introduced by %s",
		       acptr->name, sptr->name);
  return 0;
}

/*
 * parse_server_args
 *
 * inputs	- parv parameters
 * 		- parc count
 *		- info string (to be filled in by this routine)
 *		- hop count (to be filled in by this routine)
 * output	- NULL if invalid params, hostname otherwise
 * side effects	- parv[1] is trimmed to HOSTLEN size if needed.
 */

char *parse_server_args(char *parv[], int parc, char *info, int *hop)
{
  int i;
  char *host;

  info[0] = '\0';

  if (parc < 2 || *parv[1] == '\0')
    return NULL;

  *hop = 0;

  host = parv[1];

  if (parc > 3 && atoi(parv[2]))
    {
      *hop = atoi(parv[2]);
      strncpy_irc(info, parv[3], REALLEN);
      info[REALLEN] = '\0';
    }
  else if (parc > 2)
    {
      /*
       * XXX - hmmmm
       */
      strncpy_irc(info, parv[2], REALLEN);
      info[REALLEN] = '\0';
      if ((parc > 3) && ((i = strlen(info)) < (REALLEN - 2)))
        {
          strcat(info, " ");
          strncat(info, parv[3], REALLEN - i - 2);
          info[REALLEN] = '\0';
        }
    }

  if (strlen(host) > HOSTLEN)
    host[HOSTLEN] = '\0';

  return(host);
}

/*
 * bogus_host
 *
 * inputs	- hostname
 * output	- 1 if a bogus hostname input, 0 if its valid
 * side effects	- none
 */
int bogus_host(char *host)
{
  int bogus_server = 0;
  char *s;
  int dots = 0;

  for( s = host; *s; s++ )
    {
      if (!IsServChar(*s))
	{
	  bogus_server = 1;
	  break;
	}
      if ('.' == *s)
	++dots;
    }

  if (!dots || bogus_server )
    return 1;

  return 0;
}
