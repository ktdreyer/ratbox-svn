/************************************************************************
 *   IRC - Internet Relay Chat, src/client.c
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
 *  $Id$
 */
#include "tools.h"
#include "client.h"
#include "class.h"
#include "channel.h"
#include "common.h"
#include "event.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "s_gline.h"
#include "numeric.h"
#include "packet.h"
#include "s_auth.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "s_serv.h"
#include "send.h"
#include "whowas.h"
#include "s_debug.h"
#include "s_user.h"
#include "linebuf.h"
#include "hash.h"
#include "memory.h"
#include "hostmask.h"
#include "balloc.h"

#include <assert.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static void check_pings_list(dlink_list *list);
static void check_unknowns_list(dlink_list *list);
static void free_exited_clients(void *unused);

static EVH check_pings;

static int remote_client_count=0;
static int local_client_count=0;

static BlockHeap *client_heap = NULL;
static BlockHeap *lclient_heap = NULL;

/*
 * client_heap_gc
 *
 * inputs	- NONE
 * output	- NONE
 * side effect  - Does garbage collection of client heaps
 */
 
static void client_heap_gc(void *unused)
{
  BlockHeapGarbageCollect(client_heap);
  BlockHeapGarbageCollect(lclient_heap);
  eventAdd("client_heap_gc", client_heap_gc, NULL, 30, 0);
}

/*
 * init_client
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	- initialize client free memory
 */
void init_client(void)
{
  remote_client_count = 0;
  local_client_count = 0;
  /*
   * start off the check ping event ..  -- adrian
   * Every 30 seconds is plenty -- db
   */
  client_heap = BlockHeapCreate(sizeof(struct Client), 10000);
  lclient_heap = BlockHeapCreate(sizeof(struct LocalUser), 512); 
  eventAdd("check_pings", check_pings, NULL, 30, 0);
  eventAdd("free_exited_clients()", &free_exited_clients, NULL, 4, 0);
  eventAdd("client_heap_gc", client_heap_gc, NULL, 30, 0);
}

/*
 * make_client - create a new Client struct and set it to initial state.
 *
 *      from == NULL,   create local client (a client connected
 *                      to a socket).
 *
 *      from,   create remote client (behind a socket
 *                      associated with the client defined by
 *                      'from'). ('from' is a local client!!).
 */
struct Client* make_client(struct Client* from)
{
  struct Client* client_p = NULL;
  struct LocalUser *localClient;
  dlink_node *m;

  client_p = BlockHeapAlloc(client_heap);
  memset(client_p, 0, sizeof(struct Client)); 
  if (from == NULL)
    {
      client_p->from  = client_p; /* 'from' of local client is self! */
      client_p->since = client_p->lasttime = client_p->firsttime = CurrentTime;

      localClient = (struct LocalUser *)BlockHeapAlloc(lclient_heap);
      memset(localClient, 0, sizeof(struct LocalUser));

      client_p->localClient = localClient;
      client_p->localClient->ctrlfd = -1;
#ifndef HAVE_SOCKETPAIR
      client_p->localClient->ctrlfd_r = -1;
#endif      
      /* as good a place as any... */
      m = make_dlink_node();
      dlinkAdd(client_p, m, &unknown_list);
      ++local_client_count;
    }
  else
    { /* from is not NULL */
      client_p->localClient = NULL;
      client_p->from = from; /* 'from' of local client is self! */
      ++remote_client_count;
    }

  client_p->status = STAT_UNKNOWN;
  client_p->fd = -1;
#ifndef HAVE_SOCKETPAIR
  client_p->fd_r = -1;
#endif
  strcpy(client_p->username, "unknown");
#if 0
  client_p->name[0] = '\0';
  client_p->flags   = 0;
  client_p->next    = NULL;
  client_p->prev    = NULL;
  client_p->hnext   = NULL;
  client_p->lnext   = NULL;
  client_p->lprev   = NULL;
  client_p->user    = NULL;
  client_p->serv    = NULL;
  client_p->servptr = NULL;
  client_p->whowas  = NULL;
  client_p->allow_list.head = NULL;
  client_p->allow_list.tail = NULL;
  client_p->on_allow_list.head = NULL;
  client_p->on_allow_list.tail = NULL;
#endif
  return client_p;
}

void _free_client(struct Client* client_p)
{
  assert(0 != client_p);
  assert(&me != client_p);
  assert(0 == client_p->prev);
  assert(0 == client_p->next);

  /* If localClient is non NULL, its a local client */
  if (client_p->localClient != NULL)
    {
      if (-1 < client_p->fd)
	fd_close(client_p->fd);

#ifndef NDEBUG
      mem_frob(client_p->localClient, sizeof(struct LocalUser));
#endif

      BlockHeapFree(lclient_heap, client_p->localClient);
      --local_client_count;
      assert(local_client_count >= 0);
    }
  else
    {
      --remote_client_count;
    }

#ifndef NDEBUG
  mem_frob(client_p, sizeof(struct Client));
#endif
  BlockHeapFree(client_heap, client_p);
}

/*
 * check_pings - go through the local client list and check activity
 * kill off stuff that should die
 *
 * inputs       - NOT USED (from event)
 * output       - next time_t when check_pings() should be called again
 * side effects - 
 *
 *
 * A PING can be sent to clients as necessary.
 *
 * Client/Server ping outs are handled.
 */

/*
 * Addon from adrian. We used to call this after nextping seconds,
 * however I've changed it to run once a second. This is only for
 * PING timeouts, not K/etc-line checks (thanks dianora!). Having it
 * run once a second makes life a lot easier - when a new client connects
 * and they need a ping in 4 seconds, if nextping was set to 20 seconds
 * we end up waiting 20 seconds. This is stupid. :-)
 * I will optimise (hah!) check_pings() once I've finished working on
 * tidying up other network IO evilnesses.
 *     -- adrian
 */

static void
check_pings(void *notused)
{               
  check_pings_list(&lclient_list);
  check_pings_list(&serv_list);
  check_unknowns_list(&unknown_list);

  /* Reschedule a new address */
  eventAdd("check_pings", check_pings, NULL, 30, 0);
}

/*
 * check_pings_list()
 *
 * inputs	- pointer to list to check
 * output	- NONE
 * side effects	- 
 */
