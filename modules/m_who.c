/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_who.c
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
#include "common.h"   /* bleah */
#include "handlers.h"
#include "client.h"
#include "channel.h"
#include "vchannel.h"
#include "hash.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "list.h"
#include "irc_string.h"
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

static void m_who(struct Client*, struct Client*, int, char**);
static void ms_who(struct Client*, struct Client*, int, char**);

struct Message who_msgtab = {
  "WHO", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_who, ms_who, m_who}
};

void
_modinit(void)
{
  mod_add_cmd(&who_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&who_msgtab);
}

static void do_who_on_channel(struct Client *server_p,
			      struct Channel *chptr, char *real_name,
			      int server_oper, int member);

static void do_who_list(struct Client *server_p, struct Channel *chptr,
                        dlink_list *list, char *chname, char *op_flags);

static void who_global(struct Client *server_p, char *mask, int server_oper);

static void do_who(struct Client *server_p,
                   struct Client *aclient_p,
                   char *chname,
                   char *op_flags);

char *_version = "20010210";

/*
** m_who
**      parv[0] = sender prefix
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag, only 'o' for now.
*/
static void m_who(struct Client *client_p,
                 struct Client *server_p,
                 int parc,
                 char *parv[])
{
  struct Client *aclient_p;
  char  *mask = parc > 1 ? parv[1] : NULL;
  dlink_node *lp;
  struct Channel *chptr=NULL;
  struct Channel *vchan;
  struct Channel *mychannel = NULL;
  char  *chanop_flag;
  char  *halfop_flag;
  char  *voiced_flag;
  int   server_oper = parc > 2 ? (*parv[2] == 'o' ): 0; /* Show OPERS only */
  int   member;

  /*
  **  Following code is some ugly hacking to preserve the
  **  functions of the old implementation. (Also, people
  **  will complain when they try to use masks like "12tes*"
  **  and get people on channel 12 ;) --msa
  */

  /* You people code like its supposed to be hard to read *why?* -db */

  /* See if mask is there, collapse it or return if not there */

  if (mask != (char *)NULL)
    {
      (void)collapse(mask);

      if (*mask == '\0')
	{
	  sendto_one(server_p, form_str(RPL_ENDOFWHO), me.name, parv[0], "*" );
	  return;
	}
    }
  else
    {
      who_global(server_p, mask, server_oper);
      sendto_one(server_p, form_str(RPL_ENDOFWHO), me.name, parv[0], "*" );
      return;
    }

  /* mask isn't NULL at this point. repeat after me... -db */

  /* '/who *' */

  if ((*(mask+1) == (char) 0) && (*mask == '*'))
    {
      if (server_p->user)
	if ((lp = server_p->user->channel.head))
	  mychannel = lp->data;

      if (!mychannel)
        {
          sendto_one(server_p, form_str(RPL_ENDOFWHO), me.name, parv[0], "*");
          return;
        }

      if (HasVchans(mychannel))
	{
	  vchan = map_vchan(mychannel,server_p);
	  if(vchan != 0) 
	    do_who_on_channel(server_p,vchan,"*",NO,YES);
	  else
	    do_who_on_channel(server_p,mychannel,"*",NO,YES);
	}
      else
	do_who_on_channel(server_p, mychannel, "*", NO, YES);
      sendto_one(server_p, form_str(RPL_ENDOFWHO), me.name, parv[0], "*");
      return;
    }

  /* '/who #some_channel' */

  if (IsChannelName(mask))
    {
      /*
       * List all users on a given channel
       */
      chptr = hash_find_channel(mask, NULL);
      if (chptr)
	{
	  if (HasVchans(chptr))
	    {
	      vchan = map_vchan(chptr,server_p);

	      /* If vchan not 0, that makes them a member automatically */
	      if ( vchan != 0 )
		do_who_on_channel(server_p, vchan, chptr->chname, NO, YES);
	      else
		{
		  if ( IsMember(server_p, chptr) )
		    do_who_on_channel(server_p, chptr, chptr->chname, NO, YES);
		  else if(!SecretChannel(chptr))
		    do_who_on_channel(server_p, chptr, chptr->chname, NO, NO);
		}
	    }
	  else
	    {
	      if ( IsMember(server_p, chptr) )
		do_who_on_channel(server_p, chptr, chptr->chname, NO, YES);
	      else if(!SecretChannel(chptr))
		do_who_on_channel(server_p, chptr, chptr->chname, NO, NO);
	    }
	}
      sendto_one(server_p, form_str(RPL_ENDOFWHO), me.name, parv[0], mask );
      return;
    }

  /* '/who nick' */

  if (((aclient_p = find_client(mask, NULL)) != NULL) &&
      IsPerson(aclient_p) && (!server_oper || IsOper(aclient_p)))
    {
      struct Channel *bchan;
      char *chname=NULL;
      int isinvis = 0;

      if(IsServer(client_p))
	client_burst_if_needed(client_p,aclient_p);

      isinvis = IsInvisible(aclient_p);
      for (lp = aclient_p->user->channel.head; lp; lp = lp->next)
	{
	  chptr = lp->data;
	  chname = chptr->chname;

          member = IsMember(server_p, chptr);
          if (isinvis && !member)
            continue;
          if (member || (!isinvis && PubChannel(chptr)))
            {
              break;
            }
	}

      if (chptr != NULL)
	{
	  if (IsVchan(chptr))
	    {
	      bchan = find_bchan (chptr);
	      if (bchan != NULL)
		chname = bchan->chname;
	    }

	  /* XXX globalize this inside m_who.c ? */
	  /* jdc -- Check is_any_op() for +o > +h > +v priorities */
	  if( (chptr->mode.mode & MODE_HIDEOPS) &&
	      (!is_any_op(chptr,server_p)) )
	    {
	      chanop_flag = "";
	      halfop_flag = "";
	      voiced_flag = "";
	    }
	  else
	    {
	      chanop_flag = "@";
	      halfop_flag = "%";
	      voiced_flag = "+";
	    }

	  if (is_chan_op(chptr,aclient_p))
	    do_who(server_p, aclient_p, chname, chanop_flag);
	  else if(is_half_op(chptr,aclient_p))
	    do_who(server_p, aclient_p, chname, halfop_flag);
	  else if(is_voiced(chptr,aclient_p))
	    do_who(server_p, aclient_p, chname, voiced_flag);
	  else
	    do_who(server_p, aclient_p, chname, "");
	}
      else
	{
	  if (!isinvis)
	    do_who(server_p, aclient_p, NULL, "");
	}

      sendto_one(server_p, form_str(RPL_ENDOFWHO), me.name, parv[0], mask );
      return;
    }

  /* Wasn't a nick, wasn't a channel, wasn't a '*' so ... */
  who_global(server_p, mask, server_oper);
  sendto_one(server_p, form_str(RPL_ENDOFWHO), me.name, parv[0], mask );
}

