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

#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>

#include "config.h"

#ifdef HAVE_LIBCRYPTO
#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

#include "tools.h"
#include "s_serv.h"
#include "channel.h"
#include "vchannel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "event.h"
#include "fdlist.h"
#include "hash.h"
#include "md5.h"
#include "irc_string.h"
#include "ircd.h"
#include "ircd_defs.h"
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
#include "memory.h"

extern char *crypt();

#define MIN_CONN_FREQ 300

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

int MaxConnectionCount = 1;
int MaxClientCount     = 1;

struct Client *uplink=NULL;

static void        start_io(struct Client *server);
static void        burst_members(struct Client *client_p, dlink_list *list);
static void        burst_ll_members(struct Client *client_p, dlink_list *list);
 
static SlinkRplHnd slink_error;
static SlinkRplHnd slink_zipstats;
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
  { "LL",       CAP_LL },
  { "IE",       CAP_IE },
  { "EOB",      CAP_EOB },
  { "KLN",      CAP_KLN },
  { "GLN",      CAP_GLN },
  { "HOPS",     CAP_HOPS },
  { "HUB",      CAP_HUB },
  { "AOPS",     CAP_AOPS },
  { "UID",      CAP_UID },
  { "ZIP",	CAP_ZIP },

  { 0,           0 }
};

#ifdef HAVE_LIBCRYPTO
struct EncCapability enccaptab[] = {
  {     "BF/128",     CAP_ENC_BF_128, 16,     CIPHER_BF, 2 }, /* ok */
  {     "BF/256",     CAP_ENC_BF_256, 32,     CIPHER_BF, 1 }, /* ok */
  {   "CAST/128",   CAP_ENC_CAST_128, 16,   CIPHER_CAST, 3 }, /* ok */
  {     "DES/56",     CAP_ENC_DES_56,  8,    CIPHER_DES, 9 }, /* XXX */
  {   "3DES/168",   CAP_ENC_3DES_168, 24,   CIPHER_3DES, 8 }, /* XXX */
  {   "IDEA/128",   CAP_ENC_IDEA_128, 16,   CIPHER_IDEA, 4 }, /* ok */
  {  "RC5.8/128",  CAP_ENC_RC5_8_128, 16,  CIPHER_RC5_8, 7 }, /* ok */
  { "RC5.12/128", CAP_ENC_RC5_12_128, 16, CIPHER_RC5_12, 6 }, /* ok */
  { "RC5.16/128", CAP_ENC_RC5_16_128, 16, CIPHER_RC5_16, 5 }, /* ok */

  {            0,                  0,  0,             0, 0 }
};
#endif

struct SlinkRplDef slinkrpltab[] = {
  {    SLINKRPL_ERROR,    slink_error, SLINKRPL_FLAG_DATA },
  { SLINKRPL_ZIPSTATS, slink_zipstats, SLINKRPL_FLAG_DATA },
  {                 0,              0,                  0 },
};

unsigned long nextFreeMask();
static unsigned long freeMask;
static void server_burst(struct Client *client_p);
#ifndef VMS
static int fork_server(struct Client *client_p);
#endif
static void burst_all(struct Client *client_p);
static void cjoin_all(struct Client *client_p);

static CNCB serv_connect_callback;


void slink_error(unsigned int rpl, unsigned int len, unsigned char *data,
                 struct Client *server_p)
{
  assert(rpl == SLINKRPL_ERROR);

  assert(len < 256);
  data[len-1] = '\0';

  sendto_realops_flags(FLAGS_ALL, "SlinkError for %s: %s",
                       server_p->name, data);
  exit_client(server_p, server_p, &me, "servlink error -- terminating link");
}

void slink_zipstats(unsigned int rpl, unsigned int len, unsigned char *data,
                    struct Client *server_p)
{
  unsigned long in = 0, in_wire = 0, out = 0, out_wire = 0;
  double in_ratio = 0, out_ratio = 0;
  int i = 0;
  
  assert(rpl == SLINKRPL_ZIPSTATS);
  assert(len == 16);
  assert(IsCapable(server_p, CAP_ZIP));

  in |= (data[i++] << 24);
  in |= (data[i++] << 16);
  in |= (data[i++] <<  8);
  in |= (data[i++]      );

  in_wire |= (data[i++] << 24);
  in_wire |= (data[i++] << 16);
  in_wire |= (data[i++] <<  8);
  in_wire |= (data[i++]      );

  out |= (data[i++] << 24);
  out |= (data[i++] << 16);
  out |= (data[i++] <<  8);
  out |= (data[i++]      );

  out_wire |= (data[i++] << 24);
  out_wire |= (data[i++] << 16);
  out_wire |= (data[i++] <<  8);
  out_wire |= (data[i++]      );

  in_ratio = (((double)in_wire / (double)in) * 100.00);
  out_ratio = (((double)out_wire/(double)out) * 100.00);

  server_p->localClient->zipstats.in = in;
  server_p->localClient->zipstats.out = out;
  server_p->localClient->zipstats.in_wire = in_wire;
  server_p->localClient->zipstats.out_wire = out_wire;
  server_p->localClient->zipstats.in_ratio = in_ratio;
  server_p->localClient->zipstats.out_ratio = out_ratio;
}

void collect_zipstats(void *unused)
{
  dlink_node *ptr;
  struct Client *target_p;

  for(ptr = serv_list.head; ptr; ptr = ptr->next)
  {
    target_p = ptr->data;
    if (IsCapable(target_p, CAP_ZIP))
    {
      /* only bother if we haven't already got something queued... */
      if (!target_p->localClient->slinkq)
      {
        target_p->localClient->slinkq = MyMalloc(1); /* sigh.. */
        target_p->localClient->slinkq[0] = SLINKCMD_ZIPSTATS;
        target_p->localClient->slinkq_ofs = 0;
        target_p->localClient->slinkq_len = 1;

        /* schedule a write */
        comm_setselect(target_p->localClient->ctrlfd, FDLIST_IDLECLIENT,
                       COMM_SELECT_WRITE, send_queued_slink_write,
                       target_p, 0);
      }
    }
  }
  eventAdd("collect_zipstats", collect_zipstats, NULL,
      ZIPSTATS_TIME, 0);
}

#ifdef HAVE_LIBCRYPTO
struct EncCapability* select_cipher(struct Client *client_p)
{
  struct EncCapability *cipher = NULL;
  struct EncCapability *ecap;
  int priority = -1;

  /* Find the lowest (>0) priority cipher available */
  for (ecap = enccaptab; ecap->name; ecap++)
  {
    if ((ecap->priority > 0) &&         /* enabled */
        IsCapableEnc(client_p, ecap->cap) && /* supported */
        ((ecap->priority < priority) || (priority < 0)))
    {
      /* new best match */
      cipher = ecap;
      priority = ecap->priority;
    }
  }

  return cipher;
}
#endif

/*
 * my_name_for_link - return wildcard name of my server name 
 * according to given config entry --Jto
 * XXX - this is only called with me.name as name
 */
