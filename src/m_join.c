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

#include "m_commands.h"
#include "channel.h"
#include "client.h"
#include "common.h"   /* bleah */
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "send.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * m_functions execute protocol messages on this server:
 *
 *      cptr    is always NON-NULL, pointing to a *LOCAL* client
 *              structure (with an open socket connected!). This
 *              identifies the physical socket where the message
 *              originated (or which caused the m_function to be
 *              executed--some m_functions may call others...).
 *
 *      sptr    is the source of the message, defined by the
 *              prefix part of the message if present. If not
 *              or prefix not found, then sptr==cptr.
 *
 *              (!IsServer(cptr)) => (cptr == sptr), because
 *              prefixes are taken *only* from servers...
 *
 *              (IsServer(cptr))
 *                      (sptr == cptr) => the message didn't
 *                      have the prefix.
 *
 *                      (sptr != cptr && IsServer(sptr) means
 *                      the prefix specified servername. (?)
 *
 *                      (sptr != cptr && !IsServer(sptr) means
 *                      that message originated from a remote
 *                      user (not local).
 *
 *              combining
 *
 *              (!IsServer(sptr)) means that, sptr can safely
 *              taken as defining the target structure of the
 *              message in this server.
 *
 *      *Always* true (if 'parse' and others are working correct):
 *
 *      1)      sptr->from == cptr  (note: cptr->from == cptr)
 *
 *      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
 *              *cannot* be a local connection, unless it's
 *              actually cptr!). [MyConnect(x) should probably
 *              be defined as (x == x->from) --msa ]
 *
 *      parc    number of variable parameter strings (if zero,
 *              parv is allowed to be NULL)
 *
 *      parv    a NULL terminated list of parameter pointers,
 *
 *                      parv[0], sender (prefix string), if not present
 *                              this points to an empty string.
 *                      parv[1]...parv[parc-1]
 *                              pointers to additional parameters
 *                      parv[parc] == NULL, *always*
 *
 *              note:   it is guaranteed that parv[0]..parv[parc-1] are all
 *                      non-NULL pointers.
 */


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
  struct SLink  *lp;
  struct Channel *chptr = NULL;
  char  *name, *key = NULL;
  int   i, flags = 0;
#ifdef NO_CHANOPS_WHEN_SPLIT
  int   allow_op=YES;
#endif
  char  *p = NULL, *p2 = NULL;
#ifdef ANTI_SPAMBOT
  int   successful_join_count = 0; /* Number of channels successfully joined */
#endif
  
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


#ifdef NEED_SPLITCODE

  /* Check to see if the timer has timed out, and if so, see if
   * there are a decent number of servers now connected 
   * to consider any possible split over.
   * -Dianora
   */

  if (server_was_split)
    check_still_split();

#endif

  *jbuf = '\0';
  /*
  ** Rebuild list of channels joined to be the actual result of the
  ** JOIN.  Note that "JOIN 0" is the destructive problem.
  */
  for (i = 0, name = strtoken(&p, parv[1], ","); name;
       name = strtoken(&p, (char *)NULL, ","))
    {
      if (!check_channel_name(name))
        {
          sendto_one(sptr, form_str(ERR_BADCHANNAME),
                       me.name, parv[0], (unsigned char*) name);
          continue;
        }
      if (*name == '&' && !MyConnect(sptr))
        continue;
      if (*name == '0' && !atoi(name))
        *jbuf = '\0';
      else if (!IsChannelName(name))
        {
          if (MyClient(sptr))
            sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                       me.name, parv[0], name);
          continue;
        }


#ifdef NO_JOIN_ON_SPLIT_SIMPLE
      if (server_was_split && MyClient(sptr) && (*name != '&') &&
          !IsAnOper(sptr))
        {
              sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                         me.name, parv[0], name);
              continue;
        }
#endif /* NO_JOIN_ON_SPLIT_SIMPLE */

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
      /* If from a cold start, there were never any channels
       * joined, hence all of them must be considered off limits
       * until this server joins the network
       *
       * cold_start is set to NO if SPLITDELAY is set to 0 in m_set()
       */

      if(cold_start && MyClient(sptr) && (*name != '&') &&
         !IsAnOper(sptr))
        {
              sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                         me.name, parv[0], name);
              continue;
        }
