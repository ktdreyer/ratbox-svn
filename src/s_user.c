/************************************************************************
 *   IRC - Internet Relay Chat, src/s_user.c
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

#include <sys/types.h>
#include <sys/stat.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <limits.h>

#include "tools.h"
#include "s_user.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "motd.h"
#include "ircd_handler.h"
#include "msg.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_stats.h"
#include "scache.h"
#include "send.h"
#include "supported.h"
#include "whowas.h"
#include "md5.h"
#include "memory.h"

static int valid_hostname(const char* hostname);
static int valid_username(const char* username);
static void report_and_set_user_flags( struct Client *, struct ConfItem * );
static int check_X_line(struct Client *client_p, struct Client *source_p);
void user_welcome(struct Client *source_p);
static int introduce_client(struct Client *client_p, struct Client *source_p,
			    struct User *user, char *nick);
int oper_up( struct Client *source_p, struct ConfItem *aconf );


/* table of ascii char letters to corresponding bitmask */

struct flag_item
{
  int mode;
  char letter;
};

static struct flag_item user_modes[] =
{
  {FLAGS_ADMIN, 'a'},
  {FLAGS_BOTS,  'b'},
  {FLAGS_CCONN, 'c'},
  {FLAGS_DEBUG, 'd'},
  {FLAGS_DRONE, 'e'},
  {FLAGS_FULL,  'f'},
  {FLAGS_CALLERID, 'g'},
  {FLAGS_INVISIBLE, 'i'},
  {FLAGS_SKILL, 'k'},
  {FLAGS_LOCOPS, 'l'},
  {FLAGS_NCHANGE, 'n'},
  {FLAGS_OPER, 'o'},
#ifdef PERSISTANT_CLIENTS
  {FLAGS_PERSISTANT, 'p'},
#endif
  {FLAGS_REJ, 'r'},
  {FLAGS_SERVNOTICE, 's'},
  {FLAGS_UNAUTH, 'u'},
  {FLAGS_WALLOP, 'w'},
  {FLAGS_EXTERNAL, 'x'},
  {FLAGS_SPY, 'y'},
  {FLAGS_OPERWALL, 'z'},
  {0, 0}
};

/* memory is cheap. map 0-255 to equivalent mode */

int user_modes_from_c_to_bitmask[] =
{ 
  /* 0x00 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x0F */
  /* 0x10 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x1F */
  /* 0x20 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x2F */
  /* 0x30 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x3F */
  0,            /* @ */
  0,            /* A */
  0,            /* B */
  0,            /* C */
  0,            /* D */
  0,            /* E */
  0,            /* F */
  0,            /* G */
  0,            /* H */
  0,            /* I */
  0,            /* J */
  0,            /* K */
  0,            /* L */
  0,            /* M */
  0,            /* N */
  0,            /* O */
  0,            /* P */
  0,            /* Q */
  0,            /* R */
  0,            /* S */
  0,            /* T */
  0,            /* U */
  0,            /* V */
  0,            /* W */
  0,            /* X */
  0,            /* Y */
  0,            /* Z 0x5A */
  0, 0, 0, 0, 0, /* 0x5F */ 
  /* 0x60 */       0,
  FLAGS_ADMIN,  /* a */
  FLAGS_BOTS,   /* b */
  FLAGS_CCONN,  /* c */
  FLAGS_DEBUG,  /* d */
  FLAGS_DRONE,  /* e */
  FLAGS_FULL,   /* f */
  FLAGS_CALLERID,  /* g */
  0,            /* h */
  FLAGS_INVISIBLE, /* i */
  0,            /* j */
  FLAGS_SKILL,  /* k */
  FLAGS_LOCOPS, /* l */
  0,            /* m */
  FLAGS_NCHANGE, /* n */
  FLAGS_OPER,   /* o */
#ifdef PERSISTANT_CLIENTS
  FLAGS_PERSISTANT,/* p */
#else
  0,               /* p */
#endif
  0,            /* q */
  FLAGS_REJ,    /* r */
  FLAGS_SERVNOTICE, /* s */
  0,            /* t */
  FLAGS_UNAUTH, /* u */
  0,            /* v */
  FLAGS_WALLOP, /* w */
  FLAGS_EXTERNAL, /* x */
  FLAGS_SPY,    /* y */
  FLAGS_OPERWALL, /* z 0x7A */
  0,0,0,0,0,     /* 0x7B - 0x7F */

  /* 0x80 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x9F */
  /* 0x90 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x9F */
  /* 0xA0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xAF */
  /* 0xB0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xBF */
  /* 0xC0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xCF */
  /* 0xD0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xDF */
  /* 0xE0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xEF */
  /* 0xF0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  /* 0xFF */
};

/*
 * show_opers - send the client a list of opers
 * inputs       - pointer to client to show opers to
 * output       - none
 * side effects - show who is opered on this server
 */