static void
check_pings_list(dlink_list *list)
{
  char         scratch[32];	/* way too generous but... */
  struct Client *client_p;          /* current local client_p being examined */
  int           ping = 0;       /* ping time value from client */
#if 0
  time_t        timeout;        /* found necessary ping time */
#endif
  dlink_node    *ptr, *next_ptr;

  for (ptr = list->head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;
      client_p = ptr->data;

      /*
      ** Note: No need to notify opers here. It's
      ** already done when "FLAGS_DEADSOCKET" is set.
      */
      if (client_p->flags & FLAGS_DEADSOCKET)
        {
         if (client_p->flags & FLAGS_SENDQEX)
           {
            exit_client(client_p, client_p, &me, "SendQ exceeded");
            continue;
           }
          exit_client(client_p, client_p, &me, "Dead socket");
          continue; 
        }
      if (IsPerson(client_p))
        {
          if( !IsExemptKline(client_p) &&
              GlobalSetOptions.idletime && 
              !IsOper(client_p) &&
              !IsIdlelined(client_p) && 
              ((CurrentTime - client_p->user->last) > GlobalSetOptions.idletime))
            {
              struct ConfItem *aconf;

              aconf = make_conf();
              aconf->status = CONF_KILL;

              DupString(aconf->host, client_p->host);
              DupString(aconf->passwd, "idle exceeder" );
              DupString(aconf->name, client_p->username);
              aconf->port = 0;
              aconf->hold = CurrentTime + 60;
              add_temp_kline(aconf);
              sendto_realops_flags(FLAGS_ALL, L_ALL,
			   "Idle time limit exceeded for %s - temp k-lining",
				   get_client_name(client_p, HIDE_IP));


	      (void)exit_client(client_p, client_p, &me, aconf->passwd);
              continue;
            }
        }

      if (!IsRegistered(client_p))
        ping = CONNECTTIMEOUT;
      else
        ping = get_client_ping(client_p);

      if (ping < (CurrentTime - client_p->lasttime))
        {
          /*
           * If the client/server hasnt talked to us in 2*ping seconds
           * and it has a ping time, then close its connection.
           */
          if (((CurrentTime - client_p->lasttime) >= (2 * ping) &&
               (client_p->flags & FLAGS_PINGSENT)))
            {
              if (IsServer(client_p) || IsConnecting(client_p) ||
                  IsHandshake(client_p))
                {
                  sendto_realops_flags(FLAGS_ALL, L_ADMIN,
				       "No response from %s, closing link",
				       get_client_name(client_p, HIDE_IP));
                  sendto_realops_flags(FLAGS_ALL, L_OPER,
                                       "No response from %s, closing link",
                                       get_client_name(client_p, MASK_IP));
                  ilog(L_NOTICE, "No response from %s, closing link",
                      get_client_name(client_p, HIDE_IP));
                }
	      (void)ircsprintf(scratch,
			       "Ping timeout: %d seconds",
			       (int)(CurrentTime - client_p->lasttime));
	      
	      (void)exit_client(client_p, client_p, &me, scratch);
              continue;
            }
          else if ((client_p->flags & FLAGS_PINGSENT) == 0)
            {
              /*
               * if we havent PINGed the connection and we havent
               * heard from it in a while, PING it to make sure
               * it is still alive.
               */
              client_p->flags |= FLAGS_PINGSENT;
              /* not nice but does the job */
              client_p->lasttime = CurrentTime - ping;
              sendto_one(client_p, "PING :%s", me.name);
            }
        }
      /* ping_timeout: */

      /* bloat for now */
#if 0
      timeout = client_p->lasttime + ping;
      while (timeout <= CurrentTime)
        timeout += ping;
#endif
    }
}

/*
 * check_unknowns_list
 *
 * inputs	- pointer to list of unknown clients
 * output	- NONE
 * side effects	- unknown clients get marked for termination after n seconds
 */
static void
check_unknowns_list(dlink_list *list)
{
  dlink_node *ptr, *next_ptr;
  struct Client *client_p;

  for(ptr = list->head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;
      client_p = ptr->data;

      /*
       * Check UNKNOWN connections - if they have been in this state
       * for > 30s, close them.
       */

      if (client_p->firsttime ? ((CurrentTime - client_p->firsttime) > 30) : 0)
	{
	  (void)exit_client(client_p, client_p, &me, "Connection timed out");
	}
    }
}

/*
 * check_klines
 * inputs	- NONE
 * output	- NONE
 * side effects - Check all connections for a pending kline against the
 * 		  client, exit the client if a kline matches.
 */