/*
 * who_global
 *
 * inputs	- pointer to client requesting who
 *		- char * mask to match
 *		- int if oper on a server or not
 * output	- NONE
 * side effects - do a global scan of all clients looking for match
 *		  this is slightly expensive on EFnet ...
 */

static void who_global(struct Client *server_p,char *mask, int server_oper)
{
  struct Channel *chptr=NULL;
  struct Channel *bchan;
  struct Client *aclient_p;
  dlink_node  *lp;
  char  *chname=NULL;
  int   showperson;
  int   member;
  int   isinvis;
  int   maxmatches = 500;
  char  *chanop_flag;
  char  *halfop_flag;
  char  *voiced_flag;

  for (aclient_p = GlobalClientList; aclient_p; aclient_p = aclient_p->next)
    {
      if (!IsPerson(aclient_p))
        continue;
      if (server_oper && !IsOper(aclient_p))
        continue;
      
      showperson = NO;
      /*
       * Show user if they are on the same channel, or not
       * invisible and on a non secret channel (if any).
       * Do this before brute force match on all relevant fields
       * since these are less cpu intensive (I hope :-) and should
       * provide better/more shortcuts - avalon
       */
      isinvis = IsInvisible(aclient_p);
      for (lp = aclient_p->user->channel.head; lp; lp = lp->next)
        {
          chptr = lp->data;
	  chname = chptr->chname;
          member = IsMember(server_p, chptr);
          if (isinvis && !member)
            continue;
          if (member || (!isinvis && PubChannel(chptr)))
            {
              showperson = YES;
              break;
            }
          if (HiddenChannel(chptr) && !SecretChannel(chptr) &&
              !isinvis)
            showperson = YES;
        }

      if ((aclient_p->user->channel.head == NULL) && !isinvis)
	showperson = YES;

      if (showperson &&
	  (!mask ||
	   match(mask, aclient_p->name) ||
	   match(mask, aclient_p->username) ||
	   match(mask, aclient_p->host) ||
	   match(mask, aclient_p->user->server) ||
	   match(mask, aclient_p->info)))
	{
	  if (chptr != NULL)
	    {
	      if (IsVchan(chptr))
		{
		  bchan = find_bchan (chptr);
		  if (bchan != NULL)
		    chname = bchan->chname;
		}

	      /* jdc -- Check is_any_op() for +o > +h > +v priorities */
	      if( (chptr->mode.mode & MODE_HIDEOPS) &&
	          (!is_any_op(chptr,server_p)) )
		{
		  chanop_flag = "";
		  halfop_flag = "";
		  voiced_flag = "";
		}
	      else
		{
		  chanop_flag = "@";
		  halfop_flag = "%";
		  voiced_flag = "+";
		}

	      if (is_chan_op(chptr,aclient_p))
		do_who(server_p, aclient_p, chname, chanop_flag);
	      else if(is_half_op(chptr,aclient_p))
		do_who(server_p, aclient_p, chname, halfop_flag);
	      else if(is_voiced(chptr,aclient_p))
		do_who(server_p, aclient_p, chname, voiced_flag);
	      else 
		do_who(server_p, aclient_p, chname, "");
	    }
	  else
	    do_who(server_p, aclient_p, NULL, "");

	  if (maxmatches > 0)
	    {
	      --maxmatches;
	      if( maxmatches == 0 )
		return;
	    }
	}
    }
}