void show_opers(struct Client *client_p)
{
  struct Client        *client_p2;
  int j=0;
  struct ConfItem *aconf;
  dlink_node *oper_ptr;
  dlink_node *ptr;

  for(oper_ptr = oper_list.head; oper_ptr; oper_ptr = oper_ptr->next)
    {
      ++j;

      client_p2 = oper_ptr->data;

      if (MyClient(client_p) && IsOper(client_p))
        {
	  ptr = client_p2->localClient->confs.head;
	  aconf = ptr->data;

          sendto_one(client_p, ":%s %d %s :[%c][%s] %s (%s@%s) Idle: %d",
                     me.name, RPL_STATSDEBUG, client_p->name,
                     IsOper(client_p2) ? 'O' : 'o',
		     oper_privs_as_string(client_p2, aconf->port),
                     client_p2->name,
                     client_p2->username, client_p2->host,
                     (int)(CurrentTime - client_p2->user->last));
        }
      else
        {
          sendto_one(client_p, ":%s %d %s :[%c] %s (%s@%s) Idle: %d",
                     me.name, RPL_STATSDEBUG, client_p->name,
                     IsOper(client_p2) ? 'O' : 'o',
                     client_p2->name,
                     client_p2->username, client_p2->host,
                     (int)(CurrentTime - client_p2->user->last));
        }
    }

  sendto_one(client_p, ":%s %d %s :%d OPER%s", me.name, RPL_STATSDEBUG,
             client_p->name, j, (j==1) ? "" : "s");
}

/*
 * show_lusers -
 *
 * inputs	- pointer to client
 * output	-
 * side effects	- display to client user counts etc.
 */
int show_lusers(struct Client *source_p) 
{
  if(!GlobalSetOptions.hide_server || IsOper(source_p))
    sendto_one(source_p, form_str(RPL_LUSERCLIENT), me.name, source_p->name,
              (Count.total-Count.invisi), Count.invisi, Count.server);
  else
    sendto_one(source_p, form_str(RPL_LUSERCLIENT), me.name, source_p->name,
              (Count.total-Count.invisi), Count.invisi, 1);
  if (Count.oper > 0)
    sendto_one(source_p, form_str(RPL_LUSEROP), me.name, source_p->name,
               Count.oper);

  if (Count.unknown > 0)
    sendto_one(source_p, form_str(RPL_LUSERUNKNOWN), me.name, source_p->name,
               Count.unknown);

  if (Count.chan > 0)
    sendto_one(source_p, form_str(RPL_LUSERCHANNELS),
               me.name, source_p->name, Count.chan);

  if(!GlobalSetOptions.hide_server || IsOper(source_p))
    {
      sendto_one(source_p, form_str(RPL_LUSERME),
                 me.name, source_p->name, Count.local, Count.myserver);
      sendto_one(source_p, form_str(RPL_LOCALUSERS), me.name, source_p->name,
                 Count.local, Count.max_loc);
    }

  sendto_one(source_p, form_str(RPL_GLOBALUSERS), me.name, source_p->name,
             Count.total, Count.max_tot);

  if(!GlobalSetOptions.hide_server || IsOper(source_p))
    sendto_one(source_p, form_str(RPL_STATSCONN), me.name, source_p->name,
               MaxConnectionCount, MaxClientCount, Count.totalrestartcount);

  if (Count.local > MaxClientCount)
    MaxClientCount = Count.local;

  if ((Count.local + Count.myserver) > MaxConnectionCount)
    MaxConnectionCount = Count.local + Count.myserver; 

  return 0;
}

/*
 * show_isupport
 *
 * inputs	- pointer to client
 * output	- 
 * side effects	- display to client what we support (for them)
 */
int show_isupport(struct Client *source_p) 
{
  char isupportbuffer[512];
  
  ircsprintf(isupportbuffer,FEATURES,FEATURESVALUES);
  sendto_one(source_p, form_str(RPL_ISUPPORT), me.name, source_p->name, 
  	     isupportbuffer);
  	     
  return 0;	     
}


/*
** register_local_user
**      This function is called when both NICK and USER messages
**      have been accepted for the client, in whatever order. Only
**      after this, is the USER message propagated.
**
**      NICK's must be propagated at once when received, although
**      it would be better to delay them too until full info is
**      available. Doing it is not so simple though, would have
**      to implement the following:
**
**      (actually it has been implemented already for a while) -orabidoo
**
**      1) user telnets in and gives only "NICK foobar" and waits
**      2) another user far away logs in normally with the nick
**         "foobar" (quite legal, as this server didn't propagate
**         it).
**      3) now this server gets nick "foobar" from outside, but
**         has already the same defined locally. Current server
**         would just issue "KILL foobar" to clean out dups. But,
**         this is not fair. It should actually request another
**         nick from local user or kill him/her...
*/