void 
check_klines(void)
{               
  struct Client *client_p;          /* current local client_p being examined */
  struct ConfItem     *aconf = (struct ConfItem *)NULL;
  char          *reason;                /* pointer to reason string */
  dlink_node    *ptr, *next_ptr;
  
  for (ptr = lclient_list.head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;
      client_p = ptr->data;
      if (IsMe(client_p))
	continue;
      /* if there is a returned struct ConfItem then kill it */
      if ((aconf = find_dline(&client_p->localClient->ip,
			      client_p->localClient->aftype)))
	{
	  if (aconf->status & CONF_EXEMPTDLINE)
	    continue;
	  sendto_realops_flags(FLAGS_ALL, L_ALL,"DLINE active for %s",
			       get_client_name(client_p, HIDE_IP));
	  if (ConfigFileEntry.kline_with_connection_closed)
	    reason = "Connection closed";
	  else
	    {
	      if (ConfigFileEntry.kline_with_reason && aconf->passwd)
		reason = aconf->passwd;
	      else
		reason = "D-lined";
	    }
	  if (IsPerson(client_p)) 
	    sendto_one(client_p, form_str(ERR_YOUREBANNEDCREEP),
		       me.name, client_p->name, reason);
	  else
	    sendto_one(client_p, "NOTICE DLINE :*** You have been D-lined");
	  (void)exit_client(client_p, client_p, &me, reason );
	  continue; /* and go examine next fd/client_p */
	}

      if (IsPerson(client_p))
	{
	  if (ConfigFileEntry.glines &&
	      (aconf = find_gkill(client_p, client_p->username)))
	    {
	      if (IsExemptKline(client_p))
		{
		  sendto_realops_flags(FLAGS_ALL, L_ALL,
				       "GLINE over-ruled for %s, client is kline_exempt",
				       get_client_name(client_p, HIDE_IP));
		  continue;
		}
	      
	      if (IsExemptGline(client_p))
		{
		  sendto_realops_flags(FLAGS_ALL, L_ALL,
				       "GLINE over-ruled for %s, client is gline_exempt",
				       get_client_name(client_p, HIDE_IP));
		  continue;
		}
       
	      sendto_realops_flags(FLAGS_ALL, L_ALL, "GLINE active for %s",
				   get_client_name(client_p, HIDE_IP));
			    
	      if (ConfigFileEntry.kline_with_connection_closed)
		{
		  /* We use a generic non-descript message here on 
		   * purpose so as to prevent other users seeing the
		   * client disconnect from harassing the IRCops
		   */
		  reason = "Connection closed";
		} 
	      else 
		{
		  if (ConfigFileEntry.kline_with_reason && aconf->passwd)
		    reason = aconf->passwd;
		  else
		    reason = "G-lined";
		}
	
	      sendto_one(client_p, form_str(ERR_YOUREBANNEDCREEP), me.name,
			 client_p->name, reason);
	      (void)exit_client(client_p, client_p, &me, reason);
	      /* and go examine next fd/client_p */    
	      continue;
	    } 
	  else if((aconf = find_kill(client_p))) 
	    {
	      /* if there is a returned struct ConfItem.. then kill it */
	      if (IsExemptKline(client_p))
		{
		  sendto_realops_flags(FLAGS_ALL, L_ALL,
				       "KLINE over-ruled for %s, client is kline_exempt",
				       get_client_name(client_p, HIDE_IP));
		  continue;
		}

	      sendto_realops_flags(FLAGS_ALL, L_ALL, "KLINE active for %s",
				   get_client_name(client_p, HIDE_IP));
	      if (ConfigFileEntry.kline_with_connection_closed)
		reason = "Connection closed";
	      else
		{
		  if (ConfigFileEntry.kline_with_reason && aconf->passwd)
		    reason = aconf->passwd;
		  else
		    reason = "K-lined";
		}
	
	      sendto_one(client_p, form_str(ERR_YOUREBANNEDCREEP), me.name,
			 client_p->name, reason);
	      (void)exit_client(client_p, client_p, &me, reason);
	      continue; 
	    }
	}
    }
}

/*
 * update_client_exit_stats
 *
 * input	- pointer to client
 * output	- NONE
 * side effects	- 
 */
static void update_client_exit_stats(struct Client* client_p)
{
  if (IsServer(client_p))
    {
      --Count.server;
      sendto_realops_flags(FLAGS_EXTERNAL, L_ALL, 
                           "Server %s split from %s",
                           client_p->name, client_p->servptr->name);
    }
  else if (IsClient(client_p))
    {
      --Count.total;
      if (IsOper(client_p))
	--Count.oper;
      if (IsInvisible(client_p)) 
	--Count.invisi;
    }
}

/*
 * release_client_state
 *
 * input	- pointer to client to release
 * output	- NONE
 * side effects	- 
 */
static void
release_client_state(struct Client* client_p)
{
  if (client_p->user)
    {
      if (IsPerson(client_p))
	{
	  add_history(client_p,0);
	  off_history(client_p);
	}
      free_user(client_p->user, client_p); /* try this here */
    }
  if (client_p->serv)
    {
      if (client_p->serv->user)
        free_user(client_p->serv->user, client_p);
      MyFree((char*) client_p->serv);
    }
}

/*
 * remove_client_from_list
 * inputs	- point to client to remove
 * output	- NONE
 * side effects - taken the code from ExitOneClient() for this
 *		  and placed it here. - avalon
 */
void
remove_client_from_list(struct Client* client_p)
{
  assert(0 != client_p);
  
  /* A client made with make_client()
   * is on the unknown_list until removed.
   * If it =does= happen to exit before its removed from that list
   * and its =not= on the GlobalClientList, it will core here.
   * short circuit that case now -db
   */
  if (!client_p->prev && !client_p->next)
    {
      return;
    }

  if (client_p->prev)
    client_p->prev->next = client_p->next;
  else
    {
      GlobalClientList = client_p->next;
      GlobalClientList->prev = NULL;
    }

  if (client_p->next)
    client_p->next->prev = client_p->prev;
  client_p->next = client_p->prev = NULL;

  update_client_exit_stats(client_p);
}

/*
 * add_client_to_list
 * input	- pointer to client
 * output	- NONE
 * side effects	- although only a small routine,
 *		  it appears in a number of places
 * 		  as a collection of a few lines...functions like this
 *		  should be in this file, shouldnt they ?  after all,
 *		  this is list.c, isnt it ? (no
 *		  -avalon
 */
void
add_client_to_list(struct Client *client_p)
{
  /*
   * since we always insert new clients to the top of the list,
   * this should mean the "me" is the bottom most item in the list.
   */
  client_p->next = GlobalClientList;
  GlobalClientList = client_p;
  if (client_p->next)
    client_p->next->prev = client_p;
  return;
}

/* Functions taken from +CSr31, paranoified to check that the client
** isn't on a llist already when adding, and is there when removing -orabidoo
*/
void add_client_to_llist(struct Client **bucket, struct Client *client)
{
  if (!client->lprev && !client->lnext)
    {
      client->lprev = NULL;
      if ((client->lnext = *bucket) != NULL)
        client->lnext->lprev = client;
      *bucket = client;
    }
}

void
del_client_from_llist(struct Client **bucket, struct Client *client)
{
  if (client->lprev)
    {
      client->lprev->lnext = client->lnext;
    }
  else if (*bucket == client)
    {
      *bucket = client->lnext;
    }
  if (client->lnext)
    {
      client->lnext->lprev = client->lprev;
    }
  client->lnext = client->lprev = NULL;
}

/*
 *  find_userhost - find a user@host (server or user).
 *
 *  *Note*
 *      Semantics of this function has been changed from
 *      the old. 'name' is now assumed to be a null terminated
 *      string and the search is the for server and user.
 */
struct Client *find_userhost(char *user, char *host,
			     struct Client *client_p, int *count)
{
  struct Client       *c2ptr;
  struct Client       *res = client_p;

