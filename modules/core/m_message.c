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
#include "ircd.h"
#include "numeric.h"
#include "common.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"

#include "channel.h"
#include "vchannel.h"
#include "irc_string.h"
#include "hash.h"
#include "class.h"
#include "msg.h"

#include <string.h>

struct entity {
  void *ptr;
  int type;
  int flags;
};

int build_target_list(int p_or_n, char *command,
		      struct Client *cptr, struct Client *sptr,
		      char *nicks_channels, struct entity target_table[],
		      char *text);

int flood_attack_client(struct Client *sptr, struct Client *acptr);
int flood_attack_channel(struct Client *sptr, struct Channel *chptr,
			 char *chname);

#define MAX_TARGETS 20

#define ENTITY_NONE    0
#define ENTITY_CHANNEL 1
#define ENTITY_CHANOPS_ON_CHANNEL 2
#define ENTITY_CLIENT  3

struct entity target_table[MAX_TARGETS];

int duplicate_ptr( void *ptr, struct entity target_table[], int n);

void msg_channel( int p_or_n, char *command,
		  struct Client *cptr,
		  struct Client *sptr,
		  struct Channel *chptr,
		  char *text);

void msg_channel_flags( int p_or_n, char *command,
			struct Client *cptr,
			struct Client *sptr,
			struct Channel *chptr,
			int flags,
			char *text);

void msg_client(int p_or_n, char *command,
		struct Client *sptr, struct Client *acptr,
		char *text);

void handle_opers(int p_or_n, char *command,
		  struct Client *cptr,
		  struct Client *sptr,
		  char *nick,
		  char *text);

struct Message privmsg_msgtab = {
  MSG_PRIVMSG, 0, 1, MFLG_SLOW | MFLG_UNREG, 0L,
  {m_unregistered, m_privmsg, m_privmsg, m_privmsg}
};

struct Message notice_msgtab = {
  MSG_NOTICE, 0, 1, MFLG_SLOW | MFLG_UNREG, 0L,
  {m_unregistered, m_notice, m_notice, m_notice}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_PRIVMSG, &privmsg_msgtab);
  mod_add_cmd(MSG_NOTICE, &notice_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_PRIVMSG);
  mod_del_cmd(MSG_NOTICE);
}

char *_version = "20001122";

/*
** m_privmsg
**
** massive cleanup
** rev argv 6/91
**
**   Another massive cleanup Nov, 2000
** (I don't think there is a single line left from 6/91. Maybe.)
** m_privmsg and m_notice do basically the same thing.
** in the original 2.8.2 code base, they were the same function
** "m_message.c." When we did the great cleanup in conjuncton with bleep
** of ircu fame, we split m_privmsg.c and m_notice.c.
** I don't see the point of that now. Its harder to maintain, its
** easier to introduce bugs into one version and not the other etc.
** Really, the penalty of an extra function call isn't that big a deal folks.
** -db Nov 13, 2000
**
*/

#define PRIVMSG 0
#define NOTICE  1

int     m_privmsg(struct Client *cptr,
		  struct Client *sptr,
		  int parc,
		  char *parv[])
{
  return(m_message(PRIVMSG,"PRIVMSG",cptr,sptr,parc,parv));
}

int     m_notice(struct Client *cptr,
		 struct Client *sptr,
		 int parc,
		 char *parv[])
{
  return(m_message(NOTICE,"NOTICE",cptr,sptr,parc,parv));
}

/*
 * inputs	- flag privmsg or notice
 * 		- pointer to command "PRIVMSG" or "NOTICE"
 *		- pointer to cptr
 *		- pointer to sptr
 *		- pointer to channel
 */