int register_local_user(struct Client *client_p, struct Client *source_p, 
			char *nick, char *username)
{
  struct ConfItem*  aconf;
  struct User*     user = source_p->user;
  char        tmpstr2[IRCD_BUFSIZE];
  char	      ipaddr[HOSTIPLEN];
  int  status;
  dlink_node *ptr;
  dlink_node *m;
  char *id;
  assert(0 != source_p);
  assert(source_p->username != username);

  user->last = CurrentTime;

  /* pointed out by Mortiis, never be too careful */
  if(strlen(username) > USERLEN)
    username[USERLEN] = '\0';

  if( ( status = check_client(client_p, source_p, username )) < 0 )
    return(CLIENT_EXITED);

  if(!valid_hostname(source_p->host))
    {
      sendto_one(source_p,":%s NOTICE %s :*** Notice -- You have an illegal character in your hostname", 
		 me.name, source_p->name );

      strncpy(source_p->host,source_p->localClient->sockhost,HOSTIPLEN+1);
    }

  ptr = source_p->localClient->confs.head;
  aconf = ptr->data;

  if (aconf == NULL)
    {
      (void)exit_client(client_p, source_p, &me, "*** Not Authorized");
      return(CLIENT_EXITED);
    }

  if (!IsGotId(source_p))
    {
      if (IsNeedIdentd(aconf))
	{
	  ServerStats->is_ref++;
	  sendto_one(source_p,
		     ":%s NOTICE %s :*** Notice -- You need to install identd to use this server",
		     me.name, client_p->name);
	  (void)exit_client(client_p, source_p, &me, "Install identd");
	  return(CLIENT_EXITED);
	}
      else
	strncpy_irc(source_p->username, username, USERLEN);

      if (IsNoTilde(aconf))
	{
	  strncpy_irc(source_p->username, username, USERLEN);
	}
      else
	{
	  *source_p->username = '~';
	  strncpy_irc(&source_p->username[1], username, USERLEN - 1);
	}
      source_p->username[USERLEN] = '\0';
    }

  /* password check */
  if (!BadPtr(aconf->passwd) &&
      strcmp(source_p->localClient->passwd, aconf->passwd))
    {
      ServerStats->is_ref++;
      sendto_one(source_p, form_str(ERR_PASSWDMISMATCH),
		 me.name, source_p->name);
      (void)exit_client(client_p, source_p, &me, "Bad Password");
      return(CLIENT_EXITED);
    }
  memset(source_p->localClient->passwd,0, sizeof(source_p->localClient->passwd));

  /* report if user has &^>= etc. and set flags as needed in source_p */
  report_and_set_user_flags(source_p, aconf);
  
  /* Limit clients */
  /*
   * We want to be able to have servers and F-line clients
   * connect, so save room for "buffer" connections.
   * Smaller servers may want to decrease this, and it should
   * probably be just a percentage of the MAXCLIENTS...
   *   -Taner
   */
  /* Except "F:" clients */
  if ( ( (Count.local + 1) >= (GlobalSetOptions.maxclients+MAX_BUFFER)
	 ||
	 (Count.local +1) >= (GlobalSetOptions.maxclients - 5) )
       &&
       !(IsExemptLimits(source_p)) )
    {
      sendto_realops_flags(FLAGS_FULL,
			   "Too many clients, rejecting %s[%s].",
			   nick, source_p->host);
      ServerStats->is_ref++;
      (void)exit_client(client_p, source_p, &me,
			"Sorry, server is full - try later");
      return(CLIENT_EXITED);
    }

  /* valid user name check */

  if (!valid_username(source_p->username))
    {
      sendto_realops_flags(FLAGS_REJ,"Invalid username: %s (%s@%s)",
			   nick, source_p->username, source_p->host);
      ServerStats->is_ref++;
      ircsprintf(tmpstr2, "Invalid username [%s]", source_p->username);
      (void)exit_client(client_p, source_p, &me, tmpstr2);
      return(CLIENT_EXITED);
    }

  /* end of valid user name check */
  
  if ((status = check_X_line(client_p,source_p)) < 0)
    return status;

  if (source_p->user->id[0] == '\0') 
    {
      for (id = id_get(); hash_find_id(id, NULL); id = id_get())
        ;
      strcpy(source_p->user->id, id);
      add_to_id_hash_table(id, source_p);
      id = id_get();
      strcpy(user->id_key, id);
    }

  inetntop(source_p->localClient->aftype, &IN_ADDR(source_p->localClient->ip), 
  				ipaddr, HOSTIPLEN);
  sendto_realops_flags(FLAGS_CCONN,
		       "Client connecting: %s (%s@%s) [%s] {%s}",
		       nick, source_p->username, source_p->host,
		       ipaddr,
		       get_client_class(source_p));
  
  sendto_realops_flags(FLAGS_DRONE,
		       "Cn: %s (%s@%s) [%s] [%s]",
		       nick, source_p->username, source_p->host,
		       ipaddr,
		       source_p->info);
  
  if ((++Count.local) > Count.max_loc)
    {
      Count.max_loc = Count.local;
      if (!(Count.max_loc % 10))
	sendto_realops_flags(FLAGS_ALL,"New Max Local Clients: %d",
			     Count.max_loc);
    }

  SetClient(source_p);

  /* XXX source_p->servptr is &me, since local client */
  source_p->servptr = find_server(user->server);
  add_client_to_llist(&(source_p->servptr->serv->users), source_p);

  /* Increment our total user count here */
  if (++Count.total > Count.max_tot)
    Count.max_tot = Count.total;

  Count.totalrestartcount++;
  user_welcome(source_p);

  m = dlinkFind(&unknown_list, source_p);

  assert(m != NULL);
  dlinkDelete(m, &unknown_list);
  dlinkAdd(source_p, m, &lclient_list);

  return (introduce_client(client_p, source_p, user, nick));
}