const char* my_name_for_link(const char* name, struct ConfItem* aconf)
{
  if(aconf->fakename)
  	return(aconf->fakename);
  else
	return(name);
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
int hunt_server(struct Client *client_p, struct Client *source_p, char *command,
                int server, int parc, char *parv[])
{
  struct Client *target_p;
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
  if ((target_p = find_client(parv[server], NULL)))
    if (target_p->from == source_p->from && !MyConnect(target_p))
      target_p = NULL;
  if (!target_p && (target_p = find_server(parv[server])))
    if (target_p->from == source_p->from && !MyConnect(target_p))
      target_p = NULL;

  collapse(parv[server]);
  wilds = (strchr(parv[server], '?') || strchr(parv[server], '*'));

  /*
   * Again, if there are no wild cards involved in the server
   * name, use the hash lookup
   */
  if (!target_p)
    {
      if (!wilds)
        {
          if (!(target_p = find_server(parv[server])))
            {
              sendto_one(source_p, form_str(ERR_NOSUCHSERVER), me.name,
                         parv[0], parv[server]);
              return(HUNTED_NOSUCH);
            }
        }
      else
        {
          for (target_p = GlobalClientList;
               (target_p = next_client(target_p, parv[server]));
               target_p = target_p->next)
            {
              if (target_p->from == source_p->from && !MyConnect(target_p))
                continue;
              /*
               * Fix to prevent looping in case the parameter for
               * some reason happens to match someone from the from
               * link --jto
               */
              if (IsRegistered(target_p) && (target_p != client_p))
                break;
            }
        }
    }

  if (target_p)
    {
      if (IsMe(target_p) || MyClient(target_p))
        return HUNTED_ISME;
      if (!match(target_p->name, parv[server]))
        parv[server] = target_p->name;

      /* Deal with lazylinks */
      client_burst_if_needed(target_p, source_p);
      sendto_one(target_p, command, parv[0],
                 parv[1], parv[2], parv[3], parv[4],
                 parv[5], parv[6], parv[7], parv[8]);
      return(HUNTED_PASS);
    } 
  sendto_one(source_p, form_str(ERR_NOSUCHSERVER), me.name,
             parv[0], parv[server]);
  return(HUNTED_NOSUCH);
}

/*
 * try_connections - scan through configuration and try new connections.
 * Returns the calendar time when the next call to this
 * function should be made latest. (No harm done if this
 * is called earlier or later...)
 */
void
try_connections(void *unused)
{
  struct ConfItem*   aconf;
  struct Client*     client_p;
  int                connecting = FALSE;
  int                confrq;
  time_t             next = 0;
  struct Class*      cltmp;
  struct ConfItem*   con_conf = NULL;
  int                con_class = 0;

  Debug((DEBUG_NOTICE,"Connection check at: %s", myctime(CurrentTime)));

  for (aconf = ConfigItemList; aconf; aconf = aconf->next )
    {
      /*
       * Also when already connecting! (update holdtimes) --SRB 
       */
      if (!(aconf->status & CONF_SERVER) || aconf->port <= 0 ||
          !(aconf->flags & CONF_FLAGS_ALLOW_AUTO_CONN))
        continue;
      cltmp = ClassPtr(aconf);
      /*
       * Skip this entry if the use of it is still on hold until
       * future. Otherwise handle this entry (and set it on hold
       * until next time). Will reset only hold times, if already
       * made one successfull connection... [this algorithm is
       * a bit fuzzy... -- msa >;) ]
       */
      if (aconf->hold > CurrentTime)
        {
          if (next > aconf->hold || next == 0)
            next = aconf->hold;
          continue;
        }

      if ((confrq = get_con_freq(cltmp)) < MIN_CONN_FREQ )
        confrq = MIN_CONN_FREQ;

      aconf->hold = CurrentTime + confrq;
      /*
       * Found a CONNECT config with port specified, scan clients
       * and see if this server is already connected?
       */
      client_p = find_server(aconf->name);
      
      if (!client_p && (Links(cltmp) < MaxLinks(cltmp)) &&
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


    if(GlobalSetOptions.autoconn==0)
    {
      /* auto connects disabled, bail */
      goto finish;
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

    if (con_conf->flags & CONF_FLAGS_ALLOW_AUTO_CONN)
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
       sendto_realops_flags(FLAGS_ALL,
    		"Connection to %s[%s] activated.",
	      	con_conf->name, con_conf->host);
      serv_connect(con_conf, 0);
    }
  }
  Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
finish:
  eventAdd("try_connections", try_connections, NULL,
      TRY_CONNECTIONS_TIME, 0);
}

int check_server(const char *name, struct Client* client_p, int cryptlink)
{
  struct ConfItem *aconf=NULL;
  struct ConfItem *server_aconf=NULL;
  int error = -1;

  assert(0 != client_p);

  if (!(client_p->localClient->passwd))
    return -2;

  /* loop through looking for all possible connect items that might work */
  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    {
      if ((aconf->status & CONF_SERVER) == 0)
	continue;

     if (!match(name, aconf->name))
       continue;

     error = -3;

     /* XXX: Fix me for IPv6 */
     /* XXX sockhost is the IPv4 ip as a string */

     if ( match(aconf->host, client_p->host) || 
	  match(aconf->host, client_p->localClient->sockhost) )
       {
	 error = -2;

#ifdef HAVE_LIBCRYPTO
         if (cryptlink && IsConfCryptLink(aconf))
           {
             if (aconf->rsa_public_key)
               server_aconf = aconf;
           }
         else if (!(cryptlink || IsConfCryptLink(aconf)))
#endif /* HAVE_LIBCRYPTO */
           {
             if (IsConfEncrypted(aconf))
               {
                 if (strcmp(aconf->passwd, 
                      crypt(client_p->localClient->passwd, aconf->passwd)) == 0)
                   server_aconf = aconf;
               }
             else
               {
                 if (strcmp(aconf->passwd, client_p->localClient->passwd) == 0)
                   server_aconf = aconf;
               }
           }
       }
    }

  if (server_aconf == NULL)
    return error;

  attach_conf(client_p, server_aconf);

  /* Now find all leaf or hub config items for this server */
  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    {
      if ((aconf->status & (CONF_HUB|CONF_LEAF)) == 0)
	continue;

      if (!match(name, aconf->name))
	continue;

      attach_conf(client_p, aconf);
    }

  if( !(server_aconf->flags & CONF_FLAGS_LAZY_LINK) )
    ClearCap(client_p,CAP_LL);
#ifdef HAVE_LIBZ /* otherwise, clear it unconditionally */
  if( server_aconf->flags & CONF_FLAGS_NOCOMPRESSED )
#endif
    ClearCap(client_p,CAP_ZIP);
  if( !(server_aconf->flags & CONF_FLAGS_CRYPTLINK) )
    ClearCap(client_p,CAP_ENC);

  /*
   * Don't unset CAP_HUB here even if the server isn't a hub,
   * it only indicates if the server thinks it's lazylinks are
   * leafs or not.. if you unset it, bad things will happen
   */
  if(aconf != NULL)
  {
#ifdef IPV6
	  if (IN6_IS_ADDR_UNSPECIFIED((struct in6_addr *)
                                      &IN_ADDR(aconf->ipnum)))
#else
	  if (IN_ADDR(aconf->ipnum) == INADDR_NONE)
#endif
	  copy_s_addr(IN_ADDR(aconf->ipnum), IN_ADDR(client_p->localClient->ip)); 
  }
  return 0;
}

/*
 * check_server - check access for a server given its name 
 * (passed in client_p struct). Must check for all C/N lines which have a 
 * name which matches the name given and a host which matches. A host 
 * alias which is the same as the server name is also acceptable in the 
 * host field of a C/N line.
 *  
 *  0 = Access denied
 *  1 = Success
 */
#if 0
int check_server(struct Client* client_p)
{
  dlink_list *lp;
  struct ConfItem* c_conf = 0;
  struct ConfItem* n_conf = 0;

  assert(0 != client_p);

  if (attach_confs(client_p, client_p->name, 
                   CONF_CONNECT_SERVER | CONF_NOCONNECT_SERVER ) < 2)
    {
      Debug((DEBUG_DNS,"No C/N lines for %s", client_p->name));
      return 0;
    }
  lp = &client_p->localClient->confs;
  /*
   * This code is from the old world order. It should eventually be
   * duplicated somewhere else later!
   *   -- adrian
   */
#if 0
  if (client_p->dns_reply)
    {
      int             i;
      struct hostent* hp   = client_p->dns_reply->hp;
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
              strncpy_irc(client_p->host, name, HOSTLEN);
              break;
            }
        }
      for (i = 0; hp->h_addr_list[i]; ++i)
        {
          if (!c_conf)
            c_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  client_p->username, CONF_CONNECT_SERVER);
          if (!n_conf)
            n_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  client_p->username, CONF_NOCONNECT_SERVER);
        }
    }
