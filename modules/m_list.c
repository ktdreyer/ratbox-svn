/************************************************************************
 *   IRC - Internet Relay Chat, src/m_list.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Co Center
 *
 * $Id$ 
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
 */
#include "handlers.h"
#include "channel.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "vchannel.h"
#include "list.h"
#include "msg.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

struct Message list_msgtab = {
  MSG_LIST, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_list, m_ignore, m_list}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_LIST, &list_msgtab);
}

int list_all_channels(struct Client *sptr);
int list_named_channel(struct Client *sptr,char *name);

/*
** m_list
**      parv[0] = sender prefix
**      parv[1] = channel
*/
int     m_list(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  static time_t last_used=0L;

  /* If its a LazyLinks connection, let uplink handle the list */

  if( serv_cptr_list && IsCapable( serv_cptr_list, CAP_LL) )
    {
      if(parc < 2)
	sendto_one( serv_cptr_list, ":%s LIST", sptr->name );
      else
	sendto_one( serv_cptr_list, ":%s LIST %s", sptr->name, parv[1] );
      return 0;
    }

  /* If not a LazyLink connection, see if its still paced */

  if( ( (last_used + ConfigFileEntry.pace_wait) > CurrentTime) )
    {
      sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }
  else
    last_used = CurrentTime;

  /* If no arg, do all channels *whee*, else just one channel */
  if (parc < 2 || BadPtr(parv[1]))
    {
      list_all_channels(sptr);
    }
  else
    {
      list_named_channel(sptr,parv[1]);
    }

  return 0;
}


/*
** mo_list
**      parv[0] = sender prefix
**      parv[1] = channel
*/
int     mo_list(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  /* Opers don't get paced */

  /* If its a LazyLinks connection, let uplink handle the list
   * even for opers!
   */

  if( serv_cptr_list && IsCapable( serv_cptr_list, CAP_LL) )
    {
      if(parc < 2)
	sendto_one( serv_cptr_list, ":%s LIST", sptr->name );
      else
	sendto_one( serv_cptr_list, ":%s LIST %s", sptr->name, parv[1] );
      return 0;
    }

  /* If no arg, do all channels *whee*, else just one channel */
  if (parc < 2 || BadPtr(parv[1]))
    {
      list_all_channels(sptr);
    }
  else
    {
      list_named_channel(sptr,parv[1]);
    }

  return 0;
}

/*
** ms_list
**      parv[0] = sender prefix
**      parv[1] = channel
*/
int     ms_list(struct Client *cptr,
		struct Client *sptr,
		int parc,
		char *parv[])
{
  /* Only allow remote list if LazyLink request */

  if( ConfigFileEntry.hub )
    {
      if(!IsCapable(cptr->from,CAP_LL) && !MyConnect(sptr))
	return 0;

      if (parc < 2 || BadPtr(parv[1]))
	{
	  list_all_channels(sptr);
	}
      else
	{
	  list_named_channel(sptr,parv[1]);
	}
    }
  return 0;
}

/*
 * list_all_channels
 * inputs	- pointer to client requesting list
 * output	- 0/1
 * side effects	- list all channels to sptr
 */
int list_all_channels(struct Client *sptr)
{
  struct Channel *chptr;
  struct Channel *tmpchptr;
  int i;
  int j;

  SetDoingList(sptr);     /* only set if its a full list */
  ClearSendqPop(sptr);    /* just to make sure */

  /* we'll do this by looking through each hash table bucket */
  for (i=0; i<CH_MAX; i++)
    {
      for (j=0, chptr = (struct Channel*)(hash_get_channel_block(i).list);
	   (chptr) && (j<hash_get_channel_block(i).links);
	   chptr = chptr->hnextch, j++)
	{
	  if (!chptr->members || !sptr->user ||
	      (SecretChannel(chptr) && !IsMember(sptr, chptr)))
	    continue;

	  /* EVIL!  sendto_one doesnt return status of any kind!
	   *  Forcing us to make up yet another stupid client flag
	   * (we could just negate the DOING_LIST flag,
	   * but that might confuse people) -good
	   */

	  list_one_channel(sptr,chptr);

	  if (IsSendqPopped(sptr))
	    {
	      /* GAAH!  We popped our sendq. 
	       * Mark our location in the /list
	       */
	      sptr->listprogress=i;
	      sptr->listprogress2=j;
	      return 0;
	    }
	}
      
    }

  sendto_one(sptr, form_str(RPL_LISTEND), me.name, sptr->name);
  if (IsSendqPopped(sptr))
    {
      sptr->listprogress=-1;
      return 0;
    }
  ClearDoingList(sptr);   /* yupo, its over */
  return 0;
}   
          
