/************************************************************************
 *   IRC - Internet Relay Chat, src/s_serv.c
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
#include "s_serv.h"
#include "channel.h"
#include "vchannel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "packet.h"
#include "res.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_stats.h"
#include "s_user.h"
#include "scache.h"
#include "send.h"
#include "client.h"
#include "s_debug.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>

#define MIN_CONN_FREQ 300

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

int MaxConnectionCount = 1;
int MaxClientCount     = 1;

/*
 * list of recognized server capabilities.  "TS" is not on the list
 * because all servers that we talk to already do TS, and the kludged
 * extra argument to "PASS" takes care of checking that.  -orabidoo
 */
struct Capability captab[] = {
/*  name        cap     */ 
  { "QS",       CAP_QS },
  { "EX",       CAP_EX },
  { "CHW",      CAP_CHW },
  { "DE",       CAP_DE },
  { "LL",       CAP_LL },
  { "IE",       CAP_IE },
  { "VCHAN",    CAP_VCHAN },
  { "EOB",      CAP_EOB },
  { 0,   0 }
};

static unsigned long nextFreeMask();
static unsigned long freeMask;
static void server_burst(struct Client *cptr);
static void burst_members(struct Client *cptr, dlink_list *list, int nicksent);
static CNCB serv_connect_callback;


/*
 * my_name_for_link - return wildcard name of my server name 
 * according to given config entry --Jto
 * XXX - this is only called with me.name as name
 */
const char* my_name_for_link(const char* name, struct ConfItem* aconf)
{
  static char          namebuf[HOSTLEN + 1];
  register int         count = aconf->port;
  register const char* start = name;

  if (count <= 0 || count > 5)
    return start;

  while (count-- && name)
    {
      name++;
      name = strchr(name, '.');
    }
  if (!name)
    return start;

  namebuf[0] = '*';
  strncpy_irc(&namebuf[1], name, HOSTLEN - 1);
  namebuf[HOSTLEN] = '\0';
  return namebuf;
}

/*
 * hunt_server - Do the basic thing in delivering the message (command)
 *      across the relays to the specific server (server) for
 *      actions.
 *
 *      Note:   The command is a format string and *MUST* be
 *              of prefixed style (e.g. ":%s COMMAND %s ...").
 *              Command can have only max 8 parameters.
 *
 *      server  parv[server] is the parameter identifying the
 *              target server.
 *
 *      *WARNING*
 *              parv[server] is replaced with the pointer to the
 *              real servername from the matched client (I'm lazy
 *              now --msa).
 *
 *      returns: (see #defines)
 */
int hunt_server(struct Client *cptr, struct Client *sptr, char *command,
                int server, int parc, char *parv[])
{
  struct Client *acptr;
  int wilds;

  /*
   * Assume it's me, if no server
   */
  if (parc <= server || BadPtr(parv[server]) ||
      match(me.name, parv[server]) ||
      match(parv[server], me.name))
    return (HUNTED_ISME);
  /*
   * These are to pickup matches that would cause the following
   * message to go in the wrong direction while doing quick fast
   * non-matching lookups.
   */
  if ((acptr = find_client(parv[server], NULL)))
    if (acptr->from == sptr->from && !MyConnect(acptr))
      acptr = NULL;
  if (!acptr && (acptr = find_server(parv[server])))
    if (acptr->from == sptr->from && !MyConnect(acptr))
      acptr = NULL;

  collapse(parv[server]);
  wilds = (strchr(parv[server], '?') || strchr(parv[server], '*'));

  /*
   * Again, if there are no wild cards involved in the server
   * name, use the hash lookup
   */
  if (!acptr)
    {
      if (!wilds)
        {
          if (!(acptr = find_server(parv[server])))
            {
              sendto_one(sptr, form_str(ERR_NOSUCHSERVER), me.name,
                         parv[0], parv[server]);
              return(HUNTED_NOSUCH);
            }
        }
      else
        {
          for (acptr = GlobalClientList;
               (acptr = next_client(acptr, parv[server]));
               acptr = acptr->next)
            {
              if (acptr->from == sptr->from && !MyConnect(acptr))
                continue;
              /*
               * Fix to prevent looping in case the parameter for
               * some reason happens to match someone from the from
               * link --jto
               */
              if (IsRegistered(acptr) && (acptr != cptr))
                break;
            }
        }
    }

  if (acptr)
    {
      if (IsMe(acptr) || MyClient(acptr))
        return HUNTED_ISME;
      if (!match(acptr->name, parv[server]))
        parv[server] = acptr->name;
      sendto_one(acptr, command, parv[0],
                 parv[1], parv[2], parv[3], parv[4],
                 parv[5], parv[6], parv[7], parv[8]);
      return(HUNTED_PASS);
    } 
  sendto_one(sptr, form_str(ERR_NOSUCHSERVER), me.name,
             parv[0], parv[server]);
  return(HUNTED_NOSUCH);
}