#endif
  /*
   * Check for C and N lines with the hostname portion the ip number
   * of the host the server runs on. This also checks the case where
   * there is a server connecting from 'localhost'.
   */
  if (!c_conf)
    c_conf = find_conf_host(lp, client_p->host, CONF_CONNECT_SERVER);
  if (!n_conf)
    n_conf = find_conf_host(lp, client_p->host, CONF_NOCONNECT_SERVER);
  /*
   * Attach by IP# only if all other checks have failed.
   * It is quite possible to get here with the strange things that can
   * happen when using DNS in the way the irc server does. -avalon
   */
  if (!c_conf)
    c_conf = find_conf_ip(lp, (char*)& client_p->localClient->ip,
                          client_p->username, CONF_CONNECT_SERVER);
  if (!n_conf)
    n_conf = find_conf_ip(lp, (char*)& client_p->localClient->ip,
                          client_p->username, CONF_NOCONNECT_SERVER);
  /*
   * detach all conf lines that got attached by attach_confs()
   */
  det_confs_butmask(client_p, 0);
  /*
   * if no C or no N lines, then deny access
   */
  if (!c_conf || !n_conf)
    {
      Debug((DEBUG_DNS, "sv_cl: access denied: [%s@%s] c %x n %x",
             client_p->name, client_p->host, c_conf, n_conf));
      return 0;
    }
  /*
   * attach the C and N lines to the client structure for later use.
   */
  attach_conf(client_p, n_conf);
  attach_conf(client_p, c_conf);
  attach_confs(client_p, client_p->name, CONF_HUB | CONF_LEAF);

  
  if( !(n_conf->flags & CONF_FLAGS_LAZY_LINK) )
    ClearCap(client_p,CAP_LL);

  if(ServerInfo.hub)
    {
      if( n_conf->flags & CONF_FLAGS_LAZY_LINK )
        {
          if(find_conf_by_name(n_conf->name, CONF_HUB) 
	     ||
	     IsCapable(client_p,CAP_HUB))
            {
              ClearCap(client_p,CAP_LL);
              sendto_realops_flags(FLAGS_ALL,
		   "*** LazyLinks to a hub from a hub, thats a no-no.");
            }
          else
            {
              client_p->localClient->serverMask = nextFreeMask();

              if(!client_p->localClient->serverMask)
                {
                  sendto_realops_flags(FLAGS_ALL,
				       "serverMask is full!");
                  /* try and negotiate a non LL connect */
                  ClearCap(client_p,CAP_LL);
                }
            }
       }
    }


  /*
   * if the C:line doesn't have an IP address assigned put the one from
   * the client socket there
   */ 
  if (INADDR_NONE == c_conf->ipnum.s_addr)
    c_conf->ipnum.s_addr = client_p->localClient->ip.s_addr;

  Debug((DEBUG_DNS,"sv_cl: access ok: [%s]", client_p->host));
  return 1;
}
#endif

/*
 * send_capabilities
 *
 * inputs	- Client pointer to send to
 *		- int flag of capabilities that this server has
 * output	- NONE
 * side effects	- send the CAPAB line to a server  -orabidoo
 *
 */
void send_capabilities(struct Client *client_p, int cap_can_send,
                       int enc_can_send )
{
  struct Capability *cap;
  char  msgbuf[BUFSIZE];
  char  *t;
  int   tl;

#ifdef HAVE_LIBCRYPTO
  struct EncCapability *ecap;
  char  *capend;
  int    sent_cipher = 0;
#endif

  t = msgbuf;

  for (cap = captab; cap->name; ++cap)
    {
      if (cap->cap & cap_can_send)
        {
          tl = ircsprintf(t, "%s ", cap->name);
	  t += tl;
        }
    }

#ifdef HAVE_LIBCRYPTO
  if (enc_can_send)
  {
    capend = t;
    strcpy(t, "ENC:");
    t += 4;

    for (ecap = enccaptab; ecap->name; ++ecap)
    {
      if ((ecap->cap & enc_can_send) &&
          (ecap->priority > 0))
      {
        tl = ircsprintf(t, "%s,", ecap->name);
        t += tl;
        sent_cipher = 1;
      }
    }

    if (!sent_cipher)
      t = capend; /* truncate string before ENC:, below */
  }
#endif

  t--;
  *t = '\0';
  sendto_one(client_p, "CAPAB :%s", msgbuf);
}


/*
 * sendnick_TS
 * 
 * inputs	- client (server) to send nick towards
 * 		- client to send nick for
 * output	- NONE
 * side effects	- NICK message is sent towards given client_p
 */
void sendnick_TS(struct Client *client_p, struct Client *target_p)
{
  static char ubuf[12];

  if (!IsPerson(target_p))
	  return;
  
  send_umode(NULL, target_p, 0, SEND_UMODES, ubuf);
  if (!*ubuf)
  {
	  ubuf[0] = '+';
	  ubuf[1] = '\0';
  }
 
  if (HasID(target_p) && IsCapable(client_p, CAP_UID))
	  sendto_one(client_p, "CLIENT %s %d %lu %s %s %s %s %s :%s",
				 target_p->name, target_p->hopcount + 1, target_p->tsinfo, ubuf,
				 target_p->username, target_p->host,
				 target_p->user->server, target_p->user->id, target_p->info);
  else
	  sendto_one(client_p, "NICK %s %d %lu %s %s %s %s :%s",
				 target_p->name, 
				 target_p->hopcount + 1, target_p->tsinfo, ubuf,
				 target_p->username, target_p->host,
				 target_p->user->server, target_p->info);
}

/*
 * client_burst_if_needed
 * 
 * inputs	- pointer to server
 * 		- pointer to client to add
 * output	- NONE
 * side effects - If this client is not known by this lazyleaf, send it
 */
void client_burst_if_needed(struct Client *client_p, struct Client *target_p)
{
  if (!ServerInfo.hub) return;
  if (!MyConnect(client_p)) return;
  if (!IsCapable(client_p,CAP_LL)) return;
 
  if((target_p->lazyLinkClientExists & client_p->localClient->serverMask) == 0)
    {
      sendnick_TS( client_p, target_p );
      add_lazylinkclient(client_p,target_p);
    }
}

/*
 * show_capabilities - show current server capabilities
 *
 * inputs       - pointer to an struct Client
 * output       - pointer to static string
 * side effects - build up string representing capabilities of server listed
 */

const char* show_capabilities(struct Client* target_p)
{
  static char        msgbuf[BUFSIZE];
  struct Capability* cap;
  char *t;
  int  tl;

  t = msgbuf;
  tl = ircsprintf(msgbuf,"TS ");
  t += tl;

  if (!target_p->localClient->caps)        /* short circuit if no caps */
    {
      msgbuf[2] = '\0';
      return msgbuf;
    }

  for (cap = captab; cap->cap; ++cap)
    {
      if(cap->cap & target_p->localClient->caps)
        {
          tl = ircsprintf(t, "%s ", cap->name);
	  t += tl;
        }
    }

#ifdef HAVE_LIBCRYPTO
  if (IsCapable(target_p, CAP_ENC) &&
      target_p->localClient->in_cipher &&
      target_p->localClient->out_cipher)
  {
    tl = ircsprintf(t, "ENC:%s,%s ",
                    target_p->localClient->in_cipher->name,
                    target_p->localClient->out_cipher->name);
    t += tl;
  }
#endif

  t--;
  *t = '\0';

  return msgbuf;
}

/*
 * server_estab
 *
 * inputs       - pointer to a struct Client
 * output       -
 * side effects -
 */