/*
 * register_remote_user
 *
 * inputs
 * output
 * side effects	- This function is called when a remote client
 *		  is introduced by a server.
 */
int register_remote_user(struct Client *client_p, struct Client *source_p, 
			 char *nick, char *username)
{
  struct User*     user = source_p->user;
  struct Client *target_p;
  
  assert(0 != source_p);
  assert(source_p->username != username);

  user->last = CurrentTime;

  /* pointed out by Mortiis, never be too careful */
  if(strlen(username) > USERLEN)
    username[USERLEN] = '\0';

  strncpy_irc(source_p->username, username, USERLEN);

  SetClient(source_p);

  /* Increment our total user count here */
  if (++Count.total > Count.max_tot)
    Count.max_tot = Count.total;

  source_p->servptr = find_server(user->server);

  if (source_p->servptr == NULL)
    {
      sendto_realops_flags(FLAGS_ALL,"Ghost killed: %s on invalid server %s",
			   source_p->name, source_p->user->server);

      kill_client(client_p, source_p, "%s (Ghosted %s doesn't exist)",
		  me.name, user->server);

#if 0
      sendto_one(client_p,":%s KILL %s :%s (Ghosted, %s doesn't exist)",
                 me.name, source_p->name, me.name, user->server);
#endif
      source_p->flags |= FLAGS_KILLED;
      return exit_client(NULL, source_p, &me, "Ghost");
    }

  add_client_to_llist(&(source_p->servptr->serv->users), source_p);

  if ((target_p = find_server(user->server)) && target_p->from != source_p->from)
    {
      sendto_realops_flags(FLAGS_DEBUG, 
			   "Bad User [%s] :%s USER %s@%s %s, != %s[%s]",
			   client_p->name, nick, source_p->username,
			   source_p->host, user->server,
			   target_p->name, target_p->from->name);
      kill_client(client_p, source_p,
		 ":%s (%s != %s[%s] USER from wrong direction)",
		  me.name,
		  user->server,
		  target_p->from->name, target_p->from->host);

#if 0
      sendto_one(client_p,
		 ":%s KILL %s :%s (%s != %s[%s] USER from wrong direction)",
		 me.name, source_p->name, me.name, user->server,
		 target_p->from->name, target_p->from->host);

#endif
      source_p->flags |= FLAGS_KILLED;
      return exit_client(source_p, source_p, &me,
			 "USER server wrong direction");
      
    }
  /*
   * Super GhostDetect:
   * If we can't find the server the user is supposed to be on,
   * then simply blow the user away.        -Taner
   */
  if (!target_p)
    {
      kill_client(client_p, source_p,
		  "%s GHOST (no server %s on the net)",		  
		  me.name, user->server);
#if 0
      sendto_one(client_p,
		 ":%s KILL %s :%s GHOST (no server %s on the net)",
		 me.name,
		 source_p->name, me.name, user->server);
#endif
      sendto_realops_flags(FLAGS_ALL,"No server %s for user %s[%s@%s] from %s",
			   user->server, source_p->name, source_p->username,
			   source_p->host, source_p->from->name);
      source_p->flags |= FLAGS_KILLED;
      return exit_client(source_p, source_p, &me, "Ghosted Client");
    }

  return (introduce_client(client_p, source_p, user, nick));
}

/*
 * introduce_clients
 *
 * inputs	-
 * output	-
 * side effects - This common function introduces a client to the rest
 *		  of the net, either from a local client connect or
 *		  from a remote connect.
 */
static int
introduce_client(struct Client *client_p, struct Client *source_p,
		 struct User *user, char *nick)
{
  dlink_node *server_node;
  struct Client *server;
  static char ubuf[12];

  send_umode(NULL, source_p, 0, SEND_UMODES, ubuf);

  if (!*ubuf)
    {
      ubuf[0] = '+';
      ubuf[1] = '\0';
    }


  /* arghhh one could try not introducing new nicks to ll leafs
   * but then you have to introduce them "on the fly" in SJOIN
   * not fun.
   * Its not going to cost much more bandwidth to simply let new
   * nicks just ride on through.
   */

  /*
   * We now introduce nicks "on the fly" in SJOIN anyway --
   * you _need_ to if you aren't going to burst everyone initially.
   *
   * Only send to non CAP_LL servers, unless we're a lazylink leaf,
   * in that case just send it to the uplink.
   * -davidt
   * rewritten to cope with UIDs .. eww eww eww --is
   */
  