/*
 * try_connections - scan through configuration and try new connections.
 * Returns the calendar time when the next call to this
 * function should be made latest. (No harm done if this
 * is called earlier or later...)
 */
time_t try_connections(time_t currenttime)
{
  struct ConfItem*   aconf;
  struct Client*     cptr;
  int                connecting = FALSE;
  int                confrq;
  time_t             next = 0;
  struct Class*      cltmp;
  struct ConfItem*   con_conf = NULL;
  int                con_class = 0;

  Debug((DEBUG_NOTICE,"Connection check at: %s", myctime(currenttime)));

  for (aconf = ConfigItemList; aconf; aconf = aconf->next )
    {
      /*
       * Also when already connecting! (update holdtimes) --SRB 
       */
      if (!(aconf->status & CONF_CONNECT_SERVER) || aconf->port <= 0)
        continue;
      cltmp = ClassPtr(aconf);
      /*
       * Skip this entry if the use of it is still on hold until
       * future. Otherwise handle this entry (and set it on hold
       * until next time). Will reset only hold times, if already
       * made one successfull connection... [this algorithm is
       * a bit fuzzy... -- msa >;) ]
       */
      if (aconf->hold > currenttime)
        {
          if (next > aconf->hold || next == 0)
            next = aconf->hold;
          continue;
        }

      if ((confrq = get_con_freq(cltmp)) < MIN_CONN_FREQ )
        confrq = MIN_CONN_FREQ;

      aconf->hold = currenttime + confrq;
      /*
       * Found a CONNECT config with port specified, scan clients
       * and see if this server is already connected?
       */
      cptr = find_server(aconf->name);
      
      if (!cptr && (Links(cltmp) < MaxLinks(cltmp)) &&
          (!connecting || (ClassType(cltmp) > con_class)))
        {
          con_class = ClassType(cltmp);
          con_conf = aconf;
          /* We connect only one at time... */
          connecting = TRUE;
        }
      if ((next > aconf->hold) || (next == 0))
        next = aconf->hold;
    }

  if (0 == GlobalSetOptions.autoconn)
    {
      /*
       * auto connects disabled, send message to ops and bail
       */
      if (connecting)
        sendto_realops("Connection to %s[%s] not activated.",
		       con_conf->name, con_conf->host);
      sendto_realops("WARNING AUTOCONN is 0, autoconns are disabled");
      Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
      return next;
    }

  if (connecting)
    {
      if (con_conf->next)  /* are we already last? */
        {
          struct ConfItem**  pconf;
          for (pconf = &ConfigItemList; (aconf = *pconf);
               pconf = &(aconf->next))
            /* 
             * put the current one at the end and
             * make sure we try all connections
             */
            if (aconf == con_conf)
              *pconf = aconf->next;
          (*pconf = con_conf)->next = 0;
        }

      if (!(con_conf->flags & CONF_FLAGS_ALLOW_AUTO_CONN))
        {
          sendto_realops("Connection to %s[%s] not activated, autoconn is off.",
                     con_conf->name, con_conf->host);
          sendto_realops("WARNING AUTOCONN on %s[%s] is disabled",
                     con_conf->name, con_conf->host);
        }
      else
        {
          /*
           * We used to only print this if serv_connect() actually
           * suceeded, but since comm_tcp_connect() can call the callback
           * immediately if there is an error, we were getting error messages
           * in the wrong order. SO, we just print out the activated line,
           * and let serv_connect() / serv_connect_callback() print an
           * error afterwards if it fails.
           *   -- adrian
           */
          sendto_realops("Connection to %s[%s] activated.",
                     con_conf->name, con_conf->host);
          serv_connect(con_conf, 0);
        }
    }
  Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
  return next;
}

/*
 * check_server - check access for a server given its name 
 * (passed in cptr struct). Must check for all C/N lines which have a 
 * name which matches the name given and a host which matches. A host 
 * alias which is the same as the server name is also acceptable in the 
 * host field of a C/N line.
 *  
 *  0 = Access denied
 *  1 = Success
 */