#endif
      if (*jbuf)
        (void)strcat(jbuf, ",");
      (void)strncat(jbuf, name, sizeof(jbuf) - i - 1);
      i += strlen(name)+1;
    }
  /*    (void)strcpy(parv[1], jbuf);*/

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
      */
      if (*name == '0' && !atoi(name))
        {
          if (sptr->user->channel == NULL)
            continue;
          while ((lp = sptr->user->channel))
            {
              chptr = lp->value.chptr;
              sendto_channel_butserv(chptr, sptr, ":%s PART %s",
                                     parv[0], chptr->chname);
              remove_user_from_channel(sptr, chptr, 0);
            }

#ifdef ANTI_SPAMBOT       /* Dianora */

          if( MyConnect(sptr) && !IsAnOper(sptr) )
            {
              if(SPAMNUM && (sptr->join_leave_count >= SPAMNUM))
                {
                  sendto_ops_flags(FLAGS_BOTS,
                                     "User %s (%s@%s) is a possible spambot",
                                     sptr->name,
                                     sptr->username, sptr->host);
                  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
                }
              else
                {
                  int t_delta;

                  if( (t_delta = (CurrentTime - sptr->last_leave_time)) >
                      JOIN_LEAVE_COUNT_EXPIRE_TIME)
                    {
                      int decrement_count;
                      decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);

                      if(decrement_count > sptr->join_leave_count)
                        sptr->join_leave_count = 0;
                      else
                        sptr->join_leave_count -= decrement_count;
                    }
                  else
                    {
                      if((CurrentTime - (sptr->last_join_time)) < SPAMTIME)
                        {
                          /* oh, its a possible spambot */
                          sptr->join_leave_count++;
                        }
                    }
                  sptr->last_leave_time = CurrentTime;
                }
            }
#endif
          sendto_match_servs(NULL, cptr, ":%s JOIN 0", parv[0]);
          continue;
        }
      
      if (MyConnect(sptr))
        {
          /*
          ** local client is first to enter previously nonexistent
          ** channel so make them (rightfully) the Channel
          ** Operator.
          */
           /*     flags = (ChannelExists(name)) ? 0 : CHFL_CHANOP; */

          /* To save a redundant hash table lookup later on */
           
           if((chptr = hash_find_channel(name, NullChn)))
             flags = 0;
           else
             flags = CHFL_CHANOP;

           /* if its not a local channel, or isn't an oper
            * and server has been split
            */

#ifdef NO_CHANOPS_WHEN_SPLIT
          if((*name != '&') && !IsAnOper(sptr) && server_was_split)
            {
              allow_op = NO;

              if(!IsRestricted(sptr) && (flags & CHFL_CHANOP))
                sendto_one(sptr,":%s NOTICE %s :*** Notice -- Due to a network split, you can not obtain channel operator status in a new channel at this time.",
                       me.name,
                       sptr->name);
            }
#endif

          if ((sptr->user->joined >= MAXCHANNELSPERUSER) &&
             (!IsAnOper(sptr) || (sptr->user->joined >= MAXCHANNELSPERUSER*3)))
            {
              sendto_one(sptr, form_str(ERR_TOOMANYCHANNELS),
                         me.name, parv[0], name);
#ifdef ANTI_SPAMBOT
              if(successful_join_count)
                sptr->last_join_time = CurrentTime;
#endif
              return 0;
            }


#ifdef ANTI_SPAMBOT       /* Dianora */
          if(flags == 0)        /* if channel doesn't exist, don't penalize */
            successful_join_count++;
          if( SPAMNUM && (sptr->join_leave_count >= SPAMNUM))
            { 
              /* Its already known as a possible spambot */
 
              if(sptr->oper_warn_count_down > 0)  /* my general paranoia */
                sptr->oper_warn_count_down--;
              else
                sptr->oper_warn_count_down = 0;
 
              if(sptr->oper_warn_count_down == 0)
                {
                  sendto_ops_flags(FLAGS_BOTS,
                    "User %s (%s@%s) trying to join %s is a possible spambot",
                             sptr->name,
                             sptr->username,
                             sptr->host,
                             name);     
                  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
                }
#ifndef ANTI_SPAMBOT_WARN_ONLY
              return 0; /* Don't actually JOIN anything, but don't let
                           spambot know that */
#endif
            }
#endif
        }
      else
        {
          /*
          ** complain for remote JOINs to existing channels
          ** (they should be SJOINs) -orabidoo
          */
          if (!ChannelExists(name))
            ts_warn("User on %s remotely JOINing new channel", 
                    sptr->user->server);
        }

