/************************************************************************
 *   IRC - Internet Relay Chat, src/m_names.c
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void names_all_visible_channels(struct Client *sptr);
static void names_non_public_non_secret(struct Client *sptr);
static char *pub_or_secret(struct Channel *chptr);
static char *chanop_or_voice(struct SLink *lp);

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

/************************************************************************
 * m_names() - Added by Jto 27 Apr 1989
 ************************************************************************/

/*
** m_names
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = root name
*/
/* maximum names para to show to opers when abuse occurs */
#define TRUNCATED_NAMES 20


int     m_names( struct Client *cptr,
                 struct Client *sptr,
                 int parc,
                 char *parv[])
{ 
  struct Channel *vchan;
  struct Channel *ch2ptr = NULL;
  char  *s, *para = parc > 1 ? parv[1] : NULL;
  int comma_count=0;
  int char_count=0;

  if (!BadPtr(para))
    {
      /* Here is the lamer detection code
       * P.S. meta, GROW UP
       * -Dianora 
       */
      for(s = para; *s; s++)
        {
          char_count++;
          if(*s == ',')
            comma_count++;
          if(comma_count > 1)
            {
              if(char_count > TRUNCATED_NAMES)
                para[TRUNCATED_NAMES] = '\0';
              else
                {
                  s++;
                  *s = '\0';
                }
              sendto_realops("POSSIBLE /names abuser %s [%s]",
                             para,
                             get_client_name(sptr,FALSE));
              sendto_one(sptr, form_str(ERR_TOOMANYTARGETS),
                         me.name, sptr->name, "NAMES", 1);
              return 0;
            }
        }

      if( (s = strchr(para, ',')) )
        *s = '\0';

      if (!check_channel_name(para))
        { 
          sendto_one(sptr, form_str(ERR_BADCHANNAME),
                     me.name, parv[0], (unsigned char *)para);
          return 0;
        }

      if( (ch2ptr = hash_find_channel(para, NULL)) )
	{
	  if (HasVchans(ch2ptr))
	    {
	      vchan = map_vchan(ch2ptr,sptr);
	      if(vchan == 0)
		names_on_this_channel( sptr, ch2ptr, ch2ptr->chname );
	      else
		names_on_this_channel( sptr, vchan, ch2ptr->chname );
	    }
	  else
	    {
	      names_on_this_channel( sptr, ch2ptr, ch2ptr->chname );
	    }
	}
    }
  else
    {
      names_all_visible_channels(sptr);
      names_non_public_non_secret(sptr);
    }

  sendto_one(sptr, form_str(RPL_ENDOFNAMES), me.name, parv[0], "*");
  return(1);
}

/*
 * names_all_visible_channels
 *
 * inputs	- pointer to client struct requesting names
 * output	- none
 * side effects	- lists all visible channels whee!
 */

static void names_all_visible_channels(struct Client *sptr)
{
  int mlen;
  int len;
  int cur_len;
  int reply_to_send = 0;
  struct SLink  *lp;
  struct Client *c2ptr;
  struct Channel *chptr;
  struct Channel *bchan;
  char buf[BUFSIZE];
  char buf2[2*NICKLEN];
  char *chname;

  mlen = strlen(me.name) + NICKLEN + 7;
  cur_len = mlen;
  
  /* 
   * First, do all visible channels (public and the one user self is)
   */

  for (chptr = GlobalChannelList; chptr; chptr = chptr->nextch)
    {
      if (ShowChannel(sptr, chptr))
	{
	  /* Find users on same channel (defined by chptr) */
	  chname = chptr->chname;

	  if (IsVchan(chptr))
	    {
	      bchan = find_bchan (chptr);
	      if (bchan != NULL)
		chname = bchan->chname;
	    }

	  ircsprintf(buf,"%s %s :", pub_or_secret(chptr), chname);
	  len = strlen(buf);
	  cur_len = len + mlen;

	  reply_to_send = YES;

	  for (lp = chptr->members; lp; lp = lp->next)
	    {
	      c2ptr = lp->value.cptr;
	      if (IsInvisible(c2ptr) && !IsMember(sptr,chptr))
		continue;

	      ircsprintf(buf2,"%s%s ", chanop_or_voice(lp), c2ptr->name);
	      strcat(buf,buf2);
	      cur_len += strlen(buf2);

	      if ((cur_len + NICKLEN) > (BUFSIZE - 3))
		{
		  sendto_one(sptr, form_str(RPL_NAMREPLY),
			     me.name, sptr->name, buf);
		  ircsprintf(buf,"%s %s :",
			     pub_or_secret(chptr), chname);
		  reply_to_send = NO;
		  cur_len = len + mlen;
		}
	    }
	  if (reply_to_send)
	    sendto_one(sptr, form_str(RPL_NAMREPLY),
		       me.name, sptr->name, buf);
	}
    }
}

