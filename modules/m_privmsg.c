/************************************************************************
 *   IRC - Internet Relay Chat, src/m_privmsg.c
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
#include "flud.h"
#include "ircd.h"
#include "numeric.h"
#include "common.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"

#include "channel.h"
#include "vchannel.h"
#include "irc_string.h"
#include "m_privmsg.h"
#include "hash.h"
#include "class.h"
#include "msg.h"

#include <string.h>

struct entity target_table[MAX_MULTI_MESSAGES];

int duplicate_ptr( void *ptr,
			  struct entity target_table[], int n);

void privmsg_channel( struct Client *cptr,
			     struct Client *sptr,
			     struct Channel *chptr,
			     char *text);

void privmsg_channel_flags( struct Client *cptr,
				   struct Client *sptr,
				   struct Channel *chptr,
				   int flags,
				   char *text);

void privmsg_client(struct Client *sptr, struct Client *acptr,
			   char *text);

void handle_opers(struct Client *cptr,
			 struct Client *sptr,
			 char *nick,
			 char *text);

struct Message privmsg_msgtab = {
  {MSG_PRIVMSG, 0, 1, MFLG_SLOW | MFLG_UNREG, 0L,
  {m_unregistered, m_privmsg, ms_privmsg, mo_privmsg}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_PRIVMSG, &privmsg_msgtab);
}

/*
** m_privmsg
**
**      parv[0] = sender prefix
**      parv[1] = receiver list
**      parv[2] = message text
**
** massive cleanup
** rev argv 6/91
**
** You ain't seen nothing yet -db
**
*/

int     m_privmsg(struct Client *cptr,
                          struct Client *sptr,
                          int parc,
                          char *parv[])
{
  int i;
  int ntargets;

  if (!IsPerson(sptr))
    return 0;

#ifndef ANTI_SPAMBOT_WARN_ONLY
  /* if its a spambot, just ignore it */
  if(sptr->join_leave_count >= MAX_JOIN_LEAVE_COUNT)
    return 0;
#endif

  /* privmsg gives different errors, so this still needs to be checked */
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NORECIPIENT), me.name, parv[0], "PRIVMSG");
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  ntargets = build_target_list(cptr,sptr,parv[1],target_table,parv[2]);

  for(i = 0; i < ntargets ; i++)
    {
      switch (target_table[i].type)
	{
	case ENTITY_CHANNEL:
	  privmsg_channel(cptr,sptr,
			  (struct Channel *)target_table[i].ptr,
			  parv[2]);
	  break;

	case ENTITY_CHANOPS_ON_CHANNEL:
	  privmsg_channel_flags(cptr,sptr,
				(struct Channel *)target_table[i].ptr,
				target_table[i].flags,parv[2]);
	  break;

	case ENTITY_CLIENT:
	  privmsg_client(sptr,(struct Client *)target_table[i].ptr,parv[2]);
	  break;
	}
    }

  if(ntargets == 0)
    sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name,
	       parv[0], parv[1]);

  return 1;
}

/*
 * build_target_list
 *
 * inputs	- pointer to given cptr (server)
 *		- pointer to given source (oper/client etc.)
 *		- pointer to list of nicks/channels
 *		- pointer to table to place results
 *		- pointer to text (only used if sptr is an oper)
 * output	- number of valid entities
 * side effects	- target_table is modified to contain a list of
 *		  pointers to channels or clients
 *		  if source client is an oper
 *		  all the classic old bizzare oper privmsg tricks
 *		  are parsed and sent as is...
 *
 * This function will be also used in m_notice.c
 */

