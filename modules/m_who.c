/************************************************************************
 *   IRC - Internet Relay Chat, src/m_who.c
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

#include "common.h"   /* bleah */
#include "handlers.h"
#include "client.h"
#include "common.h"   /* bleah */
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

struct Message who_msgtab = {
  MSG_WHO, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_who, ms_who, m_who}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_WHO, &who_msgtab);
}

void do_who_on_channel(struct Client *sptr,
			      struct Channel *chptr, char *real_name,
			      int oper, int member);

void who_global(struct Client *sptr, char *mask, int oper);

void    do_who(struct Client *sptr,
			     struct Client *acptr,
			     char *repname,
			     int flags);

/*
** m_who
**      parv[0] = sender prefix
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag, only 'o' for now.
*/
int     m_who(struct Client *cptr,
              struct Client *sptr,
              int parc,
              char *parv[])
{
  struct Client *acptr;
  char  *mask = parc > 1 ? parv[1] : NULL;
  struct SLink  *lp;
  struct Channel *chptr=NULL;
  struct Channel *vchan;
  struct Channel *mychannel = NULL;
  int   oper = parc > 2 ? (*parv[2] == 'o' ): 0; /* Show OPERS only */
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
	  sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0], "*" );
	  return 0;
	}
    }
  else
    {
      who_global(sptr, mask, oper);
      sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0], "*" );
      return 0;
    }

  /* mask isn't NULL at this point. repeat after me... -db */

  /* '/who *' */

  if ((*(mask+1) == (char) 0) && (*mask == '*'))
    {
      if (sptr->user)
	if ((lp = sptr->user->channel))
	  mychannel = lp->value.chptr;

      if (!mychannel)
        {
          sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0], "*");
          return 0;
        }

      if (HasVchans(mychannel))
	{
	  vchan = map_vchan(mychannel,sptr);
	  if(vchan != 0) 
	    do_who_on_channel(sptr,vchan,"*",NO,YES);
	  else
	    do_who_on_channel(sptr,mychannel,"*",NO,YES);
	}
      else
	do_who_on_channel(sptr,mychannel,"*",NO,YES);
      sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0], "*");
      return 0;
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
	      vchan = map_vchan(chptr,sptr);

	      /* If vchan not 0, that makes them a member automatically */
	      if ( vchan != 0 )
		do_who_on_channel(sptr,vchan,chptr->chname,NO,YES);
	      else
		{
		  if ( IsMember(sptr, chptr) )
		    do_who_on_channel(sptr,chptr,chptr->chname,NO,YES);
		  else if(!SecretChannel(chptr))
		    do_who_on_channel(sptr,chptr,chptr->chname,NO,NO);
		}
	    }
	  else
	    {
	      if ( IsMember(sptr, chptr) )
		do_who_on_channel(sptr,chptr,chptr->chname,NO,YES);
	      else if(!SecretChannel(chptr))
		do_who_on_channel(sptr,chptr,chptr->chname,NO,NO);
	    }
	}
      sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0], mask );
      return 0;
    }

  /* '/who nick' */

  if (((acptr = find_client(mask, NULL)) != NULL) &&
      IsPerson(acptr) && (!oper || IsAnyOper(acptr)))
    {
      struct Channel *bchan;
      char *chname;
      int isinvis = 0;

      isinvis = IsInvisible(acptr);
      for (lp = acptr->user->channel; lp; lp = lp->next)
	{
	  chptr = lp->value.chptr;
	  member = IsMember(sptr, chptr);
	  if (isinvis && !member)
	    continue;
	  if (member || (!isinvis && PubChannel(chptr)))
	    {
	      break;
	    }
	}
      if (chptr != NULL)
	{
	  chname = chptr->chname;
	  lp = find_user_link(chptr->members, acptr);
	  if (IsVchan(chptr))
	    {
	      bchan = find_bchan (chptr);
	      if (bchan != NULL)
		chname = bchan->chname;
	    }

	  do_who(sptr, acptr, chname, ((lp)?lp->flags:0) );
	}
      sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0], mask );
      return 0;
    }

  /* Wasn't a nick, wasn't a channel, wasn't a '*' so ... */
  who_global(sptr, mask, oper);
  sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0], mask );
  return 0;
}