#if defined(NO_JOIN_ON_SPLIT)
      if(server_was_split && (*name != '&'))
        {
          if( chptr )   /* The channel existed, so I can't join it */
            {
              if (IsMember(sptr, chptr)) /* already a member, ignore this */
                continue;

              /* allow local joins to this channel */
              if( (chptr->users == 0) && !IsAnOper(sptr) )
                {
                  sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                             me.name, parv[0], name);
                  continue;
                }
            }
          else
            chptr = get_channel(sptr, name, CREATE);        
        }
      else
#endif
      if(!chptr)        /* If I already have a chptr, no point doing this */
        chptr = get_channel(sptr, name, CREATE);

      if(chptr)
        {
          if (IsMember(sptr, chptr))    /* already a member, ignore this */
            continue;
        }
      else
        {
          sendto_one(sptr,
                     ":%s %d %s %s :Sorry, cannot join channel.",
                     me.name, i, parv[0], name);
#ifdef ANTI_SPAMBOT
          if(successful_join_count > 0)
            successful_join_count--;
#endif
          continue;
        }

      /*
       * can_join checks for +i key, bans.
       * If a ban is found but an exception to the ban was found
       * flags will have CHFL_EXCEPTION set
       */

      if (MyConnect(sptr) && (i = can_join(sptr, chptr, key, &flags)))
        {
          sendto_one(sptr,
                    form_str(i), me.name, parv[0], name);
#ifdef ANTI_SPAMBOT
          if(successful_join_count > 0)
            successful_join_count--;
#endif
          continue;
        }

      /*
      **  Complete user entry to the new channel (if any)
      */

#ifdef NO_CHANOPS_WHEN_SPLIT
      if(allow_op)
        {
          add_user_to_channel(chptr, sptr, flags);
        }
      else
        {
          add_user_to_channel(chptr, sptr, flags & CHFL_EXCEPTION);
        }
#else
      add_user_to_channel(chptr, sptr, flags);
#endif
      /*
      **  Set timestamp if appropriate, and propagate
      */
      if (MyClient(sptr) && (flags & CHFL_CHANOP) )
        {
          chptr->channelts = CurrentTime;
#ifdef NO_CHANOPS_WHEN_SPLIT
          if(allow_op)
            {
              sendto_match_servs(chptr, cptr,
                                 ":%s SJOIN %lu %s + :@%s", me.name,
                                 chptr->channelts, name, parv[0]);

            }                                
          else
            sendto_match_servs(chptr, cptr,
                               ":%s SJOIN %lu %s + :%s", me.name,
                               chptr->channelts, name, parv[0]);
#else
          sendto_match_servs(chptr, cptr,
                             ":%s SJOIN %lu %s + :@%s", me.name,
                             chptr->channelts, name, parv[0]);
#endif
        }
      else if (MyClient(sptr))
        {
          sendto_match_servs(chptr, cptr,
                             ":%s SJOIN %lu %s + :%s", me.name,
                             chptr->channelts, name, parv[0]);
        }
      else
        sendto_match_servs(chptr, cptr, ":%s JOIN :%s", parv[0],
                           name);

      /*
      ** notify all other users on the new channel
      */
      sendto_channel_butserv(chptr, sptr, ":%s JOIN :%s",
                             parv[0], name);

      if (MyClient(sptr))
        {
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

          del_invite(sptr, chptr);

          if (chptr->topic[0] != '\0')
            {
              sendto_one(sptr, form_str(RPL_TOPIC), me.name,
                         parv[0], name, chptr->topic);
#ifdef TOPIC_INFO
              sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
                         me.name, parv[0], name,
                         chptr->topic_nick,
                         chptr->topic_time);
#endif
            }
          parv[1] = name;
          (void)m_names(cptr, sptr, 2, parv);
        }
    }

#ifdef ANTI_SPAMBOT
  if(MyConnect(sptr) && successful_join_count)
    sptr->last_join_time = CurrentTime;
#endif
  return 0;
}