/*
 * list_named_channel
 * inputs	- pointer to client requesting list
 * output	- 0/1
 * side effects	- list all channels to sptr
 */
int list_named_channel(struct Client *sptr,char *name)
{
  char  vname[CHANNELLEN+NICKLEN+4];
  struct Channel *chptr;
  struct Channel *root_chptr;
  struct Channel *tmpchptr;
  char *p;

  if((p = strchr(name,',')))
    *p = '\0';
  
  if(*name == '\0')
    return 0;

  chptr = hash_find_channel(name, NullChn);
  root_chptr = find_bchan(chptr);
  for (tmpchptr = root_chptr; tmpchptr; tmpchptr = tmpchptr->next_vchan)
    if (ShowChannel(sptr, tmpchptr) && tmpchptr->members && sptr->user)
      {
	if( (IsVchan(tmpchptr) || HasVchans(tmpchptr)) &&
	    (root_chptr->members || root_chptr->next_vchan->next_vchan) )
	  {
	    ircsprintf(vname, "%s<!%s>", root_chptr->chname,
		       pick_vchan_id(tmpchptr));
	  }
	else
	  ircsprintf(vname, "%s", root_chptr->chname);

	sendto_one(sptr, form_str(RPL_LIST), me.name, sptr->name,
		   vname, tmpchptr->users, tmpchptr->topic);
      }
  sendto_one(sptr, form_str(RPL_LISTEND), me.name, sptr->name);
  return 0;
}

#if 0

/* XXX I think this still belongs here but its going to have
 * to be done differently with module support, for now, you'll
 * find this code in channel.c -db
 */

/*
 * list_continue
 *
 * inputs	- client pointer 
 * ouput	- 
 * side effects -
 */
int list_continue(struct Client *sptr)
{
  struct Channel *chptr;
  int i;
  int j;

  if (sptr->listprogress != -1)
    {
      for (i=sptr->listprogress; i<CH_MAX; i++)
	{
	  int progress2 = sptr->listprogress2;
	  for (j=0, chptr=(struct Channel*)(hash_get_channel_block(i).list);
	       (chptr) && (j<hash_get_channel_block(i).links);
	       chptr=chptr->hnextch, j++)
	    {
	      if (j<progress2)
		continue;  /* wind up to listprogress2 */
	      if (!chptr->members || !sptr->user ||
		  (SecretChannel(chptr) && !IsMember(sptr, chptr)))
		continue;

	      list_one_channel(sptr,chptr);

	      if (IsSendqPopped(sptr))
		{
		  /* we popped again! : P */
		  sptr->listprogress=i;
		  sptr->listprogress2=j;
		  return 0;
		}
	    }
	  sptr->listprogress2 = 0;
	}
    }
  sendto_one(sptr, form_str(RPL_LISTEND), me.name, sptr->name);
  if (IsSendqPopped(sptr))
    { /* popped with the RPL_LISTEND code. d0h */
      sptr->listprogress = -1;
      return 0;
    }
  ClearDoingList(sptr);   /* yupo, its over */
  return 0;
}

/*
 * list_one_channel
 *
 * inputs	- client pointer to return result to
 *		- pointer to channel to list
 * ouput	- none
 * side effects -
 */
void list_one_channel(struct Client *sptr,struct Channel *chptr)
{
  struct Channel *root_chptr;
  char  vname[CHANNELLEN+NICKLEN+4];
  struct Channel *root_chptr;

  root_chptr = find_bchan(chptr);

  if( (IsVchan(chptr) || HasVchans(chptr)) && 
      (root_chptr->members || root_chptr->next_vchan->next_vchan) )
    {
      ircsprintf(vname, "%s<!%s>", root_chptr->chname,
		 pick_vchan_id(chptr));
    }
  else
    ircsprintf(vname, "%s", root_chptr->chname);

  sendto_one(sptr, form_str(RPL_LIST), me.name, sptr->name,
	     vname, chptr->users, chptr->topic);

}

#endif