  if (!ServerInfo.hub && uplink && IsCapable(uplink,CAP_LL)
      && client_p != uplink) 
    {
      if (IsCapable(uplink, CAP_UID) && HasID(source_p))
	{
	  sendto_one(uplink, "CLIENT %s %d %lu %s %s %s %s %s :%s",
		     nick, source_p->hopcount+1, source_p->tsinfo,
		     ubuf, source_p->username, source_p->host, user->server,
		     user->id, source_p->info);
	}
      else
	{
	  sendto_one(uplink, "NICK %s %d %lu %s %s %s %s :%s",
		     nick, source_p->hopcount+1, source_p->tsinfo,
		     ubuf, source_p->username, source_p->host, user->server,
		     source_p->info);
	}
    }
  else
    {
      for (server_node = serv_list.head; server_node; server_node = server_node->next)
	{
	  server = (struct Client *) server_node->data;
		  
	  if (IsCapable(server, CAP_LL) || server == client_p)
	    continue;
		  
	  if (IsCapable(server, CAP_UID) && HasID(source_p))
	    sendto_one(server, "CLIENT %s %d %lu %s %s %s %s %s :%s",
		       nick, source_p->hopcount+1, source_p->tsinfo,
		       ubuf, source_p->username, source_p->host, user->server,
		       user->id, source_p->info);
	  else
	    sendto_one(server, "NICK %s %d %lu %s %s %s %s :%s",
		       nick, source_p->hopcount+1, source_p->tsinfo,
		       ubuf, source_p->username, source_p->host, user->server,
		       source_p->info);
	}
    }
  
  if (ubuf[1])
    send_umode_out(client_p, source_p, 0);
  return 0;
}

/* 
 * valid_hostname - check hostname for validity
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 *
 * NOTE: this doesn't allow a hostname to begin with a dot and
 * will not allow more dots than chars.
 */
static int valid_hostname(const char* hostname)
{
  int         dots  = 0;
  int         chars = 0;
  const char* p     = hostname;

  assert(0 != p);

  if ('.' == *p)
    return NO;

  while (*p)
    {
      if (!IsHostChar(*p))
        return NO;

      if ('.' == *p || ':' == *p)
        ++dots;
      else
        ++chars;

      p++;
    }

  if( dots == 0 )
    return NO;

  return ( (dots > chars) ? NO : YES);
}

/* 
 * valid_username - check username for validity
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 * 
 * Absolutely always reject any '*' '!' '?' '@' in an user name
 * reject any odd control characters names.
 * Allow '.' in username to allow for "first.last"
 * style of username
 */
static int valid_username(const char* username)
{
  int dots = 0;
  const char *p = username;

  assert(0 != p);

  if ('~' == *p)
    ++p;

  /* reject usernames that don't start with an alphanum
   * i.e. reject jokers who have '-@somehost' or '.@somehost'
   * or "-hi-@somehost", "h-----@somehost" would still be accepted.
   */
  if (!IsAlNum(*p))
    return NO;

  while (*++p)
    {
      if((*p == '.') && ConfigFileEntry.dots_in_ident)
        {
          dots++;
          if(dots > ConfigFileEntry.dots_in_ident)
            return NO;
          if(!IsUserChar(p[1]))
            return NO;
        }
      else if (!IsUserChar(*p))
        return NO;
    }
  return YES;
}

/* report_and_set_user_flags
 *
 * Inputs       - pointer to source_p
 *              - pointer to aconf for this user
 * Output       - NONE
 * Side effects -
 * Report to user any special flags they are getting, and set them.
 */

static void 
report_and_set_user_flags(struct Client *source_p,struct ConfItem *aconf)
{
  /* If this user is being spoofed, tell them so */
  if(IsConfDoSpoofIp(aconf))
    {
      sendto_one(source_p,
                 ":%s NOTICE %s :*** Spoofing your IP. congrats.",
                 me.name,source_p->name);
    }

  /* If this user is in the exception class, Set it "E lined" */
  if(IsConfExemptKline(aconf))
    {
      SetExemptKline(source_p);
      sendto_one(source_p,
         ":%s NOTICE %s :*** You are exempt from K/D/G lines. congrats.",
                 me.name,source_p->name);
    }

  /* If this user is exempt from user limits set it F lined" */
  if(IsConfExemptLimits(aconf))
    {
      SetExemptLimits(source_p);
      sendto_one(source_p,
                 ":%s NOTICE %s :*** You are exempt from user limits. congrats.",
                 me.name,source_p->name);
    }

  /* If this user is exempt from idle time outs */
  if(IsConfIdlelined(aconf))
    {
      SetIdlelined(source_p);
      sendto_one(source_p,
         ":%s NOTICE %s :*** You are exempt from idle limits. congrats.",
                 me.name,source_p->name);
    }
}


/*
 * do_local_user
 *
 * inputs	-
 * output	-
 * side effects -
 */
int do_local_user(char* nick, struct Client* client_p, struct Client* source_p,
		  char* username, char *host, char *server, char *realname)
{
  unsigned int oflags;
  struct User* user;

  assert(0 != source_p);
  assert(source_p->username != username);

  user = make_user(source_p);

  oflags = source_p->flags;

  if (!IsUnknown(source_p))
    {
      sendto_one(source_p, form_str(ERR_ALREADYREGISTRED), me.name, nick);
      return 0;
    }
  source_p->flags |= FLAGS_INVISIBLE;

  if (!(oflags & FLAGS_INVISIBLE) && IsInvisible(source_p))
    Count.invisi++;
  /*
   * don't take the clients word for it, ever
   *  strncpy_irc(user->host, host, HOSTLEN); 
   */
  user->server = me.name;

  strncpy_irc(source_p->info, realname, REALLEN);
  
  if (source_p->name[0])
    /* NICK already received, now I have USER... */
    return register_local_user(client_p, source_p, source_p->name, username);
  else
    {
      if (!IsGotId(source_p)) 
        {
          /*
           * save the username in the client
           */
          strncpy_irc(source_p->username, username, USERLEN);
        }
    }
  return 0;
}