int     m_message(int p_or_n,
		  char *command,
		  struct Client *cptr,
		  struct Client *sptr,
		  int parc,
		  char *parv[])
{
  int i;
  int ntargets;

  if (!IsPerson(sptr))
    return 0;

  if (parc < 2 || *parv[1] == '\0')
    {
      if(p_or_n != NOTICE)
	sendto_one(sptr, form_str(ERR_NORECIPIENT), me.name, sptr->name,
		   command );
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      if(p_or_n != NOTICE)
	sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, sptr->name);
      return -1;
    }

  ntargets = build_target_list(p_or_n,command,
			       cptr,sptr,parv[1],target_table,parv[2]);

  for(i = 0; i < ntargets ; i++)
    {
      switch (target_table[i].type)
	{
	case ENTITY_CHANNEL:
	  msg_channel(p_or_n,command,
			  cptr,sptr,
			  (struct Channel *)target_table[i].ptr,
			  parv[2]);
	  break;

	case ENTITY_CHANOPS_ON_CHANNEL:
	  msg_channel_flags(p_or_n,command,
			    cptr,sptr,
			    (struct Channel *)target_table[i].ptr,
			    target_table[i].flags,parv[2]);
	  break;

	case ENTITY_CLIENT:
	  msg_client(p_or_n,command,
		     sptr,(struct Client *)target_table[i].ptr,parv[2]);
	  break;
	}
    }

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
 *		  are parsed and sent as is, if prefixed with $
 *		  to disambiguate.
 *
 */

int build_target_list(int p_or_n,
		      char *command,
		      struct Client *cptr,
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

      if( IsChanPrefix(*nick) )
	{
	  if( (chptr = hash_find_channel(nick, NullChn)) )
	    {
	      if( !duplicate_ptr(chptr, target_table, i) ) 
		{
		  target_table[i].ptr = (void *)chptr;
		  target_table[i++].type = ENTITY_CHANNEL;
	  
		  if( i >= MAX_TARGETS)
		    return(i);
		  continue;
		}
	    }
	  else
	    {
	      if(p_or_n != NOTICE)
		sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name,
			   sptr->name, nick );
	      continue;
	    }
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
	  if ((*(nick+1) == '@' && (type & MODE_VOICE)) ||
              *(nick+1) == '+' && !(type & MODE_VOICE))
            {
              nick++;
              *nick = '+';
              type = MODE_CHANOP|MODE_VOICE;
            }

	  /* suggested by Mortiis */
	  if(!*(nick+1))   /* if its a '\0' dump it, there is no recipient */
	    {
	      sendto_one(sptr, form_str(ERR_NORECIPIENT),
			 me.name, sptr->name, command);
	      continue;
	    }

	  /* At this point, nick+1 should be a channel name i.e. #foo or &foo
	   * if the channel is found, fine, if not report an error
	   */

	  if ( (chptr = hash_find_channel(nick+1, NullChn)) )
	    {
	      if( !duplicate_ptr(chptr, target_table,i) )
		{
		  target_table[i].ptr = (void *)chptr;
		  target_table[i].type = ENTITY_CHANOPS_ON_CHANNEL;
		  target_table[i++].flags = type;

		  if( i >= MAX_TARGETS)
		    return(i);
		  continue;
		}
	    }
	  else
	    {
	      if(p_or_n != NOTICE)
		sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name,
			   sptr->name, nick );
	      continue;
	    }
	}

      if (IsGlobalOper(sptr) && (*nick == '$'))
	{
	  handle_opers(p_or_n, command, cptr,sptr,nick+1,text);
	  continue;
	}

      /* At this point, its likely its another client */

      if ( (acptr = find_person(nick, NULL)) )
	{
	  if( !duplicate_ptr(acptr, target_table, i) )
	    {
	      target_table[i].ptr = (void *)acptr;
	      target_table[i].type = ENTITY_CLIENT;
	      target_table[i++].flags = 0;
	      
	      if( i >= MAX_TARGETS)
		return(i);
	      continue;
	    }
	}
      else
	{
	  if(p_or_n != NOTICE)
	    sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name,
		       sptr->name, nick );
	  continue;
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

int duplicate_ptr( void *ptr, struct entity target_table[], int n)
{
  int i;

  for(i = 0; i < n; i++)
    {
      if (target_table[i].ptr == ptr)
	return YES;
    }
  return NO;
}

/*
 * msg_channel
 *
 * inputs	- flag privmsg or notice
 * 		- pointer to command "PRIVMSG" or "NOTICE"
 *		- pointer to cptr
 *		- pointer to sptr
 *		- pointer to channel
 * output	- NONE
 * side effects	- message given channel
 */