int server_estab(struct Client *client_p)
{
  struct Client*    target_p;
  struct ConfItem*  aconf;
  const char*       inpath;
  static char       inpath_ip[HOSTLEN * 2 + USERLEN + 5];
  char*             host;
  dlink_node        *m;
  dlink_node        *ptr;

  assert(0 != client_p);
  ClearAccess(client_p);

  strcpy(inpath_ip, get_client_name(client_p, SHOW_IP));
  inpath = get_client_name(client_p, MASK_IP); /* "refresh" inpath with host */
  host = client_p->name;

  if (!(aconf = find_conf_name(&client_p->localClient->confs, host,
                               CONF_SERVER)))
    {
     /* This shouldn't happen, better tell the ops... -A1kmm */
     sendto_realops_flags(FLAGS_ALL, "Warning: Lost connect{} block "
       "for server %s(this shouldn't happen)!", host);
     return exit_client(client_p, client_p, client_p, "Lost connect{} block!");
    }
  /* We shouldn't have to check this, it should already done before
   * server_estab is called. -A1kmm */
#if 0
#ifdef CRYPT_LINK_PASSWORD
  /* use first two chars of the password they send in as salt */

  /* passwd may be NULL. Head it off at the pass... */
  if(*client_p->localClient->passwd && *n_conf->passwd)
    {
      extern  char *crypt();
      encr = crypt(client_p->localClient->passwd, n_conf->passwd);
    }
  else
    encr = "";
#else
  encr = client_p->localClient->passwd;
#endif  /* CRYPT_LINK_PASSWORD */
  if (*n_conf->passwd && 0 != strcmp(n_conf->passwd, encr))
    {
      ServerStats->is_ref++;
      sendto_one(client_p, "ERROR :No Access (passwd mismatch) %s",
                 inpath);
      sendto_realops_flags(FLAGS_ALL,
			   "Access denied (passwd mismatch) %s", inpath);
      log(L_NOTICE, "Access denied (passwd mismatch) %s", inpath_ip);
      return exit_client(client_p, client_p, client_p, "Bad Password");
    }
#endif
  memset((void *)client_p->localClient->passwd, 0,sizeof(client_p->localClient->passwd));

  /* Its got identd , since its a server */
  SetGotId(client_p);

  /* If there is something in the serv_list, it might be this
   * connecting server..
   */
  if(!ServerInfo.hub && serv_list.head)   
    {
      if (client_p != serv_list.head->data || serv_list.head->next)
        {
         ServerStats->is_ref++;
         sendto_one(client_p, "ERROR :I'm a leaf not a hub");
         return exit_client(client_p, client_p, client_p, "I'm a leaf");
        }
    }

  if (IsUnknown(client_p) && !IsConfCryptLink(aconf))
    {
      /*
       * jdc -- 1.  Use EmptyString(), not [0] index reference.
       *        2.  Check aconf->spasswd, not aconf->passwd.
       */
      if (!EmptyString(aconf->spasswd))
      {
        sendto_one(client_p,"PASS %s :TS", aconf->spasswd);
      }

      /*
       * Pass my info to the new server
       *
       * If trying to negotiate LazyLinks, pass on CAP_LL
       * If this is a HUB, pass on CAP_HUB
       */

      send_capabilities(client_p,CAP_MASK
             | ((aconf->flags & CONF_FLAGS_LAZY_LINK) ? CAP_LL : 0)
             | (ServerInfo.hub ? CAP_HUB : 0)
             | ((aconf->flags & CONF_FLAGS_NOCOMPRESSED) ? 0:CAP_ZIP_SUPPORTED),
             0);

      sendto_one(client_p, "SERVER %s 1 :%s",
                 my_name_for_link(me.name, aconf), 
                 (me.info[0]) ? (me.info) : "IRCers United");
    }

  /*
   * XXX - this should be in s_bsd
   */
  if (!set_sock_buffers(client_p->fd, READBUF_SIZE))
    report_error(SETBUF_ERROR_MSG, get_client_name(client_p, SHOW_IP), errno);

  /* Hand the server off to servlink now */

#ifndef VMS
  if (IsCapable(client_p, CAP_ENC) || IsCapable(client_p, CAP_ZIP))
    {
      if (fork_server(client_p) < 0 )
      {
        sendto_realops_flags(FLAGS_ALL, "Warning: fork failed for server "
          "%s -- check servlink_path (%s)",
           get_client_name(client_p, HIDE_IP),
           ConfigFileEntry.servlink_path);
        return exit_client(client_p, client_p, client_p, "Fork failed");
      }
      start_io(client_p);
      SetServlink(client_p);
    }
#endif
  sendto_one(client_p,"SVINFO %d %d 0 :%lu", TS_CURRENT, TS_MIN, CurrentTime);
  
  det_confs_butmask(client_p, CONF_LEAF|CONF_HUB|CONF_SERVER);
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
  SetServer(client_p);
  client_p->servptr = &me;

  /* Some day, all these lists will be consolidated *sigh* */
  add_client_to_llist(&(me.serv->servers), client_p);

  m = dlinkFind(&unknown_list, client_p);
  if(m != NULL)
    {
      dlinkDelete(m, &unknown_list);
      dlinkAdd(client_p, m, &serv_list);
    }

  Count.server++;
  Count.myserver++;

  /* Show the real host/IP to admins */
  sendto_realops_flags(FLAGS_ADMIN,
			"Link with %s established: (%s) link",
			inpath_ip,show_capabilities(client_p));

  /* Now show the masked hostname/IP to opers */
  sendto_realops_flags(FLAGS_NOTADMIN,
			"Link with %s established: (%s) link",
			inpath,show_capabilities(client_p));

  log(L_NOTICE, "Link with %s established: (%s) link",
      inpath_ip, show_capabilities(client_p));

  add_to_client_hash_table(client_p->name, client_p);
  /* doesnt duplicate client_p->serv if allocated this struct already */
  make_server(client_p);
  client_p->serv->up = me.name;
  /* add it to scache */
  find_or_add(client_p->name);
  
  client_p->serv->sconf = aconf;
  client_p->flags2 |= FLAGS2_CBURST;

  if (HasServlink(client_p))
    {
      /* we won't overflow FD_DESC_SZ here, as it can hold
       * client_p->name + 64
       */
#ifndef MISSING_SOCKPAIR
      fd_note(client_p->fd, "slink data: %s", client_p->name);
      fd_note(client_p->localClient->ctrlfd, "slink ctrl: %s",
              client_p->name);
#else
      fd_note(client_p->fd, "slink data (out): %s", client_p->name);
      fd_note(client_p->localClient->ctrlfd, "slink ctrl (out): %s",
              client_p->name);
      fd_note(client_p->fd_r, "slink data  (in): %s", client_p->name);
      fd_note(client_p->localClient->ctrlfd_r, "slink ctrl  (in): %s",
              client_p->name);
#endif
    }
  /*
  ** Old sendto_serv_but_one() call removed because we now
  ** need to send different names to different servers
  ** (domain name matching) Send new server to other servers.
  */
  for(ptr=serv_list.head;ptr;ptr=ptr->next)
    {
      target_p = ptr->data;

      if (target_p == client_p)
        continue;

      if ((aconf = target_p->serv->sconf) &&
          match(my_name_for_link(me.name, aconf), client_p->name))
        continue;
      sendto_one(target_p,":%s SERVER %s 2 :%s", me.name, client_p->name,
                 client_p->info);
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

  aconf = client_p->serv->sconf;
  for (target_p = &me; target_p; target_p = target_p->prev)
    {
      /* target_p->from == target_p for target_p == client_p */
      if (target_p->from == client_p)
        continue;
      if (IsServer(target_p))
        {
          if (match(my_name_for_link(me.name, aconf), target_p->name))
            continue;
          sendto_one(client_p, ":%s SERVER %s %d :%s", target_p->serv->up,
                     target_p->name, target_p->hopcount+1, target_p->info);
        }
    }
  
  if(!ServerInfo.hub)
    {
      uplink = client_p;
    }

  server_burst(client_p);

  return 0;
}

static void start_io(struct Client *server)
{
  unsigned char *buf;
  int c = 0;
  int linecount = 0;
  int linelen;

  buf = MyMalloc(256);

  if (IsCapable(server, CAP_ZIP))
  {
    /* ziplink */
    buf[c++] = SLINKCMD_SET_ZIP_OUT_LEVEL;
    buf[c++] = 0; /* |          */
    buf[c++] = 1; /* \ len is 1 */
    buf[c++] = ConfigFileEntry.compression_level;
    buf[c++] = SLINKCMD_START_ZIP_IN;
    buf[c++] = SLINKCMD_START_ZIP_OUT;
  }
#ifdef HAVE_LIBCRYPTO
  if (IsCapable(server, CAP_ENC))
  {
    /* Decryption settings */
    buf[c++] = SLINKCMD_SET_CRYPT_IN_CIPHER;
    buf[c++] = 0; /* upper 8 bits of len */
    buf[c++] = 1; /* lower 8 bits of len */
    buf[c++] = server->localClient->in_cipher->cipherid;
    buf[c++] = SLINKCMD_SET_CRYPT_IN_KEY;
    buf[c++] = 0; /* keylen < 256 */
    buf[c++] = server->localClient->in_cipher->keylen;
    memcpy((buf + c), server->localClient->in_key,
              server->localClient->in_cipher->keylen);
    c += server->localClient->in_cipher->keylen;
    /* Encryption settings */
    buf[c++] = SLINKCMD_SET_CRYPT_OUT_CIPHER;
    buf[c++] = 0; /* /                     */
    buf[c++] = 1; /* \ cipher id is 1 byte */
    buf[c++] = server->localClient->out_cipher->cipherid;
    buf[c++] = SLINKCMD_SET_CRYPT_OUT_KEY;
    buf[c++] = 0; /* keylen < 256 */
    buf[c++] = server->localClient->out_cipher->keylen;
    memcpy((buf + c), server->localClient->out_key,
              server->localClient->out_cipher->keylen);
    c += server->localClient->out_cipher->keylen;
    buf[c++] = SLINKCMD_START_CRYPT_IN;
    buf[c++] = SLINKCMD_START_CRYPT_OUT;
  }
#endif

  while(1)
  {
    linecount++;

    buf = MyRealloc(buf, (c + READBUF_SIZE + 64));
    
    /* store data in c+3 to allow for SLINKCMD_INJECT_RECVQ and len u16 */
    linelen = linebuf_get(&server->localClient->buf_recvq,
                          (char *)(buf + c + 3),
                          READBUF_SIZE, 1); /* include partial lines */

    if (linelen)
    {
      buf[c++] = SLINKCMD_INJECT_RECVQ;
      buf[c++] = (linelen >> 8);
      buf[c++] = (linelen & 0xff);
      c += linelen;
    }
    else
      break;
  }

  while(1)
  {
    linecount++;

    buf = MyRealloc(buf, (c + BUF_DATA_SIZE + 64));

    /* store data in c+3 to allow for SLINKCMD_INJECT_RECVQ and len u16 */
    linelen = linebuf_get(&server->localClient->buf_sendq,
                          (char *)(buf + c + 3),
                          READBUF_SIZE, 1); /* include partial lines */

    if (linelen)
    {
      buf[c++] = SLINKCMD_INJECT_SENDQ;
      buf[c++] = (linelen >> 8);
      buf[c++] = (linelen & 0xff);
      c += linelen;
    }
    else
      break;
  }

  /* start io */
  buf[c++] = SLINKCMD_INIT;

  server->localClient->slinkq = buf;
  server->localClient->slinkq_ofs = 0;
  server->localClient->slinkq_len = c;
 
  /* schedule a write */ 
  comm_setselect(server->localClient->ctrlfd, FDLIST_IDLECLIENT,
                 COMM_SELECT_WRITE, send_queued_slink_write,
                 server, 0);
}

#ifndef VMS
/*
 * fork_server
 *
 * inputs       - struct Client *server
 * output       - success: 0 / failure: -1
 * side effect  - fork, and exec SERVLINK to handle this connection
 */
int fork_server(struct Client *server)
{
  int ret;
  int i;
  int ctrl_pipe[2];
  int data_pipe[2];
#ifdef MISSING_SOCKPAIR
  int ctrl_pipe2[2];
  int data_pipe2[2];
#endif
  int ctrlfd;
  int datafd;
  char *kid_argv[] = { "-slink", NULL };
  

#ifndef MISSING_SOCKPAIR
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, ctrl_pipe) < 0)
    return -1;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, data_pipe) < 0)
    return -1;