/*
 * do_remote_user
 *
 * inputs	-
 * output	-
 * side effects -
 */
int do_remote_user(char* nick, struct Client* client_p, struct Client* source_p,
		   char* username, char *host, char *server, char *realname,
		   char *id)
{
  unsigned int oflags;
  struct User* user;

  assert(0 != source_p);
  assert(source_p->username != username);

  user = make_user(source_p);

  oflags = source_p->flags;

  /*
   * coming from another server, take the servers word for it
   */
  user->server = find_or_add(server);
  strncpy_irc(source_p->host, host, HOSTLEN); 
  strncpy_irc(source_p->info, realname, REALLEN);
  if (id)
	  strcpy(source_p->user->id, id);
  
#if 0
  /* XXX dont do this (talk to is-) */
  /* if it has no ID, set the ID to the nick just in case */
  if (!id)
	  strcpy(source_p->user->id, nick);
#endif

  return register_remote_user(client_p, source_p, source_p->name, username);
}

/*
 * user_mode - set get current users mode
 *
 * m_umode() added 15/10/91 By Darren Reed.
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 */
int user_mode(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
  int   flag;
  int   i;
  char  **p, *m;
  struct Client *target_p;
  int   what, setflags;
  int   badflag = NO;		/* Only send one bad flag notice */
  char  buf[BUFSIZE];
  dlink_node *ptr;
  struct ConfItem *aconf;

  what = MODE_ADD;

  if (parc < 2)
    {
      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "MODE");
      return 0;
    }

  if (!(target_p = find_person(parv[1], NULL)))
    {
      if (MyConnect(source_p))
        sendto_one(source_p, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[1]);
      return 0;
    }

  /* Dont know why these were commented out.. put them back using new sendto() funcs */
  if (IsServer(source_p))
    {
       sendto_realops_flags(FLAGS_ADMIN, "*** Mode for User %s from %s",
                            parv[1], source_p->name);
       return 0;
    }

  if (source_p != target_p || target_p->from != source_p->from)
    {
       sendto_one(source_p, form_str(ERR_USERSDONTMATCH), me.name, parv[0]);
       return 0;
    }


  if (parc < 3)
    {
      m = buf;
      *m++ = '+';

      for (i = 0; user_modes[i].letter && (m - buf < BUFSIZE - 4);i++)
        if (source_p->umodes & user_modes[i].mode)
          *m++ = user_modes[i].letter;
      *m = '\0';
      sendto_one(source_p, form_str(RPL_UMODEIS), me.name, parv[0], buf);
      return 0;
    }

  /* find flags already set for user */
  setflags = source_p->umodes;
  
  /*
   * parse mode change string(s)
   */
  for (p = &parv[2]; p && *p; p++ )
    for (m = *p; *m; m++)
      switch(*m)
        {
        case '+' :
          what = MODE_ADD;
          break;
        case '-' :
          what = MODE_DEL;
          break;        

        case 'o' :
          if(what == MODE_ADD)
            {
              if(IsServer(client_p) && !IsOper(source_p))
                {
                  ++Count.oper;
                  SetOper(source_p);
                }
            }
          else
            {
	      /* Only decrement the oper counts if an oper to begin with
               * found by Pat Szuta, Perly , perly@xnet.com 
               */

              if(!IsOper(source_p))
                break;

              ClearOper(source_p);
	      source_p->umodes &= ~ConfigFileEntry.oper_only_umodes;
			  
              Count.oper--;

              if (MyConnect(source_p))
                {
                  dlink_node *dm;

		  ptr = source_p->localClient->confs.head;
		  aconf = ptr->data;
                  detach_conf(source_p,aconf);

                  source_p->flags2 &= ~(FLAGS2_OPER_GLOBAL_KILL|
                                    FLAGS2_OPER_REMOTE|
                                    FLAGS2_OPER_UNKLINE|
                                    FLAGS2_OPER_GLINE|
                                    FLAGS2_OPER_N|
                                    FLAGS2_OPER_K|
                                    FLAGS2_OPER_ADMIN);

		  dm = dlinkFind(&oper_list,source_p);
		  if(dm != NULL)
		    {
		      dlinkDelete(dm,&oper_list);
		      free_dlink_node(dm);
		    }

		  /*
		    20001216:
		    reattach to "old" iline
		    - einride
		  */
		  remove_one_ip(&source_p->localClient->ip);
		  check_client(source_p->servptr, source_p, source_p->username);
                }
            }
          break;

          /* we may not get these,
           * but they shouldnt be in default
           */
        case ' ' :
        case '\n' :
        case '\r' :
        case '\t' :
          break;

        default :
          if( (flag = user_modes_from_c_to_bitmask[(unsigned char)*m]))
            {
              if (MyConnect(source_p) && !IsOper(source_p) &&
                 (ConfigFileEntry.oper_only_umodes & flag))
                {
                  badflag = YES;
                }
              else
                {
                  if (what == MODE_ADD)
                    source_p->umodes |= flag;
                  else
                    source_p->umodes &= ~flag;  
                }
            }
          else
            {
              if (MyConnect(source_p))
                badflag = YES;
            }
          break;
        }

  if(badflag)
    sendto_one(source_p, form_str(ERR_UMODEUNKNOWNFLAG), me.name, parv[0]);

  if ((source_p->umodes & FLAGS_NCHANGE) && !IsSetOperN(source_p))
    {
      sendto_one(source_p,":%s NOTICE %s :*** You need oper and N flag for +n",
                 me.name,parv[0]);
      source_p->umodes &= ~FLAGS_NCHANGE; /* only tcm's really need this */
    }