  *count = 0;
  if (collapse(user))
    for (c2ptr = GlobalClientList; c2ptr; c2ptr = c2ptr->next) 
      {
        if (!MyClient(c2ptr)) /* implies mine and a user */
          continue;
        if ((!host || match(host, c2ptr->host)) &&
            irccmp(user, c2ptr->username) == 0)
          {
            (*count)++;
            res = c2ptr;
          }
      }
  return res;
}

/*
 *  find_server - find server by name.
 *
 *      This implementation assumes that server and user names
 *      are unique, no user can have a server name and vice versa.
 *      One should maintain separate lists for users and servers,
 *      if this restriction is removed.
 *
 *  *Note*
 *      Semantics of this function has been changed from
 *      the old. 'name' is now assumed to be a null terminated
 *      string.
 */
struct Client* find_server(const char* name)
{
  if (name)
    return hash_find_server(name);
  return 0;
}

/*
 * next_client - find the next matching client. 
 * The search can be continued from the specified client entry. 
 * Normal usage loop is:
 *
 *      for (x = client; x = next_client(x,mask); x = x->next)
 *              HandleMatchingClient;
 *            
 */
struct Client*
next_client(struct Client *next,     /* First client to check */
            const char* ch)          /* search string (may include wilds) */
{
  struct Client *tmp = next;

  next = find_client(ch, tmp);
  if (tmp && tmp->prev == next)
    return ((struct Client *) NULL);

  if (next != tmp)
    return next;
  for ( ; next; next = next->next)
    {
      if (match(ch,next->name)) break;
    }
  return next;
}


/* 
 * this slow version needs to be used for hostmasks *sigh
 *
 * next_client_double - find the next matching client. 
 * The search can be continued from the specified client entry. 
 * Normal usage loop is:
 *
 *      for (x = client; x = next_client(x,mask); x = x->next)
 *              HandleMatchingClient;
 *            
 */
struct Client* 
next_client_double(struct Client *next, /* First client to check */
                   const char* ch)      /* search string (may include wilds) */
{
  struct Client *tmp = next;

  next = find_client(ch, tmp);
  if (tmp && tmp->prev == next)
    return NULL;
  if (next != tmp)
    return next;
  for ( ; next; next = next->next)
    {
      if (match(ch,next->name) || match(next->name,ch))
        break;
    }
  return next;
}

/*
 * find_person - find person by (nick)name.
 * inputs	- pointer to name
 *		- pointer to client
 * output	- return client pointer
 * side effects -
 */
struct Client *find_person(char *name, struct Client *client_p)
{
  struct Client       *c2ptr = client_p;

  c2ptr = find_client(name, c2ptr);

  if (c2ptr && IsClient(c2ptr) && c2ptr->user)
    return c2ptr;
  return client_p;
}

/*
 * find_chasing - find the client structure for a nick name (user) 
 *      using history mechanism if necessary. If the client is not found, 
 *      an error message (NO SUCH NICK) is generated. If the client was found
 *      through the history, chasing will be 1 and otherwise 0.
 */
struct Client *find_chasing(struct Client *source_p, char *user, int *chasing)
{
  struct Client *who = find_client(user, (struct Client *)NULL);
  
  if (chasing)
    *chasing = 0;
  if (who)
    return who;
  if (!(who = get_history(user, (long)KILLCHASETIMELIMIT)))
    {
      sendto_one(source_p, form_str(ERR_NOSUCHNICK),
                 me.name, source_p->name, user);
      return ((struct Client *)NULL);
    }
  if (chasing)
    *chasing = 1;
  return who;
}



/*
 * check_registered_user - is used to cancel message, if the
 * originator is a server or not registered yet. In other
 * words, passing this test, *MUST* guarantee that the
 * source_p->user exists (not checked after this--let there
 * be coredumps to catch bugs... this is intentional --msa ;)
 *
 * There is this nagging feeling... should this NOT_REGISTERED
 * error really be sent to remote users? This happening means
 * that remote servers have this user registered, although this
 * one has it not... Not really users fault... Perhaps this
 * error message should be restricted to local clients and some
 * other thing generated for remotes...
 */
int check_registered_user(struct Client* client)
{
  if (!IsRegisteredUser(client))
    {
      sendto_one(client, form_str(ERR_NOTREGISTERED), me.name, "*");
      return -1;
    }
  return 0;
}

/*
 * check_registered user cancels message, if 'x' is not
 * registered (e.g. we don't know yet whether a server
 * or user)
 */
int check_registered(struct Client* client)
{
  if (!IsRegistered(client))
    {
      sendto_one(client, form_str(ERR_NOTREGISTERED), me.name, "*");
      return -1;
    }
  return 0;
}

/*
 * get_client_name -  Return the name of the client
 *    for various tracking and
 *      admin purposes. The main purpose of this function is to
 *      return the "socket host" name of the client, if that
 *        differs from the advertised name (other than case).
 *        But, this can be used to any client structure.
 *
 * NOTE 1:
 *        Watch out the allocation of "nbuf", if either source_p->name
 *        or source_p->sockhost gets changed into pointers instead of
 *        directly allocated within the structure...
 *
 * NOTE 2:
 *        Function return either a pointer to the structure (source_p) or
 *        to internal buffer (nbuf). *NEVER* use the returned pointer
 *        to modify what it points!!!
 */

const char* get_client_name(struct Client* client, int showip)
{
  static char nbuf[HOSTLEN * 2 + USERLEN + 5];

  assert(0 != client);

  if (MyConnect(client))
    {
      if (!irccmp(client->name, client->host))
        return client->name;

      /* And finally, let's get the host information, ip or name */
      switch (showip)
        {
          case SHOW_IP:
            ircsprintf(nbuf, "%s[%s@%s]", client->name, client->username,
              client->localClient->sockhost);
            break;
          case MASK_IP:
            ircsprintf(nbuf, "%s[%s@255.255.255.255]", client->name,
              client->username);
            break;
          default:
            ircsprintf(nbuf, "%s[%s@%s]", client->name, client->username,
              client->host);
        }
      return nbuf;
    }

  /* As pointed out by Adel Mezibra 
   * Neph|l|m@EFnet. Was missing a return here.
   */
  return client->name;
}

/*
 * get_client_host
 *
 * inputs	- pointer to client struct
 * output	- pointer to static char string with client hostname
 * side effects	-
 */