#else
  if (pipe(ctrl_pipe) < 0)
    return -1;
  if (pipe(data_pipe) < 0)
    return -1;
  if (pipe(ctrl_pipe2) < 0)
    return -1;
  if (pipe(data_pipe2) < 0)
    return -1;
#endif
  
  if ((ret = vfork()) < 0)
    return -1;
  else if (ret == 0)
  {
    /* Child - use dup2 to override 3/4/5, then close everything else */
    if(dup2(ctrl_pipe[0], 3) < 0)
      exit(1);
    if(dup2(data_pipe[0], 4) < 0)
      exit(1);
    if(dup2(server->fd, 5) < 0)
      return -1;
#ifdef MISSING_SOCKPAIR
    /* only uni-directional pipes, so use 6/7 for writing */
    if(dup2(ctrl_pipe2[1], 6) < 0)
      exit(1);
    if(dup2(data_pipe2[1], 7) < 0)
      exit(1);
#endif

    for(i = 0; i <= LAST_SLINK_FD; i++)
      if(!set_non_blocking(i))
        exit(1);
    for(i = (LAST_SLINK_FD + 1); i < MAXCONNECTIONS; i++)
      close(i);

    /* exec servlink program */
    execv( ConfigFileEntry.servlink_path, kid_argv );

    /* XXX - can we send messages here? it seems dangerous... */
    /* We're still here, abort. */
    exit(1);
  }
  else
  {
    fd_close( server->fd );
    close( ctrl_pipe[0] );
    close( data_pipe[0] );

    assert(server->localClient);
    ctrlfd = server->localClient->ctrlfd = ctrl_pipe[1];
    datafd = server->fd = data_pipe[1];

#ifdef MISSING_SOCKPAIR
    close(ctrl_pipe2[1]);
    close(data_pipe2[1]);
    ctrlfd = server->localClient->ctrlfd_r = ctrl_pipe2[0];
    datafd = server->fd_r = data_pipe2[0];
#endif

    if (!set_non_blocking(server->fd))
        report_error(NONB_ERROR_MSG, get_client_name(server, SHOW_IP), errno);
    if (!set_non_blocking(server->localClient->ctrlfd))
        report_error(NONB_ERROR_MSG, get_client_name(server, SHOW_IP), errno);
#ifdef MISSING_SOCKPAIR
    if (!set_non_blocking(server->fd_r))
        report_error(NONB_ERROR_MSG, get_client_name(server, SHOW_IP), errno);
    if (!set_non_blocking(server->localClient->ctrlfd_r))
        report_error(NONB_ERROR_MSG, get_client_name(server, SHOW_IP), errno);
#endif

#ifndef MISSING_SOCKPAIR
    fd_open(server->localClient->ctrlfd, FD_SOCKET, NULL);
    fd_open(server->fd, FD_SOCKET, NULL);
#else
    fd_open(server->localClient->ctrlfd, FD_PIPE, NULL);
    fd_open(server->fd, FD_PIPE, NULL);
    fd_open(server->localClient->ctrlfd_r, FD_PIPE, NULL);
    fd_open(server->fd_r, FD_PIPE, NULL);
#endif

    comm_setselect(datafd, FDLIST_SERVER, COMM_SELECT_READ, read_packet,
                   server, 0);
    comm_setselect(ctrlfd, FDLIST_SERVER, COMM_SELECT_READ, read_ctrl_packet,
                   server, 0);
    return 0;
  }
}
#endif