#ifdef PERSISTANT_CLIENTS
  if (MyConnect(source_p) && (source_p->umodes & ~setflags &
      FLAGS_PERSISTANT))
    {
     for (ptr=source_p->localClient->confs.head; ptr; ptr=ptr->next)
       {
        aconf = (struct ConfItem*)ptr->data;
        if ((aconf->status & CONF_CLIENT))
          {
           if (!(aconf->flags & CONF_FLAGS_PERSISTANT))
             {
              sendto_one(source_p,
                ":%s NOTICE %s :Your auth block does not allow +p",
                me.name, source_p->name);
              source_p->umodes &= ~FLAGS_PERSISTANT;
             }
           break;
          }
       }
    }
#endif

  if (MyConnect(source_p) && (source_p->umodes & FLAGS_ADMIN) && !IsSetOperAdmin(source_p))
    {
      sendto_one(source_p,":%s NOTICE %s :*** You need oper and A flag for +a",
                 me.name, parv[0]);
      source_p->umodes &= ~FLAGS_ADMIN;
    }


  if (!(setflags & FLAGS_INVISIBLE) && IsInvisible(source_p))
    ++Count.invisi;
  if ((setflags & FLAGS_INVISIBLE) && !IsInvisible(source_p))
    --Count.invisi;
  /*
   * compare new flags with old flags and send string which
   * will cause servers to update correctly.
   */
  send_umode_out(client_p, source_p, setflags);

  return 0;
}
        
/*
 * send the MODE string for user (user) to connection client_p
 * -avalon
 */
void send_umode(struct Client *client_p, struct Client *source_p, int old, 
		int sendmask,  char *umode_buf)
{
  int   i;
  int flag;
  char  *m;
  int   what = MODE_NULL;

  /*
   * build a string in umode_buf to represent the change in the user's
   * mode between the new (source_p->flag) and 'old'.
   */
  m = umode_buf;
  *m = '\0';

  for (i = 0; user_modes[i].letter; i++ )
    {
      flag = user_modes[i].mode;

      if (MyClient(source_p) && !(flag & sendmask))
        continue;
      if ((flag & old) && !(source_p->umodes & flag))
        {
          if (what == MODE_DEL)
            *m++ = user_modes[i].letter;
          else
            {
              what = MODE_DEL;
              *m++ = '-';
              *m++ = user_modes[i].letter;
            }
        }
      else if (!(flag & old) && (source_p->umodes & flag))
        {
          if (what == MODE_ADD)
            *m++ = user_modes[i].letter;
          else
            {
              what = MODE_ADD;
              *m++ = '+';
              *m++ = user_modes[i].letter;
            }
        }
    }
  *m = '\0';
  if (*umode_buf && client_p)
    sendto_one(client_p, ":%s MODE %s :%s",
               source_p->name, source_p->name, umode_buf);
}

/*
 * send_umode_out
 *
 * inputs	-
 * output	- NONE
 * side effects - Only send ubuf out to servers that know about this client
 */
void send_umode_out(struct Client *client_p,
		    struct Client *source_p,
		    int old)
{
  struct Client *target_p;
  char buf[BUFSIZE];
  dlink_node *ptr;

  send_umode(NULL, source_p, old, SEND_UMODES, buf);

  for(ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      target_p = ptr->data;

      if((target_p != client_p) && (target_p != source_p) && (*buf))
        {
          if((!(ServerInfo.hub && IsCapable(target_p, CAP_LL)))
             || (target_p->localClient->serverMask &
                 source_p->lazyLinkClientExists))
            sendto_one(target_p, ":%s MODE %s :%s",
                       source_p->name, source_p->name, buf);
        }
    }

  if (client_p && MyClient(client_p))
    send_umode(client_p, source_p, old, ALL_UMODES, buf);
}

/* 
 * user_welcome
 *
 * inputs	- client pointer to client to welcome
 * output	- NONE
 * side effects	-
 */