/*
 * names_non_public_non_secret
 *
 * inputs	- pointer to client struct requesting names
 * output	- none
 * side effects	- lists all non public non secret channels
 */

static void names_non_public_non_secret(struct Client *sptr)
{
  int mlen;
  int len;
  int cur_len;
  int reply_to_send = NO;
  int dont_show = NO;
  struct SLink  *lp;
  struct Client *c2ptr;
  struct Channel *ch3ptr;
  char buf[BUFSIZE];
  char buf2[2*NICKLEN];

  mlen = strlen(me.name) + NICKLEN + 7;

  /* Second, do all non-public, non-secret channels in one big sweep */

  strncpy_irc(buf, "* * :", 6);
  len = strlen(buf);
  cur_len = len + mlen;

  for (c2ptr = GlobalClientList; c2ptr; c2ptr = c2ptr->next)
    {
      if (!IsPerson(c2ptr) || IsInvisible(c2ptr))
        continue;
      /*
       * dont show a client if they are on a secret channel or
       * they are on a channel sptr is on since they have already
       * been shown earlier. -avalon
       */
      for( lp = c2ptr->user->channel; lp; lp = lp->next )
        {
          ch3ptr = lp->value.chptr;
          if ( (PubChannel(ch3ptr) || IsMember(sptr, ch3ptr)) ||
	       (SecretChannel(ch3ptr)))
	  {
            dont_show = YES;
	    break;
	  }
        }
      if (dont_show) /* on any secret channels or shown already? */
        continue;
      ircsprintf(buf2,"%s%s ", chanop_or_voice(lp), c2ptr->name);
      strcat(buf,buf2);
      cur_len += strlen(buf2);
      reply_to_send = YES;

      if ( (cur_len + NICKLEN)  > (BUFSIZE - 3))
        {
          sendto_one(sptr, form_str(RPL_NAMREPLY),
                     me.name, sptr->name, buf);
          reply_to_send = NO;
	  strncpy_irc(buf, "* * :", 6);
	  cur_len = len + mlen;
        }
    }

  if (reply_to_send)
    sendto_one(sptr, form_str(RPL_NAMREPLY), me.name, sptr->name, buf);
}

/*
 * names_on_this_channel
 *
 * inputs	- pointer to client struct requesting names
 * output	- none
 * side effects	- lists all non public non secret channels
 */

void names_on_this_channel( struct Client *sptr,
			    struct Channel *chptr,
			    char *name_of_channel)
{
  int mlen;
  int len;
  int cur_len;
  int reply_to_send = NO;
  char buf[BUFSIZE];
  char buf2[2*NICKLEN];
  struct Client *c2ptr;
  struct SLink  *lp;

  mlen = strlen(me.name) + NICKLEN + 7;

  /* Find users on same channel (defined by chptr) */

  ircsprintf(buf, "%s %s :", pub_or_secret(chptr), name_of_channel);
  len = strlen(buf);

  cur_len = mlen + len;

  for (lp = chptr->members; lp; lp = lp->next)
    {
      c2ptr = lp->value.cptr;
      ircsprintf(buf2,"%s%s ", chanop_or_voice(lp), c2ptr->name);
      strcat(buf,buf2);
      cur_len += strlen(buf2);
      reply_to_send = YES;

      if ((cur_len + NICKLEN) > (BUFSIZE - 3))
	{
	  sendto_one(sptr, form_str(RPL_NAMREPLY),
		     me.name, sptr->name, buf);
	  ircsprintf(buf,"%s %s :", pub_or_secret(chptr), name_of_channel);
	  reply_to_send = NO;
	  cur_len = mlen + len;
	}
    }

  if(reply_to_send)
    sendto_one(sptr, form_str(RPL_NAMREPLY), me.name, sptr->name, buf);
}


static char *pub_or_secret(struct Channel *chptr)
{
  if(PubChannel(chptr))
    return("=");
  else if(SecretChannel(chptr))
    return("@");
  else
    return("*");
}


static char *chanop_or_voice(struct SLink *lp)
{
  if (lp->flags & CHFL_CHANOP)
    return("@");
  else if (lp->flags & CHFL_VOICE)
    return("+");
  return("");
}

int ms_names( struct Client *cptr,
	      struct Client *sptr,
	      int parc,
	      char *parv[])
{ 
  /* If its running as a hub, and linked with lazy links
   * then allow leaf to use normal client m_names()
   * other wise, ignore it.
   */

  if( ConfigFileEntry.hub )
    {
      if(!IsCapable(cptr->from,CAP_LL))
	return 0;
    }

  return( m_names(cptr,sptr,parc,parv) );
}