const char* get_client_host(struct Client* client)
{
  assert(0 != client);
  return get_client_name(client, HIDE_IP);
#if 0
  static char nbuf[HOSTLEN * 2 + USERLEN + 5];
  
  assert(0 != client);

  if (!MyConnect(client))
    return client->name;
  if (client->localClient->dns_query->answer.status != adns_s_ok)
    return get_client_name(client, HIDE_IP);
  else
    {
      ircsprintf(nbuf, "%s[%-.*s@%-.*s]",
                 client->name, USERLEN, client->username,
                 HOSTLEN, client->host);
    }
  return nbuf;
#endif
}

static void
free_exited_clients(void *unused)
{
 dlink_node *ptr, *next;
 struct Client *target_p;
  
 for(ptr = dead_list.head; ptr; ptr = next)
 {
  target_p = ptr->data;
  next = ptr->next;
  if (ptr->data == NULL)
  {
   sendto_realops_flags(FLAGS_ALL, L_ALL,
                        "Warning: null client on dead_list!");
   dlinkDelete(ptr, &dead_list);
   free_dlink_node(ptr);
   continue;
  }
  release_client_state(target_p);
  free_client(target_p);
  dlinkDelete(ptr, &dead_list);
  free_dlink_node(ptr);
 }
 eventAdd("free_exited_clients()", &free_exited_clients, NULL, 4, 0);
}

/*
** Exit one client, local or remote. Assuming all dependents have
** been already removed, and socket closed for local client.
*/
static void exit_one_client(struct Client *client_p, struct 
			    Client *source_p, struct Client *from,
                            const char* comment)
{
  struct Client* target_p;
  dlink_node *lp;
  dlink_node *next_lp;

  if (IsServer(source_p))
    {
      if (source_p->servptr && source_p->servptr->serv)
        del_client_from_llist(&(source_p->servptr->serv->servers),
                                    source_p);
      else
        ts_warn("server %s without servptr!", source_p->name);

      if(!IsMe(source_p))
        remove_server_from_list(source_p);
    }
  else if (source_p->servptr && source_p->servptr->serv)
      del_client_from_llist(&(source_p->servptr->serv->users), source_p);
  /* there are clients w/o a servptr: unregistered ones */

  /*
  **  For a server or user quitting, propogate the information to
  **  other servers (except to the one where is came from (client_p))
  */
  if (IsMe(source_p))
    {
      sendto_realops_flags(FLAGS_ALL, L_ALL,
			   "ERROR: tried to exit me! : %s", comment);
      return;        /* ...must *never* exit self!! */
    }
  else if (IsServer(source_p))
    {
      /*
      ** Old sendto_serv_but_one() call removed because we now
      ** need to send different names to different servers
      ** (domain name matching)
      */
      /*
      ** The bulk of this is done in remove_dependents now, all
      ** we have left to do is send the SQUIT upstream.  -orabidoo
      */
      if (source_p->localClient && (source_p->localClient->ctrlfd > -1))
        fd_close(source_p->localClient->ctrlfd);

      target_p = source_p->from;
      if (target_p && IsServer(target_p) && target_p != client_p && !IsMe(target_p) &&
          (source_p->flags & FLAGS_KILLED) == 0)
        sendto_one(target_p, ":%s SQUIT %s :%s", from->name, source_p->name, comment);
    }
  else if (!(IsPerson(source_p)))
      /* ...this test is *dubious*, would need
      ** some thought.. but for now it plugs a
      ** nasty hole in the server... --msa
      */
      ; /* Nothing */
  else if (source_p->name[0]) /* ...just clean all others with QUIT... */
    {
      /*
      ** If this exit is generated from "m_kill", then there
      ** is no sense in sending the QUIT--KILL's have been
      ** sent instead.
      */
      if ((source_p->flags & FLAGS_KILLED) == 0)
        {
          sendto_server(client_p, source_p, NULL, NOCAPS, NOCAPS,
                        NOFLAGS, ":%s QUIT :%s", source_p->name, comment);
        }
      /*
      ** If a person is on a channel, send a QUIT notice
      ** to every client (person) on the same channel (so
      ** that the client can show the "**signoff" message).
      ** (Note: The notice is to the local clients *only*)
      */
      if (source_p->user)
        {
          sendto_common_channels_local(source_p, ":%s!%s@%s QUIT :%s",
				       source_p->name,
				       source_p->username,
				       source_p->host,
				       comment);

          for (lp = source_p->user->channel.head; lp; lp = next_lp)
	    {
	      next_lp = lp->next;
	      remove_user_from_channel(lp->data,source_p, 1);
	    }
          
          /* Clean up invitefield */
          for (lp = source_p->user->invited.head; lp; lp = next_lp)
           {
              next_lp = lp->next;
              del_invite(lp->data, source_p);
           }

          /* Clean up allow lists */
          del_all_accepts(source_p);

	  if (HasID(source_p))
	    del_from_id_hash_table(source_p->user->id, source_p);
  
          /* again, this is all that is needed */
        }
    }
  
  /* 
   * Remove source_p from the client lists
   */
  del_from_client_hash_table(source_p->name, source_p);

  /* remove from global client list */
  remove_client_from_list(source_p);

  SetDead(source_p);
  /* add to dead client dlist */
  lp = make_dlink_node();
  dlinkAdd(source_p, lp, &dead_list);
}

/*
** Recursively send QUITs and SQUITs for source_p and all its dependent clients
** and servers to those servers that need them.  A server needs the client
** QUITs if it can't figure them out from the SQUIT (ie pre-TS4) or if it
** isn't getting the SQUIT because of @#(*&@)# hostmasking.  With TS4, once
** a link gets a SQUIT, it doesn't need any QUIT/SQUITs for clients depending
** on that one -orabidoo
*/
static void recurse_send_quits(struct Client *client_p, struct Client *source_p, struct Client *to,
                                const char* comment,  /* for servers */
                                const char* myname)
{
  struct Client *target_p;

  /* If this server can handle quit storm (QS) removal
   * of dependents, just send the SQUIT
   */

  if (IsCapable(to,CAP_QS))
    {
      if (match(myname, source_p->name))
        {
          for (target_p = source_p->serv->users; target_p; target_p = target_p->lnext)
            sendto_one(to, ":%s QUIT :%s", target_p->name, comment);
          for (target_p = source_p->serv->servers; target_p; target_p = target_p->lnext)
            recurse_send_quits(client_p, target_p, to, comment, myname);
        }
      else
        sendto_one(to, "SQUIT %s :%s", source_p->name, me.name);
    }
  else
    {
      for (target_p = source_p->serv->users; target_p; target_p = target_p->lnext)
        sendto_one(to, ":%s QUIT :%s", target_p->name, comment);
      for (target_p = source_p->serv->servers; target_p; target_p = target_p->lnext)
        recurse_send_quits(client_p, target_p, to, comment, myname);
      if (!match(myname, source_p->name))
        sendto_one(to, "SQUIT %s :%s", source_p->name, me.name);
    }
}