int build_target_list(struct Client *cptr,
		      struct Client *sptr,
		      char *nicks_channels,
		      struct entity target_table[],
		      char *text)
{
  int  i = 0;
  int  type;
  char *p;
  char *nick;
  struct Channel *chptr;
  struct Client *acptr;

  for (nick = strtoken(&p, nicks_channels, ","); nick; 
       nick = strtoken(&p, (char *)NULL, ","))
    {
      /*
      ** channels are privmsg'd a lot more than other clients, moved up here
      ** plain old channel msg ?
      */

      if( IsChanPrefix(*nick) && (chptr = hash_find_channel(nick, NullChn))
	  && !duplicate_ptr(chptr, target_table, i) )
	{
	  target_table[i].ptr = (void *)chptr;
	  target_table[i++].type = ENTITY_CHANNEL;
	  
	  if( i >= MAX_MULTI_MESSAGES)
	    return(i);
	  continue;
	}

      /* @#channel or +#channel message ? */

      type = 0;
      if(*nick == '@')
	type = MODE_CHANOP;
      else if(*nick == '+')
	type = MODE_CHANOP|MODE_VOICE;

      if(type)
	{
	  /* Strip if using DALnet chanop/voice prefix. */
	  if (*(nick+1) == '@' || *(nick+1) == '+')
	    {
	      nick++;
	      *nick = '@';
	      type = MODE_CHANOP|MODE_VOICE;
	    }

	  /* suggested by Mortiis */
	  if(!*nick)        /* if its a '\0' dump it, there is no recipient */
	    {
	      sendto_one(sptr, form_str(ERR_NORECIPIENT),
			 me.name, sptr->name, "PRIVMSG");
	      continue;
	    }

	  /* At this point, nick+1 should be a channel name i.e. #foo or &foo
	   * if the channel is found, fine, if not report an error
	   */

	  if ( (chptr = hash_find_channel(nick+1, NullChn)) &&
	       !duplicate_ptr(chptr, target_table,i) )
	    {
	      target_table[i].ptr = (void *)chptr;
	      target_table[i].type = ENTITY_CHANOPS_ON_CHANNEL;
	      target_table[i++].flags = type;

	      if( i >= MAX_MULTI_MESSAGES)
		return(i);
	      continue;
	    }
	}
      /* At this point, its likely its another client */

      if ( (acptr = find_person(nick, NULL)) &&
	   !duplicate_ptr(acptr, target_table, i) &&
	   !drone_attack(sptr, acptr) )
	{
	  target_table[i].ptr = (void *)acptr;
	  target_table[i].type = ENTITY_CLIENT;
	  target_table[i++].flags = 0;
	      
	  if( i >= MAX_MULTI_MESSAGES)
	    return(i);
	  continue;
	}

      /* anything else below needs global oper privs */
      if (IsGlobalOper(sptr))
	{
	  handle_opers(cptr,sptr,nick,text);
	}
    }
  return i;
}

/*
 * duplicate_ptr
 *
 * inputs	- pointer to check
 *		- pointer to table of entities
 *		- number of valid entities so far
 * output	- YES if duplicate pointer in table, NO if not.
 *		  note, this does the canonize using pointers
 * side effects	- NONE
 */

int duplicate_ptr( void *ptr,
			  struct entity target_table[], int n)
{
  int i;

  for(i = 0; i < n; i++)
    {
      if (target_table[i].ptr == ptr )
	return YES;
    }
  return NO;
}

/*
 * privmsg_channel
 *
 * inputs	- pointer to cptr
 *		- pointer to sptr
 *		- pointer to channel
 * output	- YES if duplicate pointer in table, NO if not.
 *		  note, this is canonilization
 * side effects	- message given channel
 */
void privmsg_channel( struct Client *cptr,
			     struct Client *sptr,
			     struct Channel *chptr,
			     char *text)
{
  struct Channel *vchan;
  char *channel_name=NULL;

  if (HasVchans(chptr))
    {
      if( (vchan = map_vchan(chptr,sptr)) )
	{
	  channel_name = chptr->chname;
	  chptr = vchan;
	}
    }
  else
    channel_name = chptr->chname;

  if(MyClient(sptr) && sptr->user)
    sptr->user->last = CurrentTime;

  if(check_for_ctcp(text))
    check_for_flud(sptr, NULL, chptr, 1);

  if (can_send(chptr, sptr) == 0)
    sendto_channel_butone(cptr, sptr, chptr,
			  ":%s %s %s :%s",
			  sptr->name, "PRIVMSG", channel_name, text);
  else
    sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
	       me.name, sptr->name, channel_name);
}

/*
 * privmsg_channel_flags
 *
 * inputs	- pointer to cptr
 *		- pointer to sptr
 *		- pointer to channel
 *		- pointer to text to send
 * output	- NONE
 * side effects	- message given channel either chanop or voice
 */
