/************************************************************************
 *   IRC - Internet Relay Chat, src/s_gline.c
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
#include "handlers.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "dline_conf.h"
#include "irc_string.h"
#include "ircd.h"
#include "m_kline.h"
#include "mtrie_conf.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_misc.h"
#include "scache.h"
#include "send.h"
#include "msg.h"
#include "fileio.h"
#include "s_serv.h"
#include "s_gline.h"
#include "hash.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

static struct ConfItem *glines=NULL;
static struct gline_pending *pending_glines=NULL;

static void expire_glines(void);
static void expire_pending_glines(void);


/* add_gline
 *
 * inputs       - pointer to struct ConfItem
 * output       - none
 * Side effects - links in given struct ConfItem into gline link list
 */
void
add_gline(struct ConfItem *aconf)
{
  aconf->next = glines;
  glines = aconf;
}

/* find_gkill
 *
 * inputs       - struct Client pointer to a Client struct
 * output       - struct ConfItem pointer if a gline was found for this client
 * side effects - none
 */
struct ConfItem*
find_gkill(struct Client* cptr, char* username)
{
  assert(0 != cptr);
  return (IsElined(cptr)) ? 0 : find_is_glined(cptr->host, username);
}

/*
 * find_is_glined
 * inputs       - hostname
 *              - username
 * output       - pointer to struct ConfItem if user@host glined
 * side effects -
 */
struct ConfItem*
find_is_glined(const char* host, const char* name)
{
  struct ConfItem *kill_ptr; 

  for(kill_ptr = glines; kill_ptr; kill_ptr = kill_ptr->next)
    {
      if( (kill_ptr->name && (!name || match(kill_ptr->name,name)))
	  &&
	  (kill_ptr->host && (!host || match(kill_ptr->host,host))))
	return(kill_ptr);
    }

  return((struct ConfItem *)NULL);
}


/* report_glines
 *
 * inputs       - struct Client pointer
 * output       - NONE
 * side effects - 
 *
 * report pending glines, and placed glines.
 */
void
report_glines(struct Client *sptr)
{
  struct gline_pending *glp_ptr;
  struct ConfItem *kill_ptr;
  char timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;
  char *host;
  char *name;
  char *reason;

  sendto_one(sptr,":%s NOTICE %s :Pending G-lines",
	     me.name, sptr->name);

  for(glp_ptr = pending_glines; glp_ptr; glp_ptr = glp_ptr->next)
    {
      tmptr = localtime(&glp_ptr->time_request1);
      strftime(timebuffer, MAX_DATE_STRING, "%Y/%m/%d %H:%M:%S", tmptr);

      sendto_one(sptr,
       ":%s NOTICE %s :1) %s!%s@%s on %s requested gline at %s for %s@%s [%s]",
		 me.name,sptr->name,
		 glp_ptr->oper_nick1,
		 glp_ptr->oper_user1,
		 glp_ptr->oper_host1,
		 glp_ptr->oper_server1,
		 timebuffer,
		 glp_ptr->user,
		 glp_ptr->host,
		 glp_ptr->reason1);

      if(glp_ptr->oper_nick2[0])
	{
	  tmptr = localtime(&glp_ptr->time_request2);
	  strftime(timebuffer, MAX_DATE_STRING, "%Y/%m/%d %H:%M:%S", tmptr);
	  sendto_one(sptr,
     ":%s NOTICE %s :2) %s!%s@%s on %s requested gline at %s for %s@%s [%s]",
		     me.name,sptr->name,
		     glp_ptr->oper_nick2,
		     glp_ptr->oper_user2,
		     glp_ptr->oper_host2,
		     glp_ptr->oper_server2,
		     timebuffer,
		     glp_ptr->user,
		     glp_ptr->host,
		     glp_ptr->reason2);
	}
    }

  sendto_one(sptr,":%s NOTICE %s :End of Pending G-lines",
	     me.name, sptr->name);

  for( kill_ptr = glines; kill_ptr; kill_ptr = kill_ptr->next)
    {
      if(kill_ptr->host != NULL)
	host = kill_ptr->host;
      else
	host = "*";

      if(kill_ptr->name != NULL)
	name = kill_ptr->name;
      else
	name = "*";

      if(kill_ptr->passwd)
	reason = kill_ptr->passwd;
      else
	reason = "No Reason";

      sendto_one(sptr,form_str(RPL_STATSKLINE), me.name,
		 sptr->name, 'G' , host, name, reason);
    }
}

/*
 * remove_gline_match
 *
 * inputs       - user@host
 * output       - 1 if successfully removed, otherwise 0
 * side effects -
 */
int
remove_gline_match(const char* user, const char* host)
{
  struct ConfItem *kill_ptr;
  struct ConfItem *last_ptr=NULL;

  for( kill_ptr = glines; kill_ptr; kill_ptr = kill_ptr->next)
    {
      if(!irccmp(kill_ptr->host,host) && !irccmp(kill_ptr->name,user))
	{
	  if(last_ptr != NULL)
	    last_ptr->next = kill_ptr->next;
	  else
	    glines = kill_ptr->next;

          free_conf(kill_ptr);
          return 1;
	}
      last_ptr = kill_ptr;
    }
  return 0;
}

/*
 * cleanup_glines
 *
 * inputs	- NONE
 * output	- NONE
 * side effects - expire gline lists
 *                This is an event started off in ircd.c
 */
void
cleanup_glines(void *notused)
{
  expire_glines();
  expire_pending_glines();

  eventAdd("cleanup_glines", cleanup_glines, NULL,
	   CLEANUP_GLINES_TIME, 0);
}

/*
 * expire_glines
 * 
 * inputs       - NONE
 * output       - NONE
 * side effects -
 *
 * Go through the gline list, expire any needed.
 */
static void
expire_glines()
{
  struct ConfItem *kill_ptr;
  struct ConfItem *last_ptr = NULL;
  struct ConfItem *next_ptr;

  for(kill_ptr = glines; kill_ptr; kill_ptr = next_ptr)
    {
      next_ptr = kill_ptr->next;

      if(kill_ptr->hold <= CurrentTime)
	{
	  if(last_ptr != NULL)
	    last_ptr->next = next_ptr;
	  else
	    glines->next = next_ptr;

	  free_conf(kill_ptr);
	}
      else
	last_ptr = kill_ptr;
    }
}

/*
 * expire_pending_glines
 * 
 * inputs       - NONE
 * output       - NONE
 * side effects -
 *
 * Go through the pending gline list, expire any that haven't had
 * enough "votes" in the time period allowed
 */
static void
expire_pending_glines()
{
  struct gline_pending *glp_ptr;
  struct gline_pending *last_glp_ptr = NULL;
  struct gline_pending *next_glp_ptr = NULL;

  if(pending_glines == NULL)
    return;

  for( glp_ptr = pending_glines; glp_ptr; glp_ptr = next_glp_ptr)
    {
      next_glp_ptr = glp_ptr->next;

      if( (glp_ptr->last_gline_time + GLINE_PENDING_EXPIRE) <= CurrentTime )
        {
          if(last_glp_ptr != NULL)
            last_glp_ptr->next = next_glp_ptr;
          else
            pending_glines = next_glp_ptr;

          MyFree(glp_ptr->reason1);
          MyFree(glp_ptr->reason2);
          MyFree(glp_ptr);
        }
      else
	last_glp_ptr = glp_ptr;
    }
}