int check_server(struct Client* cptr)
{
  dlink_list *lp;
  struct ConfItem* c_conf = 0;
  struct ConfItem* n_conf = 0;

  assert(0 != cptr);

  if (attach_confs(cptr, cptr->name, 
                   CONF_CONNECT_SERVER | CONF_NOCONNECT_SERVER ) < 2)
    {
      Debug((DEBUG_DNS,"No C/N lines for %s", cptr->name));
      return 0;
    }
  lp = &cptr->localClient->confs;
  /*
   * This code is from the old world order. It should eventually be
   * duplicated somewhere else later!
   *   -- adrian
   */
#if 0
  if (cptr->dns_reply)
    {
      int             i;
      struct hostent* hp   = cptr->dns_reply->hp;
      char*           name = hp->h_name;
      /*
       * if we are missing a C or N line from above, search for
       * it under all known hostnames we have for this ip#.
       */
      for (i = 0, name = hp->h_name; name; name = hp->h_aliases[i++])
        {
          if (!c_conf)
            c_conf = find_conf_host(lp, name, CONF_CONNECT_SERVER );
          if (!n_conf)
            n_conf = find_conf_host(lp, name, CONF_NOCONNECT_SERVER );
          if (c_conf && n_conf)
            {
              strncpy_irc(cptr->host, name, HOSTLEN);
              break;
            }
        }
      for (i = 0; hp->h_addr_list[i]; ++i)
        {
          if (!c_conf)
            c_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  cptr->username, CONF_CONNECT_SERVER);
          if (!n_conf)
            n_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  cptr->username, CONF_NOCONNECT_SERVER);
        }
    }
#endif
  /*
   * Check for C and N lines with the hostname portion the ip number
   * of the host the server runs on. This also checks the case where
   * there is a server connecting from 'localhost'.
   */
  if (!c_conf)
    c_conf = find_conf_host(lp, cptr->host, CONF_CONNECT_SERVER);
  if (!n_conf)
    n_conf = find_conf_host(lp, cptr->host, CONF_NOCONNECT_SERVER);
  /*
   * Attach by IP# only if all other checks have failed.
   * It is quite possible to get here with the strange things that can
   * happen when using DNS in the way the irc server does. -avalon
   */
  if (!c_conf)
    c_conf = find_conf_ip(lp, (char*)& cptr->localClient->ip,
                          cptr->username, CONF_CONNECT_SERVER);
  if (!n_conf)
    n_conf = find_conf_ip(lp, (char*)& cptr->localClient->ip,
                          cptr->username, CONF_NOCONNECT_SERVER);
  /*
   * detach all conf lines that got attached by attach_confs()
   */
  det_confs_butmask(cptr, 0);
  /*
   * if no C or no N lines, then deny access
   */
  if (!c_conf || !n_conf)
    {
      Debug((DEBUG_DNS, "sv_cl: access denied: [%s@%s] c %x n %x",
             cptr->name, cptr->host, c_conf, n_conf));
      return 0;
    }
  /*
   * attach the C and N lines to the client structure for later use.
   */
  attach_conf(cptr, n_conf);
  attach_conf(cptr, c_conf);
  attach_confs(cptr, cptr->name, CONF_HUB | CONF_LEAF);

  if( !(n_conf->flags & CONF_FLAGS_LAZY_LINK) )
    ClearCap(cptr,CAP_LL);

  if(ConfigFileEntry.hub)
    {
      if( n_conf->flags & CONF_FLAGS_LAZY_LINK )
        {
          if(find_conf_by_name(n_conf->name, CONF_HUB))
            {
              /* n line with an H line, must be a typo */
              ClearCap(cptr,CAP_LL);
              sendto_realops("n line with H oops lets not do LazyLink" );
            }
          else
            {
              /* its full folks, 32 leaves? wow. I never thought I'd
               * see the day. Now this will have to be recoded!
               */
              cptr->localClient->serverMask = nextFreeMask();

              if(!cptr->localClient->serverMask)
                {
                  sendto_realops("serverMask is full!");
                  /* try and negotiate a non LL connect */
                  ClearCap(cptr,CAP_LL);
                }
            }
       }
    }


  /*
   * if the C:line doesn't have an IP address assigned put the one from
   * the client socket there
   */ 
  if (INADDR_NONE == c_conf->ipnum.s_addr)
    c_conf->ipnum.s_addr = cptr->localClient->ip.s_addr;

  Debug((DEBUG_DNS,"sv_cl: access ok: [%s]", cptr->host));

  return 1;
}

/*
** send the CAPAB line to a server  -orabidoo
*
*/
void send_capabilities(struct Client* cptr, int can_send)
{
  struct Capability* cap;
  char  msgbuf[BUFSIZE];

  msgbuf[0] = '\0';

  for (cap = captab; cap->name; ++cap)
    {
      if (cap->cap & can_send)
        {
          strcat(msgbuf, cap->name);
          strcat(msgbuf, " ");
        }
    }
  sendto_one(cptr, "CAPAB :%s", msgbuf);
}


void sendnick_TS(struct Client *cptr, struct Client *acptr)
{
  static char ubuf[12];

  if (IsPerson(acptr))
    {
      send_umode(NULL, acptr, 0, SEND_UMODES, ubuf);
      if (!*ubuf)
        {
          ubuf[0] = '+';
          ubuf[1] = '\0';
        }

      sendto_one(cptr, "NICK %s %d %lu %s %s %s %s :%s", acptr->name, 
                 acptr->hopcount + 1, acptr->tsinfo, ubuf,
                 acptr->username, acptr->host,
                 acptr->user->server, acptr->info);
    }
}