void privmsg_channel_flags( struct Client *cptr,
				   struct Client *sptr,
				   struct Channel *chptr,
				   int flags,
				   char *text)
{
  struct Channel *vchan;
  char *channel_name=NULL;

  if (HasVchans(chptr))
    {
      if( (vchan = map_vchan(chptr,sptr)) )
	{
	  channel_name = chptr->chname;
	  chptr = vchan;
	}
    }
  else
    channel_name = chptr->chname;

  if(MyClient(sptr) && sptr->user)
    sptr->user->last = CurrentTime;

  if(check_for_ctcp(text))
    check_for_flud(sptr, NULL, chptr, 1);

  if (can_send(chptr, sptr) == 0)
    sendto_channel_type(cptr,
			sptr,
			chptr,
			flags,
			channel_name,
			"PRIVMSG",
			text);
  else
    sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
	       me.name, sptr->name, channel_name);
}

/*
 * privmsg_client
 *
 * inputs	- pointer to sptr
 *		- pointer to acptr (struct Client *)
 *		- pointer to text
 * output	- NONE
 * side effects	- message given channel either chanop or voice
 */
void privmsg_client(struct Client *sptr, struct Client *acptr,
			   char *text)
{
  /* reset idle time for message only if its not to self */
  if (sptr != acptr)
    {
      if(sptr->user)
	sptr->user->last = CurrentTime;
    }

  /* reset idle time for message only if target exists */
  if(MyClient(sptr) && sptr->user)
    sptr->user->last = CurrentTime;

  if(check_for_ctcp(text))
    check_for_flud(sptr, acptr, NULL, 1);

  if (MyConnect(sptr) &&
      acptr->user && acptr->user->away)
    sendto_one(sptr, form_str(RPL_AWAY), me.name,
	       sptr->name, acptr->name,
	       acptr->user->away);

  sendto_prefix_one(acptr, sptr, ":%s %s %s :%s",
		    sptr->name, "PRIVMSG", acptr->name, text);


  return;
}
      
int     mo_privmsg(struct Client *cptr,
                          struct Client *sptr,
                          int parc,
                          char *parv[])
{
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NORECIPIENT),
                 me.name, parv[0], "PRIVMSG");
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  m_privmsg(cptr,sptr,parc,parv);
  return 0;
}

int     ms_privmsg(struct Client *cptr,
                          struct Client *sptr,
                          int parc,
                          char *parv[])
{
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NORECIPIENT),
                 me.name, parv[0], "PRIVMSG");
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  m_privmsg(cptr,sptr,parc,parv);
  return 0;
}

/*
 * drone_attack
 * inputs	- pointer to source Client 
 *		- pointer to target Client
 * output	- 1 if target is under drone attack
 * side effects	- check for drone attack on target acptr
 */

int drone_attack(struct Client *sptr,struct Client *acptr)
{
  if(MyConnect(acptr) && IsClient(sptr) &&
     GlobalSetOptions.dronetime)
    {
      if((acptr->first_received_message_time+GlobalSetOptions.dronetime)
	 < CurrentTime)
	{
	  acptr->received_number_of_privmsgs=1;
	  acptr->first_received_message_time = CurrentTime;
	  acptr->drone_noticed = 0;
	}
      else
	{
	  if(acptr->received_number_of_privmsgs > 
	     GlobalSetOptions.dronecount)
	    {
	      if(acptr->drone_noticed == 0) /* tiny FSM */
		{
		  sendto_ops_flags(FLAGS_BOTS,
		   "Possible Drone Flooder %s [%s@%s] on %s target: %s",
				   sptr->name, sptr->username,
				   sptr->host,
				   sptr->user->server, acptr->name);
		  acptr->drone_noticed = 1;
		}
	      /* heuristic here, if target has been getting a lot
	       * of privmsgs from clients, and sendq is above halfway up
	       * its allowed sendq, then throw away the privmsg, otherwise
	       * let it through. This adds some protection, yet doesn't
	       * DoS the client.
	       * -Dianora
	       */
	      if(DBufLength(&acptr->sendQ) > (get_sendq(acptr)/2L))
		{
		  if(acptr->drone_noticed == 1) /* tiny FSM */
		    {
		      sendto_ops_flags(FLAGS_BOTS,
		       "anti_drone_flood SendQ protection activated for %s",
				       acptr->name);

		      sendto_one(acptr,     
 ":%s NOTICE %s :*** Notice -- Server drone flood protection activated for %s",
				 me.name, acptr->name, acptr->name);
		      acptr->drone_noticed = 2;
		    }
		}

	      if(DBufLength(&acptr->sendQ) <= (get_sendq(acptr)/4L))
		{
		  if(acptr->drone_noticed == 2)
		    {
		      sendto_one(acptr,     
				 ":%s NOTICE %s :*** Notice -- Server drone flood protection de-activated for %s",
				 me.name, acptr->name, acptr->name);
		      acptr->drone_noticed = 1;
		    }
		}
	      if(acptr->drone_noticed > 1)
		return 1;
	    }
	  else
	    acptr->received_number_of_privmsgs++;
	}
    }

  return 0;
}