/*
 * server_burst
 *
 * inputs       - struct Client pointer server
 *              -
 * output       - none
 * side effects - send a server burst
 * bugs		- still too long
 */
static void server_burst(struct Client *client_p)
{

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

  /* On a "lazy link" hubs send nothing.
   * Leafs always have to send nicks plus channels
   */
  if( IsCapable(client_p, CAP_LL) )
    {
      if(!ServerInfo.hub)
	{
	  /* burst all our info */
	  burst_all(client_p);
	  /* Now, ask for channel info on all our current channels */
	  cjoin_all(client_p);
	}
    }
  else
    {
      burst_all(client_p);
    }
  client_p->flags2 &= ~FLAGS2_CBURST;

  /* EOB stuff is now in burst_all */

  /* Always send a PING after connect burst is done */
  sendto_one(client_p, "PING :%s", me.name);

}

/*
 * burst_all
 *
 * inputs	- pointer to server to send burst to 
 * output	- NONE
 * side effects - complete burst of channels/nicks is sent to client_p
 */
static void
burst_all(struct Client *client_p)
{
  struct Client*    target_p;
  struct Channel*   chptr;
  struct Channel*   vchan; 
  dlink_node *ptr;
  time_t StartBurst=CurrentTime;

  /* serial counter borrowed from send.c */
  current_serial++;

  for (chptr = GlobalChannelList; chptr; chptr = chptr->nextch)
    {
      /* Don't send vchannels twice; vchannels will be
       * sent along as subchannels of the top channel
       */
      
      if(IsVchan(chptr))
	continue;
	  
      if(chptr->users != 0)
        {
          burst_members(client_p,&chptr->chanops);
          burst_members(client_p,&chptr->voiced);
          burst_members(client_p,&chptr->halfops);
          burst_members(client_p,&chptr->peons);
          send_channel_modes(client_p, chptr);
        }

      if(IsVchanTop(chptr))
	{
	  for ( ptr = chptr->vchan_list.head; ptr;
		ptr = ptr->next)
	    {
	      vchan = ptr->data;
              if(vchan->users != 0)
                {
                  burst_members(client_p,&vchan->chanops);
                  burst_members(client_p,&vchan->voiced);
                  burst_members(client_p,&vchan->halfops);
                  burst_members(client_p,&vchan->peons);
                  send_channel_modes(client_p, vchan);
                }
	    }
	}
    }

  /*
  ** also send out those that are not on any channel
  */
  for (target_p = &me; target_p; target_p = target_p->prev)
    {
      if (target_p->serial != current_serial)
	{
	  target_p->serial = current_serial;
	  if (target_p->from != client_p)
	    sendnick_TS(client_p, target_p);
	}
    }

  /* We send the time we started the burst, and let the remote host determine an EOB time,
  ** as otherwise we end up sending a EOB of 0   Sending here means it gets sent last -- fl
  */

  if(IsCapable(client_p, CAP_EOB))
    sendto_one(client_p, ":%s EOB %lu", me.name, StartBurst);
}

/*
 * cjoin_all
 *
 * inputs       - server to ask for channel info from
 * output       - NONE
 * side effects	- CJOINS for all the leafs known channels is sent
 */
static void
cjoin_all(struct Client *client_p)
{
  struct Channel *chptr;

  for (chptr = GlobalChannelList; chptr; chptr = chptr->nextch)
    {
      sendto_one(client_p, ":%s CBURST %s",
		 me.name, chptr->chname);
    }
}

/*
 * burst_channel
 *
 * inputs	- pointer to server to send sjoins to
 *              - channel pointer
 * output	- none
 * side effects	- All sjoins for channel(s) given by chptr are sent
 *                for all channel members. If channel has vchans, send
 *                them on. ONLY called by hub on behalf of a lazylink
 *		  so client_p is always guaranteed to be a LL leaf.
 */
void
burst_channel(struct Client *client_p, struct Channel *chptr)
{
  dlink_node        *ptr;
  struct Channel*   vchan;

  burst_ll_members(client_p,&chptr->chanops);
  burst_ll_members(client_p,&chptr->voiced);
  burst_ll_members(client_p,&chptr->halfops);
  burst_ll_members(client_p,&chptr->peons);
  send_channel_modes(client_p, chptr);
  add_lazylinkchannel(client_p,chptr);

  if(chptr->topic[0])
    {
      sendto_one(client_p, ":%s TOPIC %s %s %lu :%s",
		 me.name, chptr->chname,
		 chptr->topic_info,chptr->topic_time,
		 chptr->topic);
    }

  if(IsVchanTop(chptr))
    {
      for ( ptr = chptr->vchan_list.head; ptr; ptr = ptr->next)
	{
	  vchan = ptr->data;
	  burst_ll_members(client_p,&vchan->chanops);
	  burst_ll_members(client_p,&vchan->voiced);
	  burst_ll_members(client_p,&vchan->halfops);
	  burst_ll_members(client_p,&vchan->peons);
	  send_channel_modes(client_p, vchan);
	  add_lazylinkchannel(client_p,vchan);

	  if(vchan->topic[0])
	    {
	      sendto_one(client_p, ":%s TOPIC %s %s %lu :%s",
			 me.name, vchan->chname,
			 vchan->topic_info,vchan->topic_time,
			 vchan->topic);
	    }
	}
    }
}

/*
 * add_lazylinkchannel
 *
 * inputs	- pointer to server being introduced to this hub
 *		- pointer to channel structure being introduced
 * output	- NONE
 * side effects	- The channel pointed to by chptr is now known
 *		  to be on lazyleaf server given by client_p.
 *		  mark that in the bit map and add to the list
 *		  of channels to examine after this newly introduced
 *		  server is squit off.
 */
void
add_lazylinkchannel(struct Client *client_p, struct Channel *chptr)
{
  dlink_node *m;

  assert(client_p->localClient != NULL);

  chptr->lazyLinkChannelExists |= client_p->localClient->serverMask;

  m = make_dlink_node();

  dlinkAdd(chptr, m, &lazylink_channels);
}

/*
 * add_lazylinkclient
 *
 * inputs       - pointer to server being introduced to this hub
 *              - pointer to client being introduced
 * output       - NONE
 * side effects - The client pointed to by source_p is now known
 *                to be on lazyleaf server given by client_p.
 *                mark that in the bit map and add to the list
 *                of clients to examine after this newly introduced
 *                server is squit off.
 */
void
add_lazylinkclient(struct Client *client_p, struct Client *source_p)
{
  dlink_node *m;

  assert(client_p->localClient != NULL);

  source_p->lazyLinkClientExists |= client_p->localClient->serverMask;

  m = make_dlink_node();

  dlinkAdd(source_p, m, &lazylink_nicks);
}

/*
 * remove_lazylink_flags
 *
 * inputs	- pointer to server quitting
 * output	- NONE
 * side effects	- All the channels on the lazylink channel list are examined
 *		  If they hold a bit corresponding to the servermask
 *		  attached to client_p, clear that bit. If this bitmask
 *		  goes to 0, then the channel is no longer known to
 *		  be on any lazylink server, and can be removed from the 
 *		  link list.
 *
 *		  Similar is done for lazylink clients
 *
 *		  This function must be run by the HUB on any exiting
 *		  lazylink leaf server, while the pointer is still valid.
 *		  Hence must be run from client.c in exit_one_client()
 *
 *		  The old code scanned all channels, this code only
 *		  scans channels/clients on the lazylink_channels
 *		  lazylink_clients lists.
 */