/* 
** Remove all clients that depend on source_p; assumes all (S)QUITs have
** already been sent.  we make sure to exit a server's dependent clients 
** and servers before the server itself; exit_one_client takes care of 
** actually removing things off llists.   tweaked from +CSr31  -orabidoo
*/
/*
 * added sanity test code.... source_p->serv might be NULL...
 */
static void recurse_remove_clients(struct Client* source_p, const char* comment)
{
  struct Client *target_p;

  if (IsMe(source_p))
    return;

  if (!source_p->serv)        /* oooops. uh this is actually a major bug */
    return;

  while ( (target_p = source_p->serv->servers) )
    {
      recurse_remove_clients(target_p, comment);
      /*
      ** a server marked as "KILLED" won't send a SQUIT 
      ** in exit_one_client()   -orabidoo
      */
      target_p->flags |= FLAGS_KILLED;
      exit_one_client(NULL, target_p, &me, me.name);
    }

  while ( (target_p = source_p->serv->users) )
    {
      target_p->flags |= FLAGS_KILLED;
      exit_one_client(NULL, target_p, &me, comment);
    }
}

/*
** Remove *everything* that depends on source_p, from all lists, and sending
** all necessary QUITs and SQUITs.  source_p itself is still on the lists,
** and its SQUITs have been sent except for the upstream one  -orabidoo
*/
static void remove_dependents(struct Client* client_p, 
                               struct Client* source_p,
                               struct Client* from,
                               const char* comment,
                               const char* comment1)
{
  struct Client *to;
  struct ConfItem *aconf;
  static char myname[HOSTLEN+1];
  dlink_node *ptr;

  for(ptr = serv_list.head; ptr; ptr=ptr->next)
    {
      to = ptr->data;

      if (IsMe(to) ||to == source_p->from || (to == client_p && IsCapable(to,CAP_QS)))
        continue;

      /* MyConnect(source_p) is rotten at this point: if source_p
       * was mine, ->from is NULL. 
       */
      /* The WALLOPS isn't needed here as pointed out by
       * comstud, since m_squit already does the notification.
       */

      if ((aconf = to->serv->sconf))
        strncpy_irc(myname, my_name_for_link(me.name, aconf), HOSTLEN);
      else
        strncpy_irc(myname, me.name, HOSTLEN);
      recurse_send_quits(client_p, source_p, to, comment1, myname);
    }

  recurse_remove_clients(source_p, comment1);
}


