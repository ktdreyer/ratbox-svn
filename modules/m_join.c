/************************************************************************
 *   IRC - Internet Relay Chat, src/m_join.c
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
#include "channel.h"
#include "vchannel.h"
#include "client.h"
#include "common.h"   /* bleah */
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "send.h"
#include "s_serv.h"
#include "s_conf.h"
#include "msg.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct Message join_msgtab = {
  MSG_JOIN, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_join, ms_join, m_join}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_JOIN, &join_msgtab);
}

void build_list_of_channels( struct Client *sptr,
                				    char *jbuf, char *given_names);
void do_join_0(struct Client *cptr, struct Client *sptr);
void check_spambot_warning( struct Client *sptr, char *name );
int can_join (struct Client *, struct Channel *, char *,int *);

char *_version = "20001122";

/*
** m_join
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = channel password (key)
*/
int     m_join(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  static char   jbuf[BUFSIZE];
  struct Channel *chptr = NULL;
  struct Channel *vchan_chptr = NULL;
  struct Channel *root_chptr = NULL;
  int joining_vchan = 0;
  char  *name, *key = NULL;
  int   i, flags = 0;
  char  *p = NULL, *p2 = NULL;
  int   successful_join_count = 0; /* Number of channels successfully joined */
  
  if (!(sptr->user))
    {
      /* something is *fucked* - bail */
      return 0;
    }

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "JOIN");
      return 0;
    }

  build_list_of_channels( sptr, jbuf , parv[1] );

  p = NULL;
  if (parv[2])
    key = strtoken(&p2, parv[2], ",");
  parv[2] = NULL;       /* for m_names call later, parv[parc] must == NULL */

  for (name = strtoken(&p, jbuf, ","); name;
       key = (key) ? strtoken(&p2, NULL, ",") : NULL,
         name = strtoken(&p, NULL, ","))
    {
      /*
      ** JOIN 0 sends out a part for all channels a user
      ** has joined.
	  **
	  ** this should be either disabled or selectable in
	  ** config file .. it's abused a lot more than it's
	  ** used these days :/ --is
      */
      if (*name == '0' && !atoi(name))
        {
          if (sptr->user->channel == NULL)
            continue;
	  do_join_0(cptr,sptr);
	  check_spambot_warning(sptr,"0");
	  continue;
	}

      /*
      ** local client is first to enter previously nonexistent
      ** channel so make them (rightfully) the Channel
      ** Operator.
      */
           
      if((chptr = hash_find_channel(name, NullChn)))
	{
	  /* there's subchans so check those
	   * but not if it was a subchan's realname they specified */
	  if (IsVchanTop(chptr))
	    {
	      if( on_sub_vchan(chptr,sptr) )
		continue;
	      if (key && key[0] == '!')
		{
		  /* user joined with key "!".  force listing.
		     (this prevents join-invited-chan voodoo) */
		  if (!key[1])
		    {
		      show_vchans(cptr, sptr, chptr, "join");
		      continue;
		    }

		  /* found a matching vchan? let them join it */
		  if ((vchan_chptr = find_vchan(chptr, key)))
		    {
		      root_chptr = chptr;
		      chptr = vchan_chptr;
		      joining_vchan = 1;
		    }
		  else
		    {
		      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
				 me.name, parv[0], name);
		      return 0;
		    }
		}
	      else
		{
		  /* one special case here i think..
		   * if there's only one vchan, and the root is empty
		   * let them join that vchan */
		  if( (!chptr->members) && (!chptr->next_vchan->next_vchan) )
		    {
		      root_chptr = chptr;
		      chptr = chptr->next_vchan;
		      joining_vchan = 1;
		    }
		  else
		    {
		      /* voodoo to auto-join channel invited to */
		      if ((vchan_chptr=vchan_invites(chptr, sptr)))
			{
			  root_chptr = chptr;
			  chptr = vchan_chptr;
			  joining_vchan = 1;
			}
		      /* otherwise, they get a list of channels */
		      else
			{
			  show_vchans(cptr, sptr, chptr, "join");
			  continue;
			}
		    }
		}
	    }
	  /* trying to join a sub chans 'real' name
	   * don't allow that */
	  else if (IsVchan(chptr))
	    {
	      sendto_one(sptr, form_str(ERR_BADCHANNAME),
			 me.name, parv[0], (unsigned char*) name);
	      continue;
	    }
	  flags = 0;
	}
      else
	{
	  flags = CHFL_CHANOP;
	  if(!ConfigFileEntry.hub)
	    {
	      /* LazyLinks */
	      if( (*name != '&') && serv_cptr_list
		  && IsCapable( serv_cptr_list, CAP_LL) )
		{
		  sendto_one(serv_cptr_list,":%s CBURST %s %s %s",
			     me.name,name,sptr->name, key ? key: "" );
		  /* And wait for LLJOIN */
		  return 0;
		}
	    }
	}

      if (sptr->user->joined >= MAXCHANNELSPERUSER)
	{
	  sendto_one(sptr, form_str(ERR_TOOMANYCHANNELS),
		     me.name, parv[0], name);
	  if(successful_join_count)
	    sptr->last_join_time = CurrentTime;
	  return 0;
	}

      if(flags == 0)        /* if channel doesn't exist, don't penalize */
	successful_join_count++;

      check_spambot_warning(sptr, name);

      if(!chptr)        /* If I already have a chptr, no point doing this */
	chptr = get_channel(sptr, name, CREATE);
      
      if(chptr)
	{
	  if (IsMember(sptr, chptr))    /* already a member, ignore this */
	    continue;
	}
      else
	{
	  sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
		     me.name, parv[0], name);
	  if(successful_join_count > 0)
	    successful_join_count--;
	  continue;
	}
      
      /*
       * can_join checks for +i key, bans.
       * If a ban is found but an exception to the ban was found
       * flags will have CHFL_EXCEPTION set
       */

      if ( (i = can_join(sptr, chptr, key, &flags)) )
	{
	  sendto_one(sptr,
		     form_str(i), me.name, parv[0], name);
	  if(successful_join_count > 0)
	    successful_join_count--;
	  continue;
	}

      /*
      **  Complete user entry to the new channel (if any)
      */
      
      add_user_to_channel(chptr, sptr, flags);

      /*
      **  Set timestamp if appropriate, and propagate
      */
      if (flags & CHFL_CHANOP)
	{
	  if (joining_vchan)
	    add_vchan_to_client_cache(sptr,root_chptr,chptr);
	  chptr->channelts = CurrentTime;
	  sendto_match_servs(chptr, cptr,
			     ":%s SJOIN %lu %s + :@%s", me.name,
			     chptr->channelts, name, parv[0]);
	}
      else 
	{
	  if (joining_vchan)
	    add_vchan_to_client_cache(sptr,root_chptr,chptr);
	  sendto_match_servs(chptr, cptr,
			     ":%s SJOIN %lu %s + :%s", me.name,
			     chptr->channelts, name, parv[0]);
	}

      /*
      ** notify all other users on the new channel
      */
      sendto_channel_butserv(chptr, sptr, ":%s JOIN :%s",
			     parv[0], name);
      
      if( flags & CHFL_CHANOP )
	{
	  chptr->mode.mode |= MODE_TOPICLIMIT;
	  chptr->mode.mode |= MODE_NOPRIVMSGS;

	  sendto_channel_butserv(chptr, sptr,
				 ":%s MODE %s +nt",
				 me.name, chptr->chname);
	  
	  sendto_match_servs(chptr, sptr, 
			     ":%s MODE %s +nt",
			     me.name, chptr->chname);
	}

      del_invite(chptr, sptr);
      
      if (chptr->topic[0] != '\0')
	{
	  sendto_one(sptr, form_str(RPL_TOPIC), me.name,
		     parv[0], name, chptr->topic);
	  
	  sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
		     me.name, parv[0], name,
		     chptr->topic_info,
		     chptr->topic_time);
	}

      if (joining_vchan)
        (void)channel_member_names(sptr, chptr, root_chptr->chname);
      else
        (void)channel_member_names(sptr, chptr, name);
      
      if(successful_join_count)
	sptr->last_join_time = CurrentTime;
    }
  return 0;
}