/*
 * handle_opers
 *
 * inputs	- server pointer
 *		- client pointer
 *		- nick stuff to grok for opers
 *		- text to send if grok
 * output	- none
 * side effects	- all the classic icky oper type messages are parsed
 *		  i.e. privmsg #some.host 
 *		  for the moment, oper NOTICE to this icky stuff
 *		  is translated to privmsg. deal for now.
 */
void handle_opers(struct Client *cptr,
			       struct Client *sptr,
			       char *nick,
			       char *text)
{
  struct Client *acptr;
  char *host;
  char *server;
  char *s;
  int count;

  /* Everything below here should be reserved for opers 
   * as pointed out by Mortiis, user%host.name@server.name 
   * syntax could be used to flood without FLUD protection
   * its also a delightful way for non-opers to find users who
   * have changed nicks -Dianora
   *
   * Grrr it was pointed out to me that x@service is valid
   * for non-opers too, and wouldn't allow for flooding/stalking
   * -Dianora
   */

  /*
  ** the following two cases allow masks in NOTICEs
  ** (for OPERs only)
  **
  ** Armin, 8Jun90 (gruner@informatik.tu-muenchen.de)
  */
  if ((*nick == '$' || *nick == '#'))
    {
      if (!(s = (char *)strrchr(nick, '.')))
	{
	  sendto_one(sptr, form_str(ERR_NOTOPLEVEL),
		     me.name, sptr->name, nick);
	  return;
	}
      while (*++s)
	if (*s == '.' || *s == '*' || *s == '?')
	  break;
      if (*s == '*' || *s == '?')
	{
	  sendto_one(sptr, form_str(ERR_WILDTOPLEVEL),
		     me.name, sptr->name, nick);
	  return;
	}
      sendto_match_butone(IsServer(cptr) ? cptr : NULL, 
			  sptr, nick + 1,
			  (*nick == '#') ? MATCH_HOST :
			  MATCH_SERVER,
			  ":%s %s %s :%s", sptr->name,
			  "PRIVMSG", nick, text);
      return;
    }
  /*
  ** user[%host]@server addressed?
  */
  if ((server = (char *)strchr(nick, '@')) &&
      (acptr = find_server(server + 1)))
    {
      count = 0;
      
      /*
      ** Not destined for a user on me :-(
      */
      if (!IsMe(acptr))
	{
	  sendto_one(acptr,":%s %s %s :%s", sptr->name,
		     "PRIVMSG", nick, text);
	  return;
	}

      *server = '\0';

      /* special case opers@server */
      if(!irccmp(nick,"opers"))
	{
	  sendto_realops("To opers: From %s: %s",sptr->name,sptr->name);
	  return;
	}
        
      if ((host = (char *)strchr(nick, '%')))
	*host++ = '\0';
      
      /*
      ** Look for users which match the destination host
      ** (no host == wildcard) and if one and one only is
      ** found connected to me, deliver message!
      */
      acptr = find_userhost(nick, host, NULL, &count);
      if (server)
	*server = '@';
      if (host)
	*--host = '%';
      if (acptr)
	{
	  if (count == 1)
	    sendto_prefix_one(acptr, sptr,
			      ":%s %s %s :%s",
			      sptr->name, "PRIVMSG",
			      nick, text);
	  else 
	    sendto_one(sptr,
		       form_str(ERR_TOOMANYTARGETS),
		       me.name, sptr->name, nick);
	}
    }
}