/*
** exit_client - This is old "m_bye". Name  changed, because this is not a
**        protocol function, but a general server utility function.
**
**        This function exits a client of *any* type (user, server, etc)
**        from this server. Also, this generates all necessary prototol
**        messages that this exit may cause.
**
**   1) If the client is a local client, then this implicitly
**        exits all other clients depending on this connection (e.g.
**        remote clients having 'from'-field that points to this.
**
**   2) If the client is a remote client, then only this is exited.
**
** For convenience, this function returns a suitable value for
** m_function return value:
**
**        CLIENT_EXITED        if (client_p == source_p)
**        0                if (client_p != source_p)
*/
int exit_client(
struct Client* client_p, /*
                     ** The local client originating the exit or NULL, if this
                     ** exit is generated by this server for internal reasons.
                     ** This will not get any of the generated messages.
                     */
struct Client* source_p,        /* Client exiting */
struct Client* from,        /* Client firing off this Exit, never NULL! */
const char* comment         /* Reason for the exit */
                   )
{
  struct Client        *target_p;
  struct Client        *next;
  char comment1[HOSTLEN + HOSTLEN + 2];
  dlink_node *m;

  /* source_p->flags |= FLAGS_DEADSOCKET; */

  if (MyConnect(source_p))
    {
      /* Attempt to flush any queued data */
      if (source_p->fd > -1)
        send_queued_write(source_p->fd, source_p);
      if (source_p->flags & FLAGS_IPHASH)
        remove_one_ip(&source_p->localClient->ip);

      delete_adns_queries(source_p->localClient->dns_query);
      delete_identd_queries(source_p);
      client_flush_input(source_p);

      /* This source_p could have status of one of STAT_UNKNOWN, STAT_CONNECTING
       * STAT_HANDSHAKE or STAT_UNKNOWN
       * all of which are lumped together into unknown_list
       *
       * In all above cases IsRegistered() will not be true.
       */
      if (!IsRegistered(source_p))
	{
	  m = dlinkFind(&unknown_list,source_p);
	  if( m != NULL )
	    {
	      dlinkDelete(m, &unknown_list);
	      free_dlink_node(m);
	    }
	}
      if (IsOper(source_p))
        {
	  m = dlinkFind(&oper_list,source_p);
	  if( m != NULL )
	    {
	      dlinkDelete(m, &oper_list);
	      free_dlink_node(m);
	    }
        }
      if (IsClient(source_p))
        {
          Count.local--;

          if(IsPerson(source_p))        /* a little extra paranoia */
            {
	      m = dlinkFind(&lclient_list,source_p);
	      if( m != NULL )
		{
		  dlinkDelete(m,&lclient_list);
		  free_dlink_node(m);
		}
            }
        }

      /* As soon as a client is known to be a server of some sort
       * it has to be put on the serv_list, or SJOIN's to this new server
       * from the connect burst will not be seen.
       */
      if (IsServer(source_p) || IsConnecting(source_p) ||
          IsHandshake(source_p))
	{
	  m = dlinkFind(&serv_list,source_p);
	  if( m != NULL )
	    {
	      dlinkDelete(m,&serv_list);
	      free_dlink_node(m);
#ifdef USE_TABLE_MODE
          unset_chcap_usage_counts(source_p);
#endif
	    }
	}

      if (IsServer(source_p))
        {
          Count.myserver--;
	  if(ServerInfo.hub)
	    remove_lazylink_flags(source_p->localClient->serverMask);
	  else
	    uplink = NULL;
        }

      source_p->flags |= FLAGS_CLOSING;

      if (IsPerson(source_p))
        sendto_realops_flags(FLAGS_CCONN, L_ALL,
                             "Client exiting: %s (%s@%s) [%s] [%s]",
                             source_p->name, source_p->username, source_p->host,
                             comment, source_p->localClient->sockhost);

      log_user_exit(source_p);

      if (source_p->fd >= 0)
	{
	  if (client_p != NULL && source_p != client_p)
	    sendto_one(source_p, "ERROR :Closing Link: %s %s (%s)",
		       source_p->host, source_p->name, comment);
	  else
	    sendto_one(source_p, "ERROR :Closing Link: %s (%s)",
		       source_p->host, comment);
	}
      /*
      ** Currently only server connections can have
      ** depending remote clients here, but it does no
      ** harm to check for all local clients. In
      ** future some other clients than servers might
      ** have remotes too...
      **
      ** Close the Client connection first and mark it
      ** so that no messages are attempted to send to it.
      ** (The following *must* make MyConnect(source_p) == FALSE!).
      ** It also makes source_p->from == NULL, thus it's unnecessary
      ** to test whether "source_p != target_p" in the following loops.
      */
     close_connection(source_p);
    }

  if(IsServer(source_p))
    {        
      if(ConfigServerHide.hide_servers)
	{
          /* 
          ** Replaces the name of the splitting server with
          ** a.server.on.<networkname>.net        
          ** when a client exits from a split, in an attempt to 
          ** hide topology but let clients detect a split still.
          */
	  ircsprintf(comment1,"%s a.server.on.%s.net",me.name, ServerInfo.network_name);
	}
      else
	{
	  if((source_p->serv) && (source_p->serv->up))
	    strcpy(comment1, source_p->serv->up);
	  else
	    strcpy(comment1, "<Unknown>" );

	  strcat(comment1," ");
	  strcat(comment1, source_p->name);
	}

      if (!refresh_user_links)
      {
        refresh_user_links = 1;
	eventAdd("write_links_file", write_links_file, NULL,
	         ConfigServerHide.links_delay, 0);
      }

      remove_dependents(client_p, source_p, from, comment, comment1);

      if (source_p->servptr == &me)
        {
          sendto_realops_flags(FLAGS_ALL, L_ALL,
		       "%s was connected for %d seconds.  %d/%d sendK/recvK.",
			       source_p->name, (int)(CurrentTime - source_p->firsttime),
			       source_p->localClient->sendK,
			       source_p->localClient->receiveK);
          ilog(L_NOTICE, "%s was connected for %d seconds.  %d/%d sendK/recvK.",
              source_p->name, CurrentTime - source_p->firsttime, 
              source_p->localClient->sendK, source_p->localClient->receiveK);

              /* Just for paranoia... this shouldn't be necessary if the
              ** remove_dependents() stuff works, but it's still good
              ** to do it.    MyConnect(source_p) has been set to false,
              ** so we look at servptr, which should be ok  -orabidoo
              */
              for (target_p = GlobalClientList; target_p; target_p = next)
                {
                  next = target_p->next;
                  if (!IsServer(target_p) && target_p->from == source_p)
                    {
                      ts_warn("Dependent client %s not on llist!?",
                              target_p->name);
                      exit_one_client(NULL, target_p, &me, comment1);
                    }
                }
              /*
              ** Second SQUIT all servers behind this link
              */
              for (target_p = GlobalClientList; target_p; target_p = next)
                {
                  next = target_p->next;
                  if (IsServer(target_p) && target_p->from == source_p)
                    {
                      ts_warn("Dependent server %s not on llist!?", 
                                     target_p->name);
                      exit_one_client(NULL, target_p, &me, me.name);
                    }
                }
            }
        }

  exit_one_client(client_p, source_p, from, comment);
  return client_p == source_p ? CLIENT_EXITED : 0;
}

/*
 * Count up local client memory
 */

/* XXX one common Client list now */
void count_local_client_memory(int *count,
			       int *local_client_memory_used)
{
  *count = local_client_count;
  *local_client_memory_used = local_client_count *
    (sizeof(struct Client) + sizeof(struct LocalUser));
}

/*
 * Count up remote client memory
 */
void count_remote_client_memory(int *count,
				int *remote_client_memory_used)
{
  *count = remote_client_count;
  *remote_client_memory_used = remote_client_count * sizeof(struct Client);
}


/*
 * accept processing, this adds a form of "caller ID" to ircd
 * 
 * If a client puts themselves into "caller ID only" mode,
 * only clients that match a client pointer they have put on 
 * the accept list will be allowed to message them.
 *
 * [ source.on_allow_list ] -> [ target1 ] -> [ target2 ]
 *
 * [target.allow_list] -> [ source1 ] -> [source2 ]
 *
 * i.e. a target will have a link list of source pointers it will allow
 * each source client then has a back pointer pointing back
 * to the client that has it on its accept list.
 * This allows for exit_one_client to remove these now bogus entries
 * from any client having an accept on them. 
 */

/*
 * accept_message
 *
 * inputs	- pointer to source client
 * 		- pointer to target client
 * output	- 1 if accept this message 0 if not
 * side effects - See if source is on target's allow list
 */
int accept_message(struct Client *source, struct Client *target)
{
  dlink_node *ptr;
  struct Client *target_p;

  for(ptr = target->allow_list.head; ptr; ptr = ptr->next )
    {
      target_p = ptr->data;
      if(source == target_p)
	return 1;
    }
  return 0;
}


/*
 * del_from_accept
 *
 * inputs	- pointer to source client
 * 		- pointer to target client
 * output	- NONE
 * side effects - Delete's source pointer to targets allow list
 *
 * Walk through the target's accept list, remove if source is found,
 * Then walk through the source's on_accept_list remove target if found.
 */