int     ms_join(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  char *name;
  
  name = parv[1];

  /*
  ** complain for remote JOINs to existing channels
  ** (they should be SJOINs) -orabidoo
  */
  if ((name[1] == '0') && (name[1] == '\0'))
    {
      do_join_0( cptr, sptr );
    }
  else
    {
      ts_warn("User on %s remotely JOINing new channel", 
	      sptr->user->server);
    }

  /* AND ignore it finally. */
  return 0;
}

/*
 * build_list_of_channels
 *
 * inputs	- pointer to client joining
 *		- pointer to scratch buffer
 *		- pointer to list of channel names
 * output	- NONE
 * side effects - jbuf is modified to contain valid list of channel names
 */
void build_list_of_channels( struct Client *sptr,
				    char *jbuf, char *given_names)
{
  char *name;
  char *p;
  int i;

  *jbuf = '\0';

  /*
  ** Rebuild list of channels joined to be the actual result of the
  ** JOIN.  Note that "JOIN 0" is the destructive problem.
  */
  for (i = 0, name = strtoken(&p, given_names, ","); name;
       name = strtoken(&p, (char *)NULL, ","))
    {
      if (!check_channel_name(name))
        {
          sendto_one(sptr, form_str(ERR_BADCHANNAME),
                       me.name, sptr->name, (unsigned char*) name);
          continue;
        }
      if (*name == '0' && (atoi(name) == 0))
	{
	  strcat(jbuf,"0");
	  continue;
	}
      else if (!IsChannelName(name))
        {
	  sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
		     me.name, sptr->name, name);
          continue;
        }

      /* We can't do this for interoperability reasons ;-( */
#if 0
      if (strlen(name) > CHANNELLEN-15)
        {
          sendto_one(sptr, form_str(ERR_BADCHANNAME),me.name, parv[0], name);
          continue;
        }
#endif

      if (*jbuf)
        (void)strcat(jbuf, ",");
      (void)strncat(jbuf, name, BUFSIZE - i - 1);
      i += strlen(name)+1;
    }
}