void user_welcome(struct Client *source_p)
{
  sendto_one(source_p, form_str(RPL_WELCOME), me.name, source_p->name, 
             ServerInfo.network_name, source_p->name );
  /* This is a duplicate of the NOTICE but see below...*/
  sendto_one(source_p, form_str(RPL_YOURHOST), me.name, source_p->name,
	     get_listener_name(source_p->localClient->listener), version);
  
  /*
  ** Don't mess with this one - IRCII needs it! -Avalon
  */
  sendto_one(source_p,
	     "NOTICE %s :*** Your host is %s, running version %s",
	     source_p->name, get_listener_name(source_p->localClient->listener),
	     version);
  
  sendto_one(source_p, form_str(RPL_CREATED),me.name,source_p->name,creation);
  sendto_one(source_p, form_str(RPL_MYINFO), me.name, source_p->name,
	     me.name, version);
#ifdef PERSISTANT_CLIENTS
  sendto_one(source_p, form_str(RPL_YOURID), me.name, source_p->name,
             source_p->user->id, source_p->user->id_key);
#endif
  show_isupport(source_p);
  
  show_lusers(source_p);

  if (ConfigFileEntry.short_motd)
    {
      sendto_one(source_p,"NOTICE %s :*** Notice -- motd was last changed at %s",
		 source_p->name,
		 ConfigFileEntry.motd.lastChangedDate);
      
      sendto_one(source_p,
		 "NOTICE %s :*** Notice -- Please read the motd if you haven't read it",
		 source_p->name);
    
      sendto_one(source_p, form_str(RPL_MOTDSTART),
		 me.name, source_p->name, me.name);
    
      sendto_one(source_p,
		 form_str(RPL_MOTD),
		 me.name, source_p->name,
		 "*** This is the short motd ***"
		 );

      sendto_one(source_p, form_str(RPL_ENDOFMOTD),
		 me.name, source_p->name);
    }
  else  
    SendMessageFile(source_p, &ConfigFileEntry.motd);

  if (IsRestricted(source_p))
    {
      sendto_one(source_p,form_str(ERR_RESTRICTED),
		 me.name, source_p->name);
    }
}

/*
 * check_X_line
 * inputs	- pointer to client to test
 * outupt	- -1 if exiting 0 if ok
 * side effects	-
 */
static int check_X_line(struct Client *client_p, struct Client *source_p)
{
  struct ConfItem *aconf;
  char *reason;

  if(IsOper(source_p))
    return 0;

  if ((aconf = find_x_conf(source_p->info)))
    {
      if(aconf->passwd)
	reason = aconf->passwd;
      else
	reason = "NONE";
              
      if(aconf->port)
	{
	  if (aconf->port == 1)
	    {
	      sendto_realops_flags(FLAGS_REJ,
				   "X-line Rejecting [%s] [%s], user %s",
				   source_p->info,
				   reason,
				   get_client_name(client_p, HIDE_IP));
	    }
	  ServerStats->is_ref++;      
	  (void)exit_client(client_p, source_p, &me, "Bad user info");
	  return (CLIENT_EXITED);
	}
      else
	sendto_realops_flags(FLAGS_REJ,
			     "X-line Warning [%s] [%s], user %s",
			     source_p->info,
			     reason,
			     get_client_name(client_p, HIDE_IP));
    }

  return 0;
}

/*
 * oper_up
 *
 * inputs	- pointer to given client to oper
 *		- pointer to ConfItem to use
 * output	- none
 * side effects	-
 * Blindly opers up given source_p, using aconf info
 * all checks on passwords have already been done.
 * This could also be used by rsa oper routines. 
 */

int oper_up( struct Client *source_p,
                    struct ConfItem *aconf )
{
  int old = (source_p->umodes & ALL_UMODES);
  char *operprivs=NULL;
  dlink_node *ptr;
  struct ConfItem *found_aconf;
  dlink_node *m;

  SetOper(source_p);
  if((int)aconf->hold)
    {
      source_p->umodes |= ((int)aconf->hold & ALL_UMODES); 
      if( !IsSetOperN(source_p) )
	source_p->umodes &= ~FLAGS_NCHANGE;
      
      sendto_one(source_p, ":%s NOTICE %s :*** Oper flags set from conf",
		 me.name,source_p->name);
    }
  else
    {
      if(ConfigFileEntry.oper_umodes)
        {
          source_p->umodes |= ConfigFileEntry.oper_umodes & ALL_UMODES;
        }
      else
        {
          source_p->umodes |= (FLAGS_SERVNOTICE|FLAGS_OPERWALL|FLAGS_WALLOP|FLAGS_LOCOPS) & ALL_UMODES;
        }
    }
	
  SetIPHidden(source_p);
  Count.oper++;

  SetExemptKline(source_p);
      
  m = make_dlink_node();
  dlinkAdd(source_p,m,&oper_list);

  if(source_p->localClient->confs.head)
    {
      ptr = source_p->localClient->confs.head;
      if(ptr)
	{
	  found_aconf = ptr->data;
	  if(found_aconf)
	    operprivs = oper_privs_as_string(source_p,found_aconf->port);
	}
    }
  else
    operprivs = "";

  if (IsSetOperAdmin(source_p))
    source_p->umodes |= FLAGS_ADMIN;

  sendto_realops_flags(FLAGS_ALL,
		       "%s (%s@%s) is now an operator", source_p->name,
		       source_p->username, source_p->host);
  send_umode_out(source_p, source_p, old);
  sendto_one(source_p, form_str(RPL_YOUREOPER), me.name, source_p->name);
  sendto_one(source_p, ":%s NOTICE %s :*** Oper privs are %s", me.name,
             source_p->name, operprivs);
  SendMessageFile(source_p, &ConfigFileEntry.opermotd);
  
  return 1;
}
