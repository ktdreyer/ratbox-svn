/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  m_kill.c: Kills a user.
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "handlers.h"
#include "client.h"
#include "hash.h"       /* for find_client() */
#include "ircd.h"
#include "numeric.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_conf.h"
#include "send.h"
#include "whowas.h"
#include "irc_string.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

#include <string.h>

static char buf[BUFSIZE];

static void ms_kill(struct Client*, struct Client*, int, char**);
static void mo_kill(struct Client*, struct Client*, int, char**);
static void relay_kill(struct Client *, struct Client *, struct Client *,
                       const char *, const char *);

struct Message kill_msgtab = {
  "KILL", 0, 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, ms_kill, mo_kill}
};
#ifndef STATIC_MODULES

void
_modinit(void)
{
  mod_add_cmd(&kill_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&kill_msgtab);
}

const char *_version = "$Revision$";
#endif
/*
** mo_kill
**      parv[0] = sender prefix
**      parv[1] = kill victim
**      parv[2] = kill path
*/
static void mo_kill(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  struct Client*    target_p;
  const char* inpath = client_p->name;
  char*       user;
  char*       reason;

  user = parv[1];
  reason = parv[2]; /* Either defined or NULL (parc >= 2!!) */

  if (!IsOperGlobalKill(source_p))
    {
      sendto_one(source_p,":%s NOTICE %s :You need global_kill = yes;",me.name,parv[0]);
      return;
    }

  if (!BadPtr(reason))
    {
      if(strlen(reason) > (size_t) KILLLEN)
	reason[KILLLEN] = '\0';
    }
  else
    reason = "<No reason given>";

  if ((target_p = find_client(user)) == NULL)
    {
      /*
      ** If the user has recently changed nick, automatically
      ** rewrite the KILL for this new nickname--this keeps
      ** servers in synch when nick change and kill collide
      */
      if ((target_p = get_history(user, (long)KILLCHASETIMELIMIT)) == NULL)
        {
          sendto_one(source_p, form_str(ERR_NOSUCHNICK),
                     me.name, parv[0], user);
          return;
        }
      sendto_one(source_p,":%s NOTICE %s :KILL changed from %s to %s",
                 me.name, parv[0], user, target_p->name);
    }
  if (IsServer(target_p) || IsMe(target_p))
    {
      sendto_one(source_p, form_str(ERR_CANTKILLSERVER),
                 me.name, parv[0]);
      return;
    }

  if (!MyConnect(target_p) && (!IsOperGlobalKill(source_p)))
    {
      sendto_one(source_p, ":%s NOTICE %s :Nick %s isnt on your server",
                 me.name, parv[0], target_p->name);
      return;
    }

  if(MyConnect(target_p))
    sendto_one(target_p, ":%s!%s@%s KILL %s :%s", 
	       source_p->name, source_p->username, source_p->host,
	       target_p->name, reason);

  /* Do not change the format of this message.  There's no point in changing messages
   * that have been around for ever, for no reason.. */
  sendto_realops_flags(FLAGS_ALL, L_ALL,
		       "Received KILL message for %s. From %s Path: %s (%s)", 
		       target_p->name, parv[0], me.name, reason);

  ilog(L_INFO,"KILL From %s For %s Path %s (%s)",
       parv[0], target_p->name, me.name, reason);


  /*
  ** And pass on the message to other servers. Note, that if KILL
  ** was changed, the message has to be sent to all links, also
  ** back.
  ** Suicide kills are NOT passed on --SRB
  */
  if (!MyConnect(target_p))
    {
      relay_kill(client_p, source_p, target_p, inpath, reason);
      /*
      ** Set FLAGS_KILLED. This prevents exit_one_client from sending
      ** the unnecessary QUIT for this. (This flag should never be
      ** set in any other place)
      */
      target_p->flags |= FLAGS_KILLED;
    }

  ircsprintf(buf, "Killed (%s (%s))", source_p->name, reason);
  
  exit_client(client_p, target_p, source_p, buf);
}

/*
 * ms_kill
 *      parv[0] = sender prefix
 *      parv[1] = kill victim
 *      parv[2] = kill path and reason
 */