/*
 * do_join_0
 *
 * inputs	- pointer to client doing join 0
 * output	- NONE
 * side effects	- Use has decided to join 0. This is legacy
 *		  from the days when channels were numbers not names. *sigh*
 *		  There is a bunch of evilness necessary here due to
 * 		  anti spambot code.
 */

void do_join_0(struct Client *cptr, struct Client *sptr)
{
  struct Channel *chptr=NULL;
  struct SLink   *lp;

  sendto_match_servs(NULL, cptr, ":%s JOIN 0", sptr->name);

  while ((lp = sptr->user->channel))
    {
      chptr = lp->value.chptr;
      sendto_channel_butserv(chptr, sptr, ":%s PART %s",
			     sptr->name, chptr->chname);
      remove_user_from_channel(chptr, sptr, 0);
    }
}

/*
 * check_spambot_warning
 *
 * inputs	- pointer to client to check
 * output	- NONE
 * side effects	- 
 */

void check_spambot_warning( struct Client *sptr, char *name )
{
  int t_delta;
  int decrement_count;

  if(GlobalSetOptions.spam_num &&
     (sptr->join_leave_count >= GlobalSetOptions.spam_num))
    {
      if(sptr->oper_warn_count_down == 0)
	{
	  /* Its already known as a possible spambot */
	  
	  if(sptr->oper_warn_count_down > 0)  /* my general paranoia */
	    sptr->oper_warn_count_down--;
	  else
	    sptr->oper_warn_count_down = 0;

	  sendto_ops_flags(FLAGS_BOTS,
		   "User %s (%s@%s) trying to join %s is a possible spambot",
			   sptr->name,
			   sptr->username,
			   sptr->host,
			   name);     
	  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
	}
    }
  else
    {
      if( (t_delta = (CurrentTime - sptr->last_leave_time)) >
	  JOIN_LEAVE_COUNT_EXPIRE_TIME)
	{
	  decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);

	  if(decrement_count > sptr->join_leave_count)
	    sptr->join_leave_count = 0;
	  else
	    sptr->join_leave_count -= decrement_count;
	}
      else
	{
	  if((CurrentTime - (sptr->last_join_time)) < 
	     GlobalSetOptions.spam_time)
	    {
	      /* oh, its a possible spambot */
	      sptr->join_leave_count++;
	    }
	}
      sptr->last_leave_time = CurrentTime;
    }
}

/*
 * can_join
 *
 * inputs	- 
 * output	- 
 * side effects - NONE
 */
int can_join(struct Client *sptr, struct Channel *chptr,
		    char *key, int *flags)
{
  struct SLink  *lp;
  int ban_or_exception;

  if ( (ban_or_exception = is_banned(chptr,sptr)) == CHFL_BAN)
    return (ERR_BANNEDFROMCHAN);
  else
    *flags |= ban_or_exception; /* Mark this client as "charmed" */

  if (chptr->mode.mode & MODE_INVITEONLY)
    {
      for (lp = sptr->user->invited; lp; lp = lp->next)
        if (lp->value.chptr == chptr)
          break;
      if (!lp)
        return (ERR_INVITEONLYCHAN);
    }

  if (*chptr->mode.key && (BadPtr(key) || irccmp(chptr->mode.key, key)))
    return (ERR_BADCHANNELKEY);

  if (chptr->mode.limit && chptr->users >= chptr->mode.limit)
    return (ERR_CHANNELISFULL);

  return 0;
}

#ifdef DBOP
/* ZZZZZZZZZZZZ Q&D debug function */
int     m_dbop(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  int counted_ops=0;
  struct SLink  *l;
  char *name;
  struct Channel *chptr;

  name = parv[1];

  if(!(chptr=hash_find_channel(name, NullChn)))
    {
      sendto_one(sptr,
      ":%s NOTICE %s :*** Notice %s does not exist",
        me.name, sptr->name,  name );
      return -1;
    }

  for (l = chptr->members; l && l->value.cptr; l = l->next)
    if (l->flags & MODE_CHANOP)
      {
        counted_ops++;
      }

  sendto_one(sptr,":%s NOTICE %s :*** Notice %s chptr->opcount %d counted %d",
    me.name, sptr->name, name, chptr->opcount, counted_ops);

  return 0;
}
#endif