/*
 * who_global
 *
 * inputs	- pointer to client requesting who
 *		- char * mask to match
 *		- int if oper or not
 * output	- NONE
 * side effects - do a global scan of all clients looking for match
 *		  this is slightly expensive on EFnet ...
 */

void who_global(struct Client *sptr,char *mask, int oper)
{
  struct Channel *chptr=NULL;
  struct Channel *bchan;
  struct Client *acptr;
  struct SLink  *lp;
  char  *chname;
  int   showperson;
  int   member;
  int   isinvis;
  int   maxmatches = 500;

  for (acptr = GlobalClientList; acptr; acptr = acptr->next)
    {
      if (!IsPerson(acptr))
        continue;
      if (oper && !IsAnyOper(acptr))
        continue;
      
      showperson = NO;
      /*
       * Show user if they are on the same channel, or not
       * invisible and on a non secret channel (if any).
       * Do this before brute force match on all relevant fields
       * since these are less cpu intensive (I hope :-) and should
       * provide better/more shortcuts - avalon
       */
      isinvis = IsInvisible(acptr);
      for (lp = acptr->user->channel; lp; lp = lp->next)
        {
          chptr = lp->value.chptr;
          member = IsMember(sptr, chptr);
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
      if (!acptr->user->channel && !isinvis)
        showperson = YES;
      if (showperson &&
          (!mask ||
           match(mask, acptr->name) ||
           match(mask, acptr->username) ||
           match(mask, acptr->host) ||
           match(mask, acptr->user->server) ||
           match(mask, acptr->info)))
        {
	  lp = find_user_link(chptr->members, acptr);
	  chname = chptr->chname;
	  if (IsVchan(chptr))
	    {
	      bchan = find_bchan (chptr);
	      if (bchan != NULL)
		chname = bchan->chname;
	    }

	  do_who(sptr, acptr, chname, ((lp)?lp->flags:0) );

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
 *		- int if sptr is oper or not
 *		- int if client is member or not
 * output	- NONE
 * side effects - do a who on given channel
 */

void do_who_on_channel(struct Client *sptr,
			      struct Channel *chptr,
			      char *real_name,
			      int oper, int member)
{
  struct SLink  *lp;

  for (lp = chptr->members; lp; lp = lp->next)
    {
      if (oper && !IsAnyOper(lp->value.cptr))
	continue;
      if (IsInvisible(lp->value.cptr) && !member)
	continue;
      do_who(sptr, lp->value.cptr, real_name, lp->flags);
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

void    do_who(struct Client *sptr,
			     struct Client *acptr,
			     char *repname,
			     int flags)
{
  char  status[5];

  ircsprintf(status,"%c%s%s", 
	     acptr->user->away ? 'G' : 'H',
	     IsAnyOper(acptr) ? "*" : "",
	     channel_chanop_or_voice(flags));

  sendto_one(sptr, form_str(RPL_WHOREPLY), me.name, sptr->name,
             repname, acptr->username,
             acptr->host, acptr->user->server, acptr->name,
             status, acptr->hopcount, acptr->info);
}

/*
** ms_who
**      parv[0] = sender prefix
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag, only 'o' for now.
*/
int     ms_who(struct Client *cptr,
              struct Client *sptr,
              int parc,
              char *parv[])
{
  /* If its running as a hub, and linked with lazy links
   * then allow leaf to use normal client m_who()
   * other wise, ignore it.
   */

  if( ConfigFileEntry.hub )
    {
      if(!IsCapable(cptr->from,CAP_LL))
	return 0;
    }

  return( m_who(cptr,sptr,parc,parv) );
}