void msg_channel( int p_or_n, char *command,
		  struct Client *cptr,
		  struct Client *sptr,
		  struct Channel *chptr,
		  char *text)
{
  struct Channel *vchan;
  char *channel_name=NULL;
  int result;

  channel_name = chptr->chname;

  if ( (HasVchans(chptr)) && (vchan = map_vchan(chptr,sptr)) )
    {
      chptr = vchan;
    }

  if(MyClient(sptr))
    {
      if(sptr->user)
	sptr->user->last = CurrentTime;
    }

  /* chanops and voiced can flood their own channel with impunity */
  if( (result = can_send(chptr,sptr)) )
    {
      if (result == CAN_SEND_OPV)
	{
	  sendto_channel_butone(cptr, sptr, chptr,
				":%s %s %s :%s",
				sptr->name, command, channel_name, text);
	}
      else
	{
	  if(!flood_attack_channel(sptr, chptr,channel_name))
	    sendto_channel_butone(cptr, sptr, chptr,
				  ":%s %s %s :%s",
				  sptr->name, command, channel_name, text);
	}
    }
  else
    {
      if (p_or_n != NOTICE)
        sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
                   me.name, sptr->name, channel_name);
    }
}

/*
 * msg_channel_flags
 *
 * inputs	- flag 0 if PRIVMSG 1 if NOTICE. RFC 
 *		  say NOTICE must not auto reply
 *		- pointer to command, "PRIVMSG" or "NOTICE"
 *		- pointer to cptr
 *		- pointer to sptr
 *		- pointer to channel
 *		- pointer to text to send
 * output	- NONE
 * side effects	- message given channel either chanop or voice
 */
void msg_channel_flags( int p_or_n, char *command,
			struct Client *cptr,
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

  if(MyClient(sptr))
    {
      if(sptr->user)
	sptr->user->last = CurrentTime;
    }

  sendto_channel_type(cptr,
		      sptr,
		      &chptr->chanops,
		      '@',
		      channel_name,
		      command,
		      text);
}

/*
 * msg_client
 *
 * inputs	- flag 0 if PRIVMSG 1 if NOTICE. RFC 
 *		  say NOTICE must not auto reply
 *		- pointer to command, "PRIVMSG" or "NOTICE"
 * 		- pointer to sptr source (struct Client *)
 *		- pointer to acptr target (struct Client *)
 *		- pointer to text
 * output	- NONE
 * side effects	- message given channel either chanop or voice
 */
void msg_client(int n_or_p, char *command,
		struct Client *sptr, struct Client *acptr,
		char *text)
{
  if(MyClient(sptr))
    {
      /* reset idle time for message only if its not to self */
      if((sptr != acptr) && sptr->user)
	sptr->user->last = CurrentTime;
    }

  if (MyConnect(sptr) &&
      acptr->user && acptr->user->away)
    sendto_one(sptr, form_str(RPL_AWAY), me.name,
	       sptr->name, acptr->name,
	       acptr->user->away);

  if(MyClient(acptr))
    {
      if(IsSetCallerId(acptr))
	{
	  /* Here is the anti-flood bot/spambot bloat^H^H^H^H^Hcode -db */
	  if(accept_message(sptr,acptr))
	    {
	      sendto_prefix_one(acptr, sptr, ":%s %s %s :%s",
				sptr->name, command, acptr->name, text);
	    }
	  else
	    {
	      /* check for accept, flag recipient incoming message */
	      sendto_prefix_one(sptr, acptr,
		":%s NOTICE %s :*** I'm in +g mode (server side ignore).",
			    acptr->name, sptr->name);
	      /* XXX hard coded 60 ick fix -db */
	      if((acptr->localClient->last_caller_id_time + 60) < CurrentTime)
		{
		  sendto_prefix_one(sptr, acptr,
		    ":%s NOTICE %s :*** I've been informed you messaged me.",
				    acptr->name, sptr->name);

		  sendto_prefix_one(acptr, sptr,
      ":%s NOTICE %s :*** Client %s [%s@%s] is messaging you and you are +g",
				    me.name, acptr->name,
				    sptr->name, sptr->username,
				    sptr->host );

		  acptr->localClient->last_caller_id_time = CurrentTime;
		  
		}
	      /* Only so opers can watch for floods */
	      (void)flood_attack_client(sptr,acptr);
	    }
	}
      else
	{
	  if(!flood_attack_client(sptr,acptr))
	    sendto_prefix_one(acptr, sptr, ":%s %s %s :%s",
			      sptr->name, command, acptr->name, text);
	}
    }
  else
    if(!flood_attack_client(sptr,acptr))
      sendto_prefix_one(acptr, sptr, ":%s %s %s :%s",
			sptr->name, command, acptr->name, text);
  return;
}
      