void
remove_lazylink_flags(unsigned long mask)
{
  dlink_node *ptr;
  dlink_node *next_ptr;
  struct Channel *chptr;
  struct Client *target_p;
  unsigned long clear_mask;

  if(!mask) /* On 0 mask, don't do anything */
    return;

  clear_mask = ~mask;

  freeMask |= mask;

  for (ptr = lazylink_channels.head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;

      chptr = ptr->data;
      chptr->lazyLinkChannelExists &= clear_mask;

      if ( chptr->lazyLinkChannelExists == 0 )
	{
	  dlinkDelete(ptr, &lazylink_channels);
	  free_dlink_node(ptr);
	}
    }

  for (ptr = lazylink_nicks.head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;

      target_p = ptr->data;
      target_p->lazyLinkClientExists &= clear_mask;

      if ( target_p->lazyLinkClientExists == 0 )
        {
          dlinkDelete(ptr, &lazylink_nicks);
          free_dlink_node(ptr);
        }
    }
}

/*
 * burst_members
 *
 * inputs	- pointer to server to send members to
 * 		- dlink_list pointer to membership list to send
 * output	- NONE
 * side effects	-
 */
void burst_members(struct Client *client_p, dlink_list *list)
{
  struct Client *target_p;
  dlink_node *ptr;

  for (ptr = list->head; ptr; ptr = ptr->next)
    {
      target_p = ptr->data;
      if (target_p->serial != current_serial)
	{
	  target_p->serial = current_serial;
	  if (target_p->from != client_p)
	    sendnick_TS(client_p, target_p);
	}
    }
}

/*
 * burst_ll_members
 *
 * inputs	- pointer to server to send members to
 * 		- dlink_list pointer to membership list to send
 * output	- NONE
 * side effects	- This version also has to check the bitmap for lazylink
 */