void del_from_accept(struct Client *source, struct Client *target)
{
  dlink_node *ptr;
  dlink_node *ptr2;
  dlink_node *next_ptr;
  dlink_node *next_ptr2;
  struct Client *target_p;

  for (ptr = target->allow_list.head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;

      target_p = ptr->data;
      if(source == target_p)
	{
	  dlinkDelete(ptr, &target->allow_list);
	  free_dlink_node(ptr);

	  for (ptr2 = source->on_allow_list.head; ptr2;
	       ptr2 = next_ptr2)
	    {
	      next_ptr2 = ptr2->next;

	      target_p = ptr2->data;
	      if (target == target_p)
		{
		  dlinkDelete(ptr2, &source->on_allow_list);
		  free_dlink_node(ptr2);
		}
	    }
	}
    }
}

/*
 * del_all_accepts
 *
 * inputs	- pointer to exiting client
 * output	- NONE
 * side effects - Walk through given clients allow_list and on_allow_list
 *                remove all references to this client
 */
void del_all_accepts(struct Client *client_p)
{
  dlink_node *ptr;
  dlink_node *next_ptr;
  struct Client *target_p;

  for (ptr = client_p->allow_list.head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;
      target_p = ptr->data;
      if(target_p != NULL)
        del_from_accept(target_p,client_p);
    }

  for (ptr = client_p->on_allow_list.head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;
      target_p = ptr->data;
      if(target_p != NULL)
	del_from_accept(client_p, target_p);
    }
}


/*
 * set_initial_nick
 * inputs
 * output
 * side effects	-
 *
 * This function is only called to set up an initially registering
 * client. 
 */
int
set_initial_nick(struct Client *client_p, struct Client *source_p,
                 char *nick)
{
 char buf[USERLEN + 1];
 /* Client setting NICK the first time */
  
 /* This had to be copied here to avoid problems.. */
 source_p->tsinfo = CurrentTime;
 if (source_p->name[0])
  del_from_client_hash_table(source_p->name, source_p);
 strcpy(source_p->name, nick);
 add_to_client_hash_table(nick, source_p);
 /* fd_desc is long enough */
 fd_note(client_p->fd, "Nick: %s", nick);
  
 /* They have the nick they want now.. */
 *client_p->llname = '\0';

 if (source_p->user)
 {
  strncpy_irc(buf, source_p->username, USERLEN);
  buf[USERLEN] = '\0';
  /*
   * USER already received, now we have NICK.
   * *NOTE* For servers "NICK" *must* precede the
   * user message (giving USER before NICK is possible
   * only for local client connection!). register_user
   * may reject the client and call exit_client for it
   * --must test this and exit m_nick too!!!
   */
#ifdef USE_IAUTH
  /*
   * Send the client to the iauth module for verification
   */
  BeginAuthorization(source_p);
#else
  if (register_local_user(client_p, source_p, nick, buf) == CLIENT_EXITED)
   return CLIENT_EXITED;
#endif
 }
 return 0;
}

/*
 * change_local_nick
 * inputs	- pointer to server
 *		- pointer to client
 * output	- 
 * side effects	- changes nick of a LOCAL user
 *
 */
int change_local_nick(struct Client *client_p, struct Client *source_p,
                      char *nick)
{
  /*
  ** Client just changing his/her nick. If he/she is
  ** on a channel, send note of change to all clients
  ** on that channel. Propagate notice to other servers.
  */

  source_p->tsinfo = CurrentTime;

  if( (source_p->localClient->last_nick_change +
       ConfigFileEntry.max_nick_time) < CurrentTime)
    source_p->localClient->number_of_nick_changes = 0;
  source_p->localClient->last_nick_change = CurrentTime;
  source_p->localClient->number_of_nick_changes++;

  if((ConfigFileEntry.anti_nick_flood && 
      (source_p->localClient->number_of_nick_changes
       <= ConfigFileEntry.max_nick_changes)) ||
     !ConfigFileEntry.anti_nick_flood || 
     (IsOper(source_p) && ConfigFileEntry.no_oper_flood))
    {
      sendto_realops_flags(FLAGS_NCHANGE, L_ALL,
			   "Nick change: From %s to %s [%s@%s]",
			   source_p->name, nick, source_p->username,
			   source_p->host);

      sendto_common_channels_local(source_p, ":%s!%s@%s NICK :%s",
				   source_p->name, source_p->username, source_p->host,
				   nick);
      if (source_p->user)
	{
	  add_history(source_p,1);
	  
	  /* Only hubs care about lazy link nicks not being sent on yet
	   * lazylink leafs/leafs always send their nicks up to hub,
	   * hence must always propogate nick changes.
	   * hubs might not propogate a nick change, if the leaf
	   * does not know about that client yet.
	   */
          sendto_server(client_p, source_p, NULL, NOCAPS, NOCAPS,
                        NOFLAGS, ":%s NICK %s :%lu", source_p->name,
                        nick, (unsigned long) source_p->tsinfo);
	}
    }
  else
    {
      sendto_one(source_p,
                 form_str(ERR_NICKTOOFAST),me.name, source_p->name,
                 source_p->name, nick, ConfigFileEntry.max_nick_time);
      return 0;
    }

  /* Finally, add to hash */
  del_from_client_hash_table(source_p->name, source_p);
  strcpy(source_p->name, nick);
  add_to_client_hash_table(nick, source_p);

  /* Make sure everyone that has this client on its accept list
   * loses that reference. 
   */

  del_all_accepts(source_p);

  /* fd_desc is long enough */
  fd_note(client_p->fd, "Nick: %s", nick);

  return 1;
}

/*
 * clean_nick_name - ensures that the given parameter (nick) is
 * really a proper string for a nickname (note, the 'nick'
 * may be modified in the process...)
 *
 *      RETURNS the length of the final NICKNAME (0, if
 *      nickname is illegal)
 *
 *  Nickname characters are in range
 *      'A'..'}', '_', '-', '0'..'9'
 *  anything outside the above set will terminate nickname.
 *  In addition, the first character cannot be '-'
 *  or a Digit.
 *
 *  Note:
 *      '~'-character should be NOT be allowed.
 */
int clean_nick_name(char* nick)
{
  char* ch   = nick;
  char* endp = ch + NICKLEN;
  assert(0 != nick);

  if (*nick == '-' || IsDigit(*nick)) /* first character in [0..9-] */
    return 0;
  
  for ( ; ch < endp && *ch; ++ch)
    {
      if (!IsNickChar(*ch))
	break;
    }
  *ch = '\0';

  return (ch - nick);
}