static void ms_kill(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  struct Client *target_p;
  char *user;
  char *reason;
  char *path;
  int chasing = 0;

  *buf = '\0';

  if (*parv[1] == '\0')
    {
      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "KILL");
      return;
    }

  user = parv[1];

  if(BadPtr(parv[2]))
  {
    reason = "<No reason given>";

    /* hyb6 takes the nick of the killer from the path *sigh* --fl_ */
    path = source_p->name;
  }
  else
  {
    reason = strchr(parv[2], ' ');
      
    if(reason)
    {
      *reason = '\0';
      reason++;
    }
    else
      reason = "<No reason given>";

    path = parv[2];
  }

  if ((target_p = find_client(user)) == NULL)
    {
      /*
       * If the user has recently changed nick, but only if its 
       * not an uid, automatically rewrite the KILL for this new nickname.
       * --this keeps servers in synch when nick change and kill collide
       */
      if( (*user == '.')  ||
	  (!(target_p = get_history(user, (long)KILLCHASETIMELIMIT))))
        {
          sendto_one(source_p, form_str(ERR_NOSUCHNICK),
                     me.name, parv[0], user);
          return;
        }
      sendto_one(source_p,":%s NOTICE %s :KILL changed from %s to %s",
                 me.name, parv[0], user, target_p->name);
      chasing = 1;
    }
    
  if (IsServer(target_p) || IsMe(target_p))
    {
      sendto_one(source_p, form_str(ERR_CANTKILLSERVER),
                 me.name, parv[0]);
      return;
    }

  if(MyConnect(target_p))
  {
    if(IsServer(source_p))
    {
      /* dont send clients kills from a hidden server */
      if(ConfigServerHide.hide_servers && !IsOper(target_p))
        sendto_one(target_p, ":%s KILL %s :%s",
 		   me.name, target_p->name, reason);
      else
	sendto_one(target_p, ":%s KILL %s :%s",
	           source_p->name, target_p->name, reason);
    }
    else
      sendto_one(target_p, ":%s!%s@%s KILL %s :%s",
		 source_p->name, source_p->username, source_p->host,
		 target_p->name, reason);
  }

  /* Be warned, this message must be From %s, or it confuses clients
   * so dont change it to From: or the case or anything! -- fl -- db */
  if (IsOper(source_p)) /* send it normally */
    {
      sendto_realops_flags(FLAGS_ALL, L_ALL,
			   "Received KILL message for %s. From %s Path: %s %s",
			   target_p->name, parv[0], source_p->user->server, reason);
    }
  else
    {
      sendto_realops_flags(FLAGS_SKILL, L_ALL,
			   "Received KILL message for %s. From %s %s",
			   target_p->name, parv[0], reason);
    }

  ilog(L_INFO,"KILL From %s For %s Path %s %s",
       parv[0], target_p->name, parv[0], reason);

  relay_kill(client_p, source_p, target_p, path, reason);

  /* FLAGS_KILLED prevents a quit being sent out */ 
  target_p->flags |= FLAGS_KILLED;

  /* reason comes supplied with its own ()'s */
  if(ConfigServerHide.hide_servers && IsServer(source_p))
    ircsprintf(buf, "Killed (%s %s)", me.name, reason);
  else
    ircsprintf(buf, "Killed (%s %s)", source_p->name, reason);
    
  exit_client(client_p, target_p, source_p, buf);
}

static void relay_kill(struct Client *one, struct Client *source_p,
                       struct Client *target_p,
                       const char *inpath,
		       const char *reason)
{
  dlink_node *ptr;
  struct Client *client_p;
  int introduce_killed_client;
  char* user; 
  
  /* LazyLinks:
   * Check if each lazylink knows about target_p.
   *   If it does, send the kill, introducing source_p if required.
   *   If it doesn't either:
   *     a) don't send the kill (risk ghosts)
   *     b) introduce the client (and source_p, if required)
   *        [rather redundant]
   *
   * Use a) if IsServer(source_p), but if an oper kills someone,
   * ensure we blow away any ghosts.
   *
   * -davidt
   */

  if(IsServer(source_p))
    introduce_killed_client = 0;
  else
    introduce_killed_client = 1;

  for( ptr = serv_list.head; ptr; ptr = ptr->next )
  {
    client_p = (struct Client *) ptr->data;
    
    if( !client_p || client_p == one )
      continue;

    if( !introduce_killed_client )
    {
      if( ServerInfo.hub && IsCapable(client_p, CAP_LL) )
      {
        if(((client_p->localClient->serverMask &
             target_p->lazyLinkClientExists) == 0))
        {
          /* target isn't known to lazy leaf, skip it */
          continue;
        }
      }
    }
    /* force introduction of killed client but check that
     * its not on the server we're bursting too.. */
    else if(strcmp(target_p->user->server,client_p->name))
      client_burst_if_needed(client_p, target_p);

    /* introduce source of kill */
    client_burst_if_needed(client_p, source_p);

    /* check the server supports UID */
    if (IsCapable(client_p, CAP_UID))
      user = ID(target_p);
    else
      user = target_p->name;

    if(MyClient(source_p))
      {
        sendto_one(client_p, ":%s KILL %s :%s!%s!%s!%s (%s)",
                   source_p->name, user,
                   me.name, source_p->host, source_p->username,
                   source_p->name, reason);
      }
    else
      {
        sendto_one(client_p, ":%s KILL %s :%s %s",
                   source_p->name, user,
                   inpath, reason);
      }
  }
}