void burst_ll_members(struct Client *client_p, dlink_list *list)
{
  struct Client *target_p;
  dlink_node *ptr;

  for (ptr = list->head; ptr; ptr = ptr->next)
    {
      target_p = ptr->data;
      if ((target_p->lazyLinkClientExists & client_p->localClient->serverMask) == 0)
        {
          if (target_p->from != client_p)
	    {
	      add_lazylinkclient(client_p,target_p);
	      sendnick_TS(client_p, target_p);
	    }
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
void set_autoconn(struct Client *source_p,char *parv0,char *name,int newval)
{
  struct ConfItem *aconf;

  if(name && (aconf= find_conf_by_name(name, CONF_SERVER)))
    {
      if (newval)
        aconf->flags |= CONF_FLAGS_ALLOW_AUTO_CONN;
      else
        aconf->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN;

      sendto_realops_flags(FLAGS_ALL,
			   "%s has changed AUTOCONN for %s to %i",
			   parv0, name, newval);
      sendto_one(source_p,
                 ":%s NOTICE %s :AUTOCONN for %s is now set to %i",
                 me.name, parv0, name, newval);
    }
  else if (name)
    {
      sendto_one(source_p,
                 ":%s NOTICE %s :Can't find %s",
                 me.name, parv0, name);
    }
  else
    {
      sendto_one(source_p,
                 ":%s NOTICE %s :Please specify a server name!",
                 me.name, parv0);
    }
}


/*
 * show_servers - send server list to client
 *
 * inputs        - struct Client pointer to client to show server list to
 *               - name of client
 * output        - NONE
 * side effects  - NONE
 */
void show_servers(struct Client *client_p)
{
  struct Client *client_p2;
  dlink_node *ptr;
  int j=0;                /* used to count servers */

  for(ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      client_p2 = ptr->data;

      ++j;
      sendto_one(client_p, ":%s %d %s :%s (%s!%s@%s) Idle: %d",
                 me.name, RPL_STATSDEBUG, client_p->name, client_p2->name,
                 (client_p2->serv->by[0] ? client_p2->serv->by : "Remote."), 
                 "*", "*", (int)(CurrentTime - client_p2->lasttime));

      /*
       * NOTE: moving the username and host to the client struct
       * makes the names in the server->user struct no longer available
       * IMO this is not a big problem because as soon as the user that
       * started the connection leaves the user info has to go away
       * anyhow. Simply showing the nick should be enough here.
       * --Bleep
       */ 
    }

  sendto_one(client_p, ":%s %d %s :%d Server%s", me.name, RPL_STATSDEBUG,
             client_p->name, j, (j==1) ? "" : "s");
}

void initServerMask(void)
{
  freeMask = 0xFFFFFFFFL;
}

/*
 * nextFreeMask
 *
 * inputs	- NONE
 * output	- unsigned long next unused mask for use in LL
 * side effects	-
 */
unsigned long nextFreeMask()
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
 * inputs	- pointer to conf 
 *		- pointer to client doing the connet
 * output	-
 * side effects	-
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
    struct Client *client_p;
    int fd;
    char buf[HOSTIPLEN];
    /* Make sure aconf is useful */
    assert(aconf != NULL);

    /* log */
    inetntop(DEF_FAM, &IN_ADDR(aconf->ipnum), buf, HOSTIPLEN);
    log(L_NOTICE, "Connect to %s[%s] @%s", aconf->user, aconf->host,
         buf);

    /*
     * Make sure this server isn't already connected
     * Note: aconf should ALWAYS be a valid C: line
     */
    if ((client_p = find_server(aconf->name)))
      { 
        sendto_realops_flags(FLAGS_ALL,
			     "Server %s already present from %s",
			     aconf->name, get_client_name(client_p, SHOW_IP));
        if (by && IsPerson(by) && !MyClient(by))
	  sendto_one(by, ":%s NOTICE %s :Server %s already present from %s",
		     me.name, by->name, aconf->name,
		     get_client_name(client_p, SHOW_IP));
        return 0;
      }

    /* create a socket for the server connection */ 
    if ((fd = comm_open(DEF_FAM, SOCK_STREAM, 0, NULL)) < 0)
      {
        /* Eek, failure to create the socket */
        report_error("opening stream socket to %s: %s", aconf->name, errno);
        return 0;
      }

    /* servernames are always guaranteed under HOSTLEN chars */
    fd_note(fd, "Server: %s", aconf->name);

    /* Create a local client */
    client_p = make_client(NULL);

    /* Copy in the server, hostname, fd */
    strncpy_irc(client_p->name, aconf->name, HOSTLEN);
    strncpy_irc(client_p->host, aconf->host, HOSTLEN);
    inetntop(DEF_FAM, &IN_ADDR(aconf->ipnum), client_p->localClient->sockhost, HOSTIPLEN);
    client_p->fd = fd;

    /*
     * Set up the initial server evilness, ripped straight from
     * connect_server(), so don't blame me for it being evil.
     *   -- adrian
     */

    if (!set_non_blocking(client_p->fd))
        report_error(NONB_ERROR_MSG, get_client_name(client_p, SHOW_IP), errno);

    if (!set_sock_buffers(client_p->fd, READBUF_SIZE))
        report_error(SETBUF_ERROR_MSG, get_client_name(client_p, SHOW_IP), errno);

    /*
     * NOTE: if we're here we have a valid C:Line and the client should
     * have started the connection and stored the remote address/port and
     * ip address name in itself
     *
     * Attach config entries to client here rather than in
     * serv_connect_callback(). This to avoid null pointer references.
     */
    if (!attach_cn_lines(client_p, aconf->name, aconf->host))
      {
        sendto_realops_flags(FLAGS_ALL,
			   "Host %s is not enabled for connecting:no C/N-line",
			     aconf->name);
        if (by && IsPerson(by) && !MyClient(by))  
            sendto_one(by, ":%s NOTICE %s :Connect to host %s failed.",
              me.name, by->name, client_p->name);
        det_confs_butmask(client_p, 0);
        free_client(client_p);
        return 0;
      }
    /*
     * at this point we have a connection in progress and C/N lines
     * attached to the client, the socket info should be saved in the
     * client and it should either be resolved or have a valid address.
     *
     * The socket has been connected or connect is in progress.
     */
    make_server(client_p);
    if (by && IsPerson(by))
      {
        strcpy(client_p->serv->by, by->name);
        if (client_p->serv->user)
            free_user(client_p->serv->user, NULL);
        client_p->serv->user = by->user;
        by->user->refcnt++;
      }
    else
      {
        strcpy(client_p->serv->by, "AutoConn.");
        if (client_p->serv->user)
            free_user(client_p->serv->user, NULL);
        client_p->serv->user = NULL;
      }
    client_p->serv->up = me.name;
    SetConnecting(client_p);
    add_client_to_list(client_p);
    client_p->localClient->aftype = DEF_FAM;
    /* Now, initiate the connection */
    if(ServerInfo.specific_virtual_host)
      {
	struct irc_sockaddr ipn;
	memset(&ipn, 0, sizeof(struct irc_sockaddr));
	S_FAM(ipn) = DEF_FAM;
	S_PORT(ipn) = 0;
#ifdef IPV6
	copy_s_addr(S_ADDR(ipn), IN_ADDR(ServerInfo.ip));
#else
	copy_s_addr(S_ADDR(ipn), IN_ADDR(ServerInfo.ip));
#endif
	comm_connect_tcp(client_p->fd, aconf->host, aconf->port,
			 (struct sockaddr *)&SOCKADDR(ipn), sizeof(struct irc_sockaddr), 
			 serv_connect_callback, client_p, aconf->aftype);
      }
    else
      {
	comm_connect_tcp(client_p->fd, aconf->host, aconf->port, NULL, 0, 
			 serv_connect_callback, client_p, aconf->aftype);
      }

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
    struct Client *client_p = data;
    struct ConfItem *aconf;

    /* First, make sure its a real client! */
    assert(client_p != NULL);
    assert(client_p->fd == fd);

    /* Next, for backward purposes, record the ip of the server */
    copy_s_addr(IN_ADDR(client_p->localClient->ip), S_ADDR(fd_table[fd].connect.hostaddr));
    /* Check the status */
    if (status != COMM_OK)
      {
        /* We have an error, so report it and quit */
	/* Admins get to see any IP, mere opers don't *sigh*
	 */
        sendto_realops_flags(FLAGS_ADMIN,
			     "Error connecting to %s[%s]: %s", client_p->name,
			     client_p->host, comm_errstr(status));
	sendto_realops_flags(FLAGS_NOTADMIN,
			     "Error connecting to %s: %s",
			     client_p->name, comm_errstr(status));
	client_p->flags |= FLAGS_DEADSOCKET;
        exit_client(client_p, client_p, &me, comm_errstr(status));
        return;
      }

    /* COMM_OK, so continue the connection procedure */
    /* Get the C/N lines */
    aconf = find_conf_name(&client_p->localClient->confs,
			    client_p->name, CONF_SERVER); 
    if (!aconf)
      { 
        sendto_realops_flags(FLAGS_ALL,
		     "Lost C-Line for %s", get_client_name(client_p, HIDE_IP));
        exit_client(client_p, client_p, &me, "Lost C-line");
        return;
      }
    /* Next, send the initial handshake */
    SetHandshake(client_p);

#ifdef HAVE_LIBCRYPTO
    /* Handle all CRYPTLINK links in cryptlink_init */
    if (IsConfCryptLink(aconf))
    {
      cryptlink_init(client_p, aconf, fd);
      return;
    }
#endif
    
    /*
     * jdc -- Check and send spasswd, not passwd.
     */
    if (!EmptyString(aconf->spasswd))
    {
        sendto_one(client_p, "PASS %s :TS", aconf->spasswd);
    }
    
    /*
     * Pass my info to the new server
     *
     * If trying to negotiate LazyLinks, pass on CAP_LL
     * If this is a HUB, pass on CAP_HUB
     */

    send_capabilities(client_p,CAP_MASK
             | ((aconf->flags & CONF_FLAGS_LAZY_LINK) ? CAP_LL : 0)
             | ((aconf->flags & CONF_FLAGS_NOCOMPRESSED) ? 0:CAP_ZIP_SUPPORTED)
             | (ServerInfo.hub ? CAP_HUB : 0),
             0);

    sendto_one(client_p, "SERVER %s 1 :%s",
      my_name_for_link(me.name, aconf), me.info);

    /* 
     * If we've been marked dead because a send failed, just exit
     * here now and save everyone the trouble of us ever existing.
     */
    if (IsDead(client_p)) {
        sendto_realops_flags(FLAGS_ADMIN,
			     "%s[%s] went dead during handshake",
                             client_p->name,
			     client_p->host);
        sendto_realops_flags(FLAGS_OPER,
			     "%s went dead during handshake", client_p->name);
        exit_client(client_p, client_p, &me, "Went dead during handshake");
        return;
    }

    /* don't move to serv_list yet -- we haven't sent a burst! */

    /* If we get here, we're ok, so lets start reading some data */
    comm_setselect(fd, FDLIST_SERVER, COMM_SELECT_READ, read_packet,
                   client_p, 0);
}

#ifdef HAVE_LIBCRYPTO
/*
 * sends a CRYPTSERV command.
 */
void cryptlink_init(struct Client *client_p,
                    struct ConfItem *aconf,
                    int fd)
{
  char *encrypted;
  char *key_to_send;
  char randkey[CIPHERKEYLEN];
  int enc_len;

  /* get key */
  if (!ServerInfo.rsa_private_key ||
      !RSA_check_key(ServerInfo.rsa_private_key) ||
      !aconf->rsa_public_key)
    {
      cryptlink_error(client_p,
                      "%s[%s]: CRYPTLINK failed - invalid RSA key(s)");
      return;
    }

  if (RAND_bytes(randkey, CIPHERKEYLEN) != 1)
    {
      cryptlink_error(client_p,
                      "%s[%s]: CRYPTLINK failed - couldn't generate secret");
      return;
    }

  encrypted = MyMalloc(RSA_size(ServerInfo.rsa_private_key));

  enc_len = RSA_public_encrypt(CIPHERKEYLEN,
                               randkey,
                               encrypted,
                               aconf->rsa_public_key,
                               RSA_PKCS1_PADDING);

  memcpy(client_p->localClient->in_key, randkey, CIPHERKEYLEN);

  if (enc_len <= 0)
    {
      MyFree(encrypted);
      cryptlink_error(client_p,
                      "%s[%s]: CRYPTLINK failed - couldn't encrypt data");
      return;
    }

  if (!(base64_block(&key_to_send, encrypted, enc_len)))
    {
      MyFree(encrypted);
      cryptlink_error(client_p,
                      "%s[%s]: CRYPTLINK failed - couldn't base64 key");
      return;
    }


  send_capabilities(client_p,CAP_MASK
         | ((aconf->flags & CONF_FLAGS_LAZY_LINK) ? CAP_LL : 0)
         | ((aconf->flags & CONF_FLAGS_NOCOMPRESSED) ? 0:CAP_ZIP_SUPPORTED)
         | (ServerInfo.hub ? CAP_HUB : 0),
         CAP_ENC_MASK);

  sendto_one(client_p, "CRYPTSERV %s %s :%s",
             my_name_for_link(me.name, aconf), key_to_send, me.info);

  SetHandshake(client_p);
  SetWaitAuth(client_p);

  MyFree(encrypted);
  MyFree(key_to_send);

  /*
   * If we've been marked dead because a send failed, just exit
   * here now and save everyone the trouble of us ever existing.
   */
  if (IsDead(client_p))
   {
     cryptlink_error(client_p,
                     "%s[%s] went dead during handshake");
     return;
   }

  /* If we get here, we're ok, so lets start reading some data */
  if (fd > -1)
    comm_setselect(fd, FDLIST_SERVER, COMM_SELECT_READ, read_packet,
                   client_p, 0);
}

void cryptlink_error(struct Client *client_p, char *reason)
{
    sendto_realops_flags(FLAGS_ADMIN,
                         reason,
                         client_p->name,
                         client_p->host);
    sendto_realops_flags(FLAGS_OPER,
                         reason,
                         client_p->name,
                         "user@255.255.255.255");
    exit_client(client_p, client_p, &me, "Unable to use CRYPTLINK auth!");
}

#endif /* HAVE_LIBCRYPTO */