/*
 * show_capabilities - show current server capabilities
 *
 * inputs       - pointer to an struct Client
 * output       - pointer to static string
 * side effects - build up string representing capabilities of server listed
 */

const char* show_capabilities(struct Client* acptr)
{
  static char        msgbuf[BUFSIZE];
  struct Capability* cap;

  strcpy(msgbuf,"TS ");
  if (!acptr->localClient->caps)        /* short circuit if no caps */
    return msgbuf;

  for (cap = captab; cap->cap; ++cap)
    {
      if(cap->cap & acptr->localClient->caps)
        {
          strcat(msgbuf, cap->name);
          strcat(msgbuf, " ");
        }
    }
  return msgbuf;
}

/*
 * server_estab
 *
 * inputs       - pointer to a struct Client
 * output       -
 * side effects -
 */

int server_estab(struct Client *cptr)
{
  struct Client*    acptr;
  struct ConfItem*  n_conf;
  struct ConfItem*  c_conf;
  const char*       inpath;
  static char       inpath_ip[HOSTLEN * 2 + USERLEN + 5];
  char*             host;
  char*             encr;
  int               split;
  dlink_node        *m;
  dlink_node        *ptr;

  assert(0 != cptr);
  ClearAccess(cptr);

  strcpy(inpath_ip, get_client_name(cptr, SHOW_IP));
  inpath = get_client_name(cptr, MASK_IP); /* "refresh" inpath with host */
  split = irccmp(cptr->name, cptr->host);
  host = cptr->name;

  if (!(n_conf = find_conf_name(&cptr->localClient->confs,
				host, CONF_NOCONNECT_SERVER)))
    {
      ServerStats->is_ref++;
       sendto_one(cptr,
                 "ERROR :Access denied. No N line for server %s", inpath_ip);
      sendto_realops("Access denied. No N line for server %s", inpath);
      log(L_NOTICE, "Access denied. No N line for server %s", inpath_ip);
      return exit_client(cptr, cptr, cptr, "No N line for server");
    }
  if (!(c_conf = find_conf_name(&cptr->localClient->confs,
				host, CONF_CONNECT_SERVER )))
    {
      ServerStats->is_ref++;
      sendto_one(cptr, "ERROR :Only N (no C) field for server %s", inpath);
      sendto_realops("Only N (no C) field for server %s",inpath);
      log(L_NOTICE, "Only N (no C) field for server %s", inpath_ip);
      return exit_client(cptr, cptr, cptr, "No C line for server");
    }

#ifdef CRYPT_LINK_PASSWORD
  /* use first two chars of the password they send in as salt */

  /* passwd may be NULL. Head it off at the pass... */
  if(*cptr->localClient->passwd && *n_conf->passwd)
    {
      extern  char *crypt();
      encr = crypt(cptr->localClient->passwd, n_conf->passwd);
    }
  else
    encr = "";
#else
  encr = cptr->localClient->passwd;
#endif  /* CRYPT_LINK_PASSWORD */
  if (*n_conf->passwd && 0 != strcmp(n_conf->passwd, encr))
    {
      ServerStats->is_ref++;
      sendto_one(cptr, "ERROR :No Access (passwd mismatch) %s",
                 inpath);
      sendto_realops("Access denied (passwd mismatch) %s", inpath);
      return exit_client(cptr, cptr, cptr, "Bad Password");
    }
  memset((void *)cptr->localClient->passwd, 0,sizeof(cptr->localClient->passwd));

  /* Its got identd , since its a server */
  SetGotId(cptr);

  if(!ConfigFileEntry.hub)
    {
      /* Its easy now, if there is a server in my link list
       * and I'm not a HUB, I can't grow the linklist more than 1
       */
      if (serv_list.head)   
        {
          ServerStats->is_ref++;
          sendto_one(cptr, "ERROR :I'm a leaf not a hub");
          return exit_client(cptr, cptr, cptr, "I'm a leaf");
        }
    }

  if (IsUnknown(cptr))
    {
      if (c_conf->passwd[0])
        sendto_one(cptr,"PASS %s :TS", c_conf->passwd);
      /*
      ** Pass my info to the new server
      */

      send_capabilities(cptr,CAP_MASK|
                      ((n_conf->flags & CONF_FLAGS_LAZY_LINK) ? CAP_LL : 0));
      sendto_one(cptr, "SERVER %s 1 :%s",
                 my_name_for_link(me.name, n_conf), 
                 (me.info[0]) ? (me.info) : "If you see this, this server is misconfigured");
    }
  else
    {
      Debug((DEBUG_INFO, "Check Usernames [%s]vs[%s]",
             n_conf->user, cptr->username));
      if (!match(n_conf->user, cptr->username))
        {
          ServerStats->is_ref++;
          sendto_realops("Username mismatch [%s]v[%s] : %s",
                     n_conf->user, cptr->username,
                     get_client_name(cptr, TRUE));
          sendto_one(cptr, "ERROR :No Username Match");
          return exit_client(cptr, cptr, cptr, "Bad User");
        }
    }

  sendto_one(cptr,"SVINFO %d %d 0 :%lu", TS_CURRENT, TS_MIN, CurrentTime);
  
  det_confs_butmask(cptr, CONF_LEAF|CONF_HUB|CONF_NOCONNECT_SERVER);
  release_client_dns_reply(cptr);
  /*
  ** *WARNING*
  **    In the following code in place of plain server's
  **    name we send what is returned by get_client_name
  **    which may add the "sockhost" after the name. It's
  **    *very* *important* that there is a SPACE between
  **    the name and sockhost (if present). The receiving
  **    server will start the information field from this
  **    first blank and thus puts the sockhost into info.
  **    ...a bit tricky, but you have been warned, besides
  **    code is more neat this way...  --msa
  */
  SetServer(cptr);
  cptr->servptr = &me;
  add_client_to_llist(&(me.serv->servers), cptr);

  Count.server++;
  Count.myserver++;

  /*
   * XXX - this should be in s_bsd
   */
  if (!set_sock_buffers(cptr->fd, READBUF_SIZE))
    report_error(SETBUF_ERROR_MSG, get_client_name(cptr, TRUE), errno);

  /* LINKLIST */
  m = make_dlink_node();
  dlinkAdd(cptr, m, &serv_list);
  
  /* ircd-hybrid-6 can do TS links, and  zipped links*/
  sendto_realops("Link with %s established: (%s) link",
             inpath,show_capabilities(cptr));
  log(L_NOTICE, "Link with %s established: (%s) link",
      inpath_ip, show_capabilities(cptr));

  add_to_client_hash_table(cptr->name, cptr);
  /* doesnt duplicate cptr->serv if allocated this struct already */
  make_server(cptr);
  cptr->serv->up = me.name;
  /* add it to scache */
  find_or_add(cptr->name);
  
  cptr->serv->nline = n_conf;
  cptr->flags2 |= FLAGS2_CBURST;

  /*
  ** Old sendto_serv_but_one() call removed because we now
  ** need to send different names to different servers
  ** (domain name matching) Send new server to other servers.
  */
  for(ptr=serv_list.head;ptr;ptr=ptr->next)
    {
      acptr = ptr->data;

      if (acptr == cptr)
        continue;

      if ((n_conf = acptr->serv->nline) &&
          match(my_name_for_link(me.name, n_conf), cptr->name))
        continue;
      if (split)
        {
          /*
          sendto_one(acptr,":%s SERVER %s 2 :[%s] %s",
                   me.name, cptr->name,
                   cptr->host, cptr->info);
                   */

          /* DON'T give away the IP of the server here
           * if its a hub especially.
           */

          sendto_one(acptr,":%s SERVER %s 2 :%s",
                   me.name, cptr->name,
                   cptr->info);
        }
      else
        sendto_one(acptr,":%s SERVER %s 2 :%s",
                   me.name, cptr->name, cptr->info);
    }

  /*
  ** Pass on my client information to the new server
  **
  ** First, pass only servers (idea is that if the link gets
  ** cancelled beacause the server was already there,
  ** there are no NICK's to be cancelled...). Of course,
  ** if cancellation occurs, all this info is sent anyway,
  ** and I guess the link dies when a read is attempted...? --msa
  ** 
  ** Note: Link cancellation to occur at this point means
  ** that at least two servers from my fragment are building
  ** up connection this other fragment at the same time, it's
  ** a race condition, not the normal way of operation...
  **
  ** ALSO NOTE: using the get_client_name for server names--
  **    see previous *WARNING*!!! (Also, original inpath
  **    is destroyed...)
  */

  n_conf = cptr->serv->nline;
  for (acptr = &me; acptr; acptr = acptr->prev)
    {
      /* acptr->from == acptr for acptr == cptr */
      if (acptr->from == cptr)
        continue;
      if (IsServer(acptr))
        {
          if (match(my_name_for_link(me.name, n_conf), acptr->name))
            continue;
          split = (MyConnect(acptr) &&
                   irccmp(acptr->name, acptr->host));

          /* DON'T give away the IP of the server here
           * if its a hub especially.
           */

          if (split)
            sendto_one(cptr, ":%s SERVER %s %d :%s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1,
                       acptr->info);
            /*
            sendto_one(cptr, ":%s SERVER %s %d :[%s] %s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1,
                       acptr->host, acptr->info);
                       */
          else
            sendto_one(cptr, ":%s SERVER %s %d :%s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1, acptr->info);
        }
    }
  
  server_burst(cptr);

  return 0;
}


/*
 * server_burst
 *
 * inputs       - struct Client pointer server
 *              -
 * output       - none
 * side effects - send a server burst
 * bugs		- still too long
 */
static void server_burst(struct Client *cptr)
{
  struct Client*    acptr;
  dlink_node *l;
  static char   nickissent = 1;
  struct Channel*   chptr;
  struct Channel*   vchan; 
      /*
      ** Send it in the shortened format with the TS, if
      ** it's a TS server; walk the list of channels, sending
      ** all the nicks that haven't been sent yet for each
      ** channel, then send the channel itself -- it's less
      ** obvious than sending all nicks first, but on the
      ** receiving side memory will be allocated more nicely
      ** saving a few seconds in the handling of a split
      ** -orabidoo
      */

  /* On a "lazy link" (version 1 at least) only send the nicks
   * Leafs always have to send nicks plus channels
   */
  if (ConfigFileEntry.hub && IsCapable(cptr, CAP_LL))
    {
     /* LazyLinks version 2, don't send nicks! */
#ifdef LLVER1
      for (acptr = &me; acptr; acptr = acptr->prev)
        if (acptr->from != cptr)
          sendnick_TS(cptr, acptr);
#endif
      return;
    }

  if (!ConfigFileEntry.hub && IsCapable(cptr, CAP_LL))
    {
      for (chptr = GlobalChannelList; chptr; chptr = chptr->nextch)
	{
	  sendto_one(cptr,":%s CBURST %s",
		     me.name, chptr->chname );
	}
    }

  nickissent = 3 - nickissent;
  /* flag used for each nick to check if we've sent it
   * yet - must be different each time and !=0, so we
   * alternate between 1 and 2 -orabidoo
   */
  for (chptr = GlobalChannelList; chptr; chptr = chptr->nextch)
    {
      /* Don't send vchannels twice; vchannels will be
       * sent along as subchannels of the top channel
       */
      
      if(IsVchan(chptr))
	continue;
	  
      burst_members(cptr,&chptr->chanops, nickissent);
      burst_members(cptr,&chptr->voiced, nickissent);
      burst_members(cptr,&chptr->halfops, nickissent);
      burst_members(cptr,&chptr->peons, nickissent);
      send_channel_modes(cptr, chptr);

      if(IsVchanTop(chptr))
	{
	  for ( vchan = chptr->next_vchan; vchan;
		vchan = vchan->next_vchan)
	    {
	      burst_members(cptr,&vchan->chanops, nickissent);
	      burst_members(cptr,&vchan->voiced, nickissent);
	      burst_members(cptr,&vchan->halfops, nickissent);
	      burst_members(cptr,&vchan->peons, nickissent);
	      send_channel_modes(cptr, vchan);
	    }
	}
    }

  /*
  ** also send out those that are not on any channel
  */
  for (acptr = &me; acptr; acptr = acptr->prev)
    {
      if (acptr->nicksent != nickissent)
	{
	  acptr->nicksent = nickissent;
	  if (acptr->from != cptr)
	    sendnick_TS(cptr, acptr);
	}
    }

  cptr->flags2 &= ~FLAGS2_CBURST;

  /* Always send a PING after connect burst is done */
  sendto_one(cptr, "PING :%s", me.name);

  /* XXX maybe `EOB %d %d` where we send lenght of burst and time? */
  if(IsCapable(cptr, CAP_EOB))
    sendto_one(cptr, "EOB", me.name ); 
}

/*
 * burst_members
 * inputs	- pointer to server to send members to
 * 		- dlink_list pointer to membership list to send
 * 		- current nicksent flag
 * output	- current nicksent flag
 * side effects	-
 */
static void burst_members(struct Client *cptr, dlink_list *list, int nickissent)
{
  struct Client *acptr;
  dlink_node *ptr;

  for (ptr = list->head; ptr; ptr = ptr->next)
    {
      acptr = ptr->data;
      if (acptr->nicksent != nickissent)
	{
	  acptr->nicksent = nickissent;
	  if (acptr->from != cptr)
	    sendnick_TS(cptr, acptr);
	}
    }
}

/*
 * set_autoconn - set autoconnect mode
 *
 * inputs       - struct Client pointer to oper requesting change
 *              -
 * output       - none
 * side effects -
 */
void set_autoconn(struct Client *sptr,char *parv0,char *name,int newval)
{
  struct ConfItem *aconf;

  if((aconf= find_conf_by_name(name, CONF_CONNECT_SERVER)))
    {
      if (newval)
        aconf->flags |= CONF_FLAGS_ALLOW_AUTO_CONN;
      else
        aconf->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN;

      sendto_realops(
                 "%s has changed AUTOCONN for %s to %i",
                 parv0, name, newval);
      sendto_one(sptr,
                 ":%s NOTICE %s :AUTOCONN for %s is now set to %i",
                 me.name, parv0, name, newval);
    }
  else
    {
      sendto_one(sptr,
                 ":%s NOTICE %s :Can't find %s",
                 me.name, parv0, name);
    }
}


/*
 * show_servers - send server list to client
 *
 * inputs        - struct Client pointer to client to show server list to
 *               - name of client
 * output        - NONE
 * side effects        -
 */
void show_servers(struct Client *cptr)
{
  struct Client *cptr2;
  int j=0;                /* used to count servers */
  dlink_node *ptr;

  for(ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      cptr2 = ptr->data;

      ++j;
      sendto_one(cptr, ":%s %d %s :%s (%s!%s@%s) Idle: %d",
                 me.name, RPL_STATSDEBUG, cptr->name, cptr2->name,
                 (cptr2->serv->by[0] ? cptr2->serv->by : "Remote."), 
                 "*", "*", CurrentTime - cptr2->lasttime);

      /*
       * NOTE: moving the username and host to the client struct
       * makes the names in the server->user struct no longer available
       * IMO this is not a big problem because as soon as the user that
       * started the connection leaves the user info has to go away
       * anyhow. Simply showing the nick should be enough here.
       * --Bleep
       */ 
    }

  sendto_one(cptr, ":%s %d %s :%d Server%s", me.name, RPL_STATSDEBUG,
             cptr->name, j, (j==1) ? "" : "s");
}

void initServerMask(void)
{
  freeMask = 0xFFFFFFFFL;
}

void restoreUnusedServerMask(unsigned long mask)
{
  struct Channel*   chptr;
  unsigned long clear_mask;

  if(!mask) /* On 0 mask, don't do anything */
    return;

  freeMask |= mask;

  clear_mask = ~mask;

  for (chptr = GlobalChannelList; chptr; chptr = chptr->nextch)
    {
      chptr->lazyLinkChannelExists &= clear_mask;
    }
}

static unsigned long nextFreeMask()
{
  int i;
  unsigned long mask;

  mask = 1;

  for(i=0;i<32;i++)
    {
      if( mask & freeMask )
        {
          freeMask &= ~mask;
          return(mask);
        }
      mask <<= 1;
    }
  return 0L; /* watch this special case ... */
}

/*
 * New server connection code
 * Based upon the stuff floating about in s_bsd.c
 *   -- adrian
 */

/*
 * serv_connect() - initiate a server connection
 *
 * This code initiates a connection to a server. It first checks to make
 * sure the given server exists. If this is the case, it creates a socket,
 * creates a client, saves the socket information in the client, and
 * initiates a connection to the server through comm_connect_tcp(). The
 * completion of this goes through serv_completed_connection().
 *
 * We return 1 if the connection is attempted, since we don't know whether
 * it suceeded or not, and 0 if it fails in here somewhere.
 */
int
serv_connect(struct ConfItem *aconf, struct Client *by)
{
    struct Client *cptr;
    int fd;
    char servname[64]; /* XXX shouldn't this be SERVLEN or something? -- adrian */

    /* Make sure aconf is useful */
    assert(aconf != NULL);

    /* log */
    log(L_NOTICE, "Connect to %s[%s] @%s", aconf->user, aconf->host,
        inetntoa((char *)&aconf->ipnum));

    /*
     * Make sure this server isn't already connected
     * Note: aconf should ALWAYS be a valid C: line
     */
    if ((cptr = find_server(aconf->name))) { 
        sendto_realops("Server %s already present from %s",
          aconf->name, get_client_name(cptr, TRUE));
        if (by && IsPerson(by) && !MyClient(by))
            sendto_one(by, ":%s NOTICE %s :Server %s already present from %s",
              me.name, by->name, aconf->name,
              get_client_name(cptr, TRUE));
        return 0;
    }

    /* XXX we should make sure we're not already connected! */
    strcpy(servname, "Server: ");
    strncat(servname, aconf->name, 64 - 9);

    /* create a socket for the server connection */ 
    if ((fd = comm_open(AF_INET, SOCK_STREAM, 0, servname)) < 0) {
        /* Eek, failure to create the socket */
        report_error("opening stream socket to %s: %s", aconf->name, errno);
        return 0;
    }

    /* Create a client */
    cptr = make_client(NULL);

    /* Copy in the server, hostname, fd */
    strncpy_irc(cptr->name, aconf->name, HOSTLEN);
    strncpy_irc(cptr->host, aconf->host, HOSTLEN);
    cptr->fd = fd;

    /*
     * Set up the initial server evilness, ripped straight from
     * connect_server(), so don't blame me for it being evil.
     *   -- adrian
     */
    strncpy_irc(cptr->localClient->sockhost,
		inetntoa((const char*) &cptr->localClient->ip.s_addr),
      HOSTIPLEN);

    if (!set_non_blocking(cptr->fd))
        report_error(NONB_ERROR_MSG, get_client_name(cptr, TRUE), errno);

    if (!set_sock_buffers(cptr->fd, READBUF_SIZE))
        report_error(SETBUF_ERROR_MSG, get_client_name(cptr, TRUE), errno);

    /*
     * NOTE: if we're here we have a valid C:Line and the client should
     * have started the connection and stored the remote address/port and
     * ip address name in itself
     *
     * Attach config entries to client here rather than in
     * serv_connect_callback(). This to avoid null pointer references.
     */
    if (!attach_cn_lines(cptr, aconf->host)) {
        sendto_realops("Host %s is not enabled for connecting:no C/N-line",
          aconf->host);
        if (by && IsPerson(by) && !MyClient(by))  
            sendto_one(by, ":%s NOTICE %s :Connect to host %s failed.",
              me.name, by->name, cptr);
        det_confs_butmask(cptr, 0);
        free_client(cptr);
        return 0;
    }
    /*
     * at this point we have a connection in progress and C/N lines
     * attached to the client, the socket info should be saved in the
     * client and it should either be resolved or have a valid address.
     *
     * The socket has been connected or connect is in progress.
     */
    make_server(cptr);
    if (by && IsPerson(by)) {
        strcpy(cptr->serv->by, by->name);
        if (cptr->serv->user)
            free_user(cptr->serv->user, NULL);
        cptr->serv->user = by->user;
        by->user->refcnt++;
    } else {
        strcpy(cptr->serv->by, "AutoConn.");
        if (cptr->serv->user)
            free_user(cptr->serv->user, NULL);
        cptr->serv->user = NULL;
    }
    cptr->serv->up = me.name;
    local[cptr->fd] = cptr;
    SetConnecting(cptr);
    add_client_to_list(cptr);

    /* Now, initiate the connection */
    comm_connect_tcp(cptr->fd, aconf->host, aconf->port, NULL, 0, 
        serv_connect_callback, cptr);

    return 1;
}

/*
 * serv_connect_callback() - complete a server connection.
 * 
 * This routine is called after the server connection attempt has
 * completed. If unsucessful, an error is sent to ops and the client
 * is closed. If sucessful, it goes through the initialisation/check
 * procedures, the capabilities are sent, and the socket is then
 * marked for reading.
 */
static void
serv_connect_callback(int fd, int status, void *data)
{
    struct Client *cptr = data;
    struct ConfItem *c_conf;
    struct ConfItem *n_conf;

    /* First, make sure its a real client! */
    assert(cptr != NULL);
    assert(cptr->fd == fd);

    /* Next, for backward purposes, record the ip of the server */
    cptr->localClient->ip = fd_table[fd].connect.hostaddr;

    /* Check the status */
    if (status != COMM_OK)
      {
        /* We have an error, so report it and quit */
        sendto_realops("Error connecting to %s[%s]: %s\n", cptr->name,
		       cptr->host, comm_errstr(status));
        exit_client(cptr, cptr, &me, comm_errstr(status));
        return;
      }

    /* COMM_OK, so continue the connection procedure */
    /* Get the C/N lines */
    c_conf = find_conf_name(&cptr->localClient->confs,
			    cptr->name, CONF_CONNECT_SERVER);
    if (!c_conf)
      { 
        sendto_realops("Lost C-Line for %s", get_client_name(cptr,FALSE));
        exit_client(cptr, cptr, &me, "Lost C-line");
        return;
      }
    n_conf = find_conf_name(&cptr->localClient->confs,
			    cptr->name, CONF_NOCONNECT_SERVER);
    if (!n_conf)
      { 
        sendto_realops("Lost N-Line for %s", get_client_name(cptr,FALSE));
        exit_client(cptr, cptr, &me, "Lost N-Line");
      }

    /* Next, send the initial handshake */
    SetHandshake(cptr);

    if (!EmptyString(c_conf->passwd))
        sendto_one(cptr, "PASS %s :TS", c_conf->passwd);

    send_capabilities(cptr, CAP_MASK|
      ((n_conf->flags & CONF_FLAGS_LAZY_LINK) ? CAP_LL : 0));

    sendto_one(cptr, "SERVER %s 1 :%s",
      my_name_for_link(me.name, n_conf), me.info);

    /* 
     * If we've been marked dead because a send failed, just exit
     * here now and save everyone the trouble of us ever existing.
     */
    if (IsDead(cptr)) {
        sendto_realops("%s[%s] went dead during handshake", cptr->name,
          cptr->host);
        exit_client(cptr, cptr, &me, "Went dead during handshake");
        return;
    }

    /* If we get here, we're ok, so lets start reading some data */
    comm_setselect(fd, FDLIST_SERVER, COMM_SELECT_READ, read_packet, cptr, 0);
}
