/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_names.c
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
#include "parse.h"
#include "modules.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void names_all_visible_channels(struct Client *source_p);
static void names_non_public_non_secret(struct Client *source_p);

static void m_names(struct Client*, struct Client*, int, char**);
static void ms_names(struct Client*, struct Client*, int, char**);

struct Message names_msgtab = {
  "NAMES", 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_names, ms_names, m_names}
};

void
_modinit(void)
{
  mod_add_cmd(&names_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&names_msgtab);
}

char *_version = "20001122";


/************************************************************************
 * m_names() - Added by Jto 27 Apr 1989
 ************************************************************************/

/*
** m_names
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = root name
*/
static void m_names( struct Client *client_p,
                    struct Client *source_p,
                    int parc,
                    char *parv[])
{ 
  struct Channel *vchan;
  struct Channel *ch2ptr = NULL;
  char  *s;
  char *para = parc > 1 ? parv[1] : NULL;

  if (!BadPtr(para))
    {
      if( (s = strchr(para, ',')) )
        *s = '\0';

      if (!check_channel_name(para))
        { 
          sendto_one(source_p, form_str(ERR_BADCHANNAME),
                     me.name, parv[0], (unsigned char *)para);
          return;
        }

      if( (ch2ptr = hash_find_channel(para, NULL)) )
	{
	  if (HasVchans(ch2ptr))
	    {
	      vchan = map_vchan(ch2ptr,source_p);
	      if(vchan == 0)
		channel_member_names( source_p, ch2ptr, ch2ptr->chname );
	      else
		channel_member_names( source_p, vchan, ch2ptr->chname );
	    }
	  else
	    {
	      channel_member_names( source_p, ch2ptr, ch2ptr->chname );
	    }
	  return;
	}
    }
  else
    {
      names_all_visible_channels(source_p);
      names_non_public_non_secret(source_p);
    }

  sendto_one(source_p, form_str(RPL_ENDOFNAMES), me.name, parv[0], "*");
}

/*
 * names_all_visible_channels
 *
 * inputs	- pointer to client struct requesting names
 * output	- none
 * side effects	- lists all visible channels whee!
 */

static void names_all_visible_channels(struct Client *source_p)
{
  int mlen;
  int cur_len;
  int reply_to_send;
  struct Channel *chptr;
  struct Channel *bchan;
  char buf[BUFSIZE];
  char *chname=NULL;
  char *show_ops_flag;
  char *show_voiced_flag;
  char *show_halfop_flag;

  /* 
   * First, do all visible channels (public and the one user self is)
   */

  for (chptr = GlobalChannelList; chptr; chptr = chptr->nextch)
    {
      if (ShowChannel(source_p, chptr))
	{
	  /* Find users on same channel (defined by chptr) */
	  if (IsVchan(chptr))
	    {
	      bchan = find_bchan (chptr);
	      if (bchan != NULL)
		chname = bchan->chname;
	    }
	  else
	    chname = chptr->chname;

	  if(chptr->mode.mode & MODE_HIDEOPS && !is_any_op(chptr,source_p))
	    {
	      show_ops_flag = "";
	      show_voiced_flag = "";
	      show_halfop_flag = "";
	    }
	  else
	    {
	      show_ops_flag = "@";
	      show_voiced_flag = "+";
	      show_halfop_flag = "%";
	    }

	  ircsprintf(buf,form_str(RPL_NAMREPLY),
		     me.name, source_p->name,channel_pub_or_secret(chptr));
	  mlen = strlen(buf);
	  ircsprintf(buf + mlen," %s :", chname);
	  mlen = strlen(buf);
	  cur_len = mlen;

	  channel_member_list(source_p,
                              chptr,
			      &chptr->chanops,
			      &chptr->voiced,
			      &chptr->halfops,
			      &chptr->peons,
			      show_ops_flag,
			      show_voiced_flag,
			      show_halfop_flag,
			      buf, mlen, &cur_len, &reply_to_send);

	  if (reply_to_send)
	    sendto_one(source_p, "%s", buf);
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

static void names_non_public_non_secret(struct Client *source_p)
{
  int mlen;
  int tlen;
  int cur_len;
  int reply_to_send = NO;
  int dont_show = NO;
  dlink_node    *lp;
  struct Client *c2ptr;
  struct Channel *ch3ptr=NULL;
  char buf[BUFSIZE];
  char *t;

  ircsprintf(buf,form_str(RPL_NAMREPLY),
	     me.name,source_p->name," * * :");

  mlen = strlen(buf);

  cur_len = mlen;
  t = buf + mlen;

  /* Second, do all non-public, non-secret channels in one big sweep */

  for (c2ptr = GlobalClientList; c2ptr; c2ptr = c2ptr->next)
    {
      if (!IsPerson(c2ptr) || IsInvisible(c2ptr))
        continue;
      /*
       * dont show a client if they are on a secret channel or
       * they are on a channel source_p is on since they have already
       * been shown earlier. -avalon
       */
      for( lp = c2ptr->user->channel.head; lp; lp = lp->next )
        {
          ch3ptr = lp->data;

          if ( (!PubChannel(ch3ptr) || IsMember(source_p, ch3ptr)) ||
	       (SecretChannel(ch3ptr)))
	  {
            dont_show = YES;
	    break;
	  }
        }
      if (dont_show) /* on any secret channels or shown already? */
        continue;

      if(lp == NULL)	/* Nothing to do. yay */
	continue;

      if(ch3ptr->mode.mode & MODE_HIDEOPS)
	ircsprintf(t," %s ", c2ptr->name);
      else
	ircsprintf(t,"%s%s ", channel_chanop_or_voice(ch3ptr, c2ptr),
		   c2ptr->name);

      tlen = strlen(t);
      cur_len += tlen;
      t += tlen;

      reply_to_send = YES;

      if ( (cur_len + NICKLEN)  > (BUFSIZE - 3))
        {
          sendto_one(source_p, "%s", buf);
          reply_to_send = NO;
	  cur_len = mlen;
	  t = buf + mlen;
        }
    }

  if (reply_to_send)
    sendto_one(source_p, "%s", buf );
}

static void ms_names( struct Client *client_p,
                     struct Client *source_p,
                     int parc,
                     char *parv[])
{ 
  /* If its running as a hub, and linked with lazy links
   * then allow leaf to use normal client m_names()
   * other wise, ignore it.
   */

  if( ServerInfo.hub )
    {
      if(!IsCapable(client_p->from,CAP_LL))
	return;
    }

  m_names(client_p,source_p,parc,parv);
}