/*
 * do_who_on_channel
 *
 * inputs	- pointer to client requesting who
 *		- pointer to channel to do who on
 *		- The "real name" of this channel
 *		- int if server_p is a server oper or not
 *		- int if client is member or not
 * output	- NONE
 * side effects - do a who on given channel
 */

static void do_who_on_channel(struct Client *server_p,
			      struct Channel *chptr,
			      char *chname,
			      int server_oper, int member)
{
  char *chanop_flag;
  char *halfop_flag;
  char *voiced_flag;


  /* jdc -- Check is_any_op() for +o > +h > +v priorities */
  if( (chptr->mode.mode & MODE_HIDEOPS) &&
      (!is_any_op(chptr,server_p)) )
    {
      chanop_flag = "";
      halfop_flag = "";
      voiced_flag = "";
    }
  else
    {
      chanop_flag = "@";
      halfop_flag = "%";
      voiced_flag = "+";
    }

  do_who_list(server_p, chptr, &chptr->chanops, chname, chanop_flag);
  do_who_list(server_p, chptr, &chptr->halfops, chname, halfop_flag);
  do_who_list(server_p, chptr, &chptr->voiced,  chname, voiced_flag);
  do_who_list(server_p, chptr, &chptr->peons,   chname, "");
}

static void do_who_list(struct Client *server_p, struct Channel *chptr,
                        dlink_list *list, char *chname, char *op_flags)
{
  dlink_node *ptr;
  struct Client *aclient_p;

  for(ptr = list->head; ptr; ptr = ptr->next)
    {
      aclient_p = ptr->data;
      do_who(server_p, aclient_p, chname, op_flags);
    }
}

/*
 * do_who
 *
 * inputs	- pointer to client requesting who
 *		- pointer to client to do who on
 *		- The reported name
 *		- channel flags
 * output	- NONE
 * side effects - do a who on given person
 */

static void do_who(struct Client *server_p,
                   struct Client *aclient_p,
                   char *chname,
                   char *op_flags)
{
  char  status[5];

  ircsprintf(status,"%c%s%s", 
	     aclient_p->user->away ? 'G' : 'H',
	     IsOper(aclient_p) ? "*" : "", op_flags );

  if(GlobalSetOptions.hide_server)
    {
      sendto_one(server_p, form_str(RPL_WHOREPLY), me.name, server_p->name,
		 (chname) ? (chname) : "*",
		 aclient_p->username,
		 aclient_p->host, IsOper(server_p) ? aclient_p->user->server : "*",
		 aclient_p->name,
		 status, 0, aclient_p->info);
    }
  else
    {
      sendto_one(server_p, form_str(RPL_WHOREPLY), me.name, server_p->name,
		 (chname) ? (chname) : "*",
		 aclient_p->username,
		 aclient_p->host,  aclient_p->user->server, aclient_p->name,
		 status, aclient_p->hopcount, aclient_p->info);
    }
}

/*
** ms_who
**      parv[0] = sender prefix
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag, only 'o' for now.
*/
static void ms_who(struct Client *client_p,
                  struct Client *server_p,
                  int parc,
                  char *parv[])
{
  /* If its running as a hub, and linked with lazy links
   * then allow leaf to use normal client m_who()
   * other wise, ignore it.
   */

  if( ServerInfo.hub )
    {
      if(!IsCapable(client_p->from,CAP_LL))
	return;
    }

  m_who(client_p,server_p,parc,parv);
}