/*
 * flood_attack_client
 * inputs	- pointer to source Client 
 *		- pointer to target Client
 * output	- 1 if target is under flood attack
 * side effects	- check for flood attack on target acptr
 */
int flood_attack_client(struct Client *sptr,struct Client *acptr)
{
  int delta;

  if(GlobalSetOptions.floodcount && MyConnect(acptr) && IsClient(sptr))
    {
      if((acptr->localClient->first_received_message_time+1)
	 < CurrentTime)
	{
	  delta = CurrentTime - acptr->localClient->first_received_message_time;
	  acptr->localClient->received_number_of_privmsgs -= delta;
	  acptr->localClient->first_received_message_time = CurrentTime;
	  if(acptr->localClient->received_number_of_privmsgs <= 0)
	    {
	      acptr->localClient->received_number_of_privmsgs = 0;
	      acptr->localClient->flood_noticed = 0;
	    }
	}

      if((acptr->localClient->received_number_of_privmsgs > 
	  GlobalSetOptions.floodcount) || acptr->localClient->flood_noticed)
	{
	  if(acptr->localClient->flood_noticed == 0)
	    {
	      sendto_realops_flags(FLAGS_BOTS,
				   "Possible Flooder %s [%s@%s] on %s target: %s",
				   sptr->name, sptr->username,
				   sptr->host,
				   sptr->user->server, acptr->name);
	      acptr->localClient->flood_noticed = 1;
	      /* add a bit of penalty */
	      acptr->localClient->received_number_of_privmsgs += 2;
	    }
	  if(MyClient(sptr))
	    sendto_one(sptr, ":%s NOTICE %s :*** Message to %s throttled due to flooding",
		       me.name, sptr->name, acptr->name);
	  return 1;
	}
      else
	acptr->localClient->received_number_of_privmsgs++;
    }

  return 0;
}

/*
 * flood_attack_channel
 * inputs	- pointer to source Client 
 *		- pointer to target channel
 * output	- 1 if target is under flood attack
 * side effects	- check for flood attack on target chptr
 */
int flood_attack_channel(struct Client *sptr,struct Channel *chptr,
			 char *chname)
{
  int delta;

  if(GlobalSetOptions.floodcount)
    {
      if((chptr->first_received_message_time+1) < CurrentTime)
	{
	  delta = CurrentTime - chptr->first_received_message_time;
	  chptr->received_number_of_privmsgs -= delta;
	  chptr->first_received_message_time = CurrentTime;
	  if(chptr->received_number_of_privmsgs <= 0)
	    {
	      chptr->received_number_of_privmsgs=0;
	      chptr->flood_noticed = 0;
	    }
	}

      if((chptr->received_number_of_privmsgs > GlobalSetOptions.floodcount)
	 || chptr->flood_noticed)
	{
	  if(chptr->flood_noticed == 0)
	    {
	      sendto_realops_flags(FLAGS_BOTS,
				   "Possible Flooder %s [%s@%s] on %s target: %s",
				   sptr->name, sptr->username,
				   sptr->host,
				   sptr->user->server, chptr->chname);
	      chptr->flood_noticed = 1;

	      /* Add a bit of penalty */
	      chptr->received_number_of_privmsgs += 2;
	    }
	  if(MyClient(sptr))
	    sendto_one(sptr, ":%s NOTICE %s :*** Message to %s throttled due to flooding",
		       me.name, sptr->name, chname);
	  return 1;
	}
      else
	chptr->received_number_of_privmsgs++;
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
 * side effects	- all the traditional oper type messages are parsed here.
 *		  i.e. "/msg #some.host."
 *		  However, syntax has been changed.
 *		  previous syntax "/msg #some.host.mask"
 *		  now becomes     "/msg $#some.host.mask"
 *		  previous syntax "/msg $some.server.mask"
 *		  now becomes	  "/msg $$some.server.mask"
 *		  This disambiguates the syntax.
 */
void handle_opers(int p_or_n,
		  char *command,
		  struct Client *cptr,
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
   * have changed nicks
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






