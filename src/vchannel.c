/************************************************************************
 *   IRC - Internet Relay Chat, src/vchannel.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Co Center
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
 *
 *
 * $Id$
 */
#include "tools.h"
#include "vchannel.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "send.h"
#include "numeric.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

/* Given base chan pointer and vchan pointer add to
 * translation table cache for this client
 */

static void
vchan_show_ids(struct Client *sptr, struct Channel *chptr);

void    add_vchan_to_client_cache(struct Client *sptr,
				  struct Channel *base_chan,
				  struct Channel *vchan)
{
  dlink_node *vchanmap_node;
  struct Vchan_map *vchan_info;

  assert(sptr != NULL);

  /* oops its the top channel of the subchans */
  if( base_chan == vchan )
    return;

  vchan_info = (struct Vchan_map *)MyMalloc(sizeof(struct Vchan_map));
  vchan_info->base_chan = base_chan;
  vchan_info->vchan = vchan;

  vchanmap_node = make_dlink_node();
  dlinkAdd(vchan_info, vchanmap_node, &sptr->vchan_map);
}

/* Given vchan pointer remove from translation table cache */

void del_vchan_from_client_cache(struct Client *sptr, struct Channel *vchan)
{
  dlink_node *vchanmap_node;
  struct Vchan_map *vchan_info;

  assert(sptr != NULL);

  for (vchanmap_node = sptr->vchan_map.head; vchanmap_node;
       vchanmap_node = vchanmap_node->next)
    {
      vchan_info = vchanmap_node->data;     
      if (vchan_info->vchan == vchan)
        {
          MyFree(vchan_info);
          dlinkDelete(vchanmap_node, &sptr->vchan_map);
          free_dlink_node(vchanmap_node);
          return;
        }
    }
}

/* see if this client given by sptr is on a subchan already */

int on_sub_vchan(struct Channel *chptr, struct Client *sptr)
{
  dlink_node *vchanmap_node;
  struct Vchan_map *vchan_info;

  assert(sptr != NULL);

  /* they are in the root chan */
  if (IsMember(sptr, chptr))
    return YES;

  /* check to see if this chptr maps to a sub vchan */
  for (vchanmap_node = sptr->vchan_map.head; vchanmap_node;
       vchanmap_node = vchanmap_node->next)
    {
      vchan_info = vchanmap_node->data;
      if (vchan_info->base_chan == chptr)
        return YES;
    }

  return NO;
}

/* return matching vchan given base chan and sptr */
struct Channel* map_vchan(struct Channel *chptr, struct Client *sptr)
{
  dlink_node *vchanmap_node;
  struct Vchan_map *vchan_info;

  assert(sptr != NULL);

  /* they're in the root chan */
  if (IsMember(sptr, chptr))
    return chptr;

  /* check to see if this chptr maps to a sub vchan */
  for (vchanmap_node = sptr->vchan_map.head; vchanmap_node;
       vchanmap_node = vchanmap_node->next)
    {
      vchan_info = vchanmap_node->data;
      if (vchan_info->base_chan == chptr)
	return (vchan_info->vchan);
    }

  return NullChn;
}

/* return matching bchan given vchan and sptr */
struct Channel* map_bchan(struct Channel *chptr, struct Client *sptr)
{
  dlink_node *vchanmap_node;
  struct Vchan_map *vchan_info;

  assert(sptr != NULL);

  for (vchanmap_node = sptr->vchan_map.head; vchanmap_node;
       vchanmap_node = vchanmap_node->next)
    {
      vchan_info = vchanmap_node->data;
      if (vchan_info->vchan == chptr)
	return (vchan_info->base_chan);
    }

  return NullChn;
}

/* return the base chan from a vchan, this is less efficient than
 * map_bchan, but only needs a chan pointer.
 */
struct Channel* find_bchan(struct Channel *chptr)
{
  dlink_node *vptr;
  struct Channel *chtmp;

  for(vptr = chptr->vchan_list.head; vptr; vptr = vptr->prev)
    {
      chtmp = vptr->data;

      if (!IsVchan(chtmp))
	return chtmp;
    }

  return NullChn;
}

/* show available vchans */
void show_vchans(struct Client *cptr,
                        struct Client *sptr,
                        struct Channel *chptr,
                        char *command)
{
   int no_of_vchans = 0;

   no_of_vchans = dlink_list_length(&chptr->vchan_list);

   sendto_one(sptr, form_str(RPL_VCHANEXIST),
              me.name, sptr->name, chptr->chname, no_of_vchans);

   vchan_show_ids(sptr, chptr);

   sendto_one(sptr, form_str(RPL_VCHANHELP),
              me.name, sptr->name, command, chptr->chname);
}

static void
vchan_show_ids(struct Client *sptr, struct Channel *chptr)
{
  char buf[BUFSIZE];
  char *t;
  int mlen;
  int cur_len;
  int tlen;
  dlink_node *ptr;
  struct Channel *chtmp;
  int reply_to_send = 0;

  ircsprintf(buf, form_str(RPL_VCHANLIST), me.name, sptr->name,
	     chptr->chname);

  mlen = strlen(buf);
  cur_len = mlen;
  t = buf + mlen;

  for (ptr = chptr->vchan_list.head; ptr; ptr = ptr->next )
     {
       chtmp = ptr->data;

       /* Obey the rules of /list */
       if(SecretChannel(chtmp))
	 continue;

       if (chtmp->users != 0)
	 {
	   ircsprintf(t,"!%s ",pick_vchan_id(chtmp));
	   tlen = strlen(t);
	   cur_len += tlen;
	   t += tlen;
	   reply_to_send = 1;

	   if (cur_len > (BUFSIZE - NICKLEN - 3))
	     {
	       sendto_one(sptr, "%s", buf );
	       cur_len = mlen;
	       t = buf + mlen;
	       reply_to_send = 0;
	     }
	 }
     }

   if (reply_to_send)
     sendto_one(sptr, "%s", buf);
}

/*
 * pick_vchan_id
 * inputs	- pointer to vchan
 * output	- pointer to static string
 * side effects - pick a name from the channel.
 *                the topic setters nick if available,
 *                otherwise use who's been there longest according 
 *                to the server.
 */
char* pick_vchan_id(struct Channel *chptr)
{
  dlink_node *lp;
  struct Client *acptr;
  static char topic_nick[NICKLEN+USERLEN+HOSTLEN+10];
  char *p;

  /* see if we can use the nick of who set the topic */
  if (chptr->topic_info)
    {
      /* XXX */
      strcpy(topic_nick, chptr->topic_info);

      /* cut off anything after a '!' */
      if ( (p = strchr(topic_nick, '!')) )
	*p = '\0';

      if ( (acptr = hash_find_client(topic_nick,(struct Client *)NULL)) &&
           IsMember(acptr, chptr) )
        {
          return topic_nick;
        }
    }

  for (lp = chptr->chanops.head; lp; lp = lp->next)
    if (!lp->next)
      { 
	acptr = lp->data;
	return acptr->name;;
      }

  for (lp = chptr->halfops.head; lp; lp = lp->next)
    if (!lp->next)
      { 
	acptr = lp->data;
	return acptr->name;;
      }

  for (lp = chptr->voiced.head; lp; lp = lp->next)
    if (!lp->next)
      { 
	acptr = lp->data;
	return acptr->name;;
      }

  for (lp = chptr->peons.head; lp; lp = lp->next)
    if (!lp->next)
      {
	acptr = lp->data;
	return acptr->name;
      }

  /* shouldn't get here! */
  return NULL;
}

/* return matching vchan, from root and !key (nick)
 * or NULL if there isn't one */
struct Channel* find_vchan(struct Channel *chptr, char *key)
{
  struct Channel *chtmp;
  struct Client *acptr;

  key++; /* go past the '!' */

  if( (acptr = hash_find_client(key,(struct Client *)NULL)) )
    if( (chtmp = map_vchan(chptr, acptr)) )
      return chtmp;

  return NullChn;
}

/* return the first found invite matching a subchannel of chptr
 * or NULL if no invites are found
 */

struct Channel* vchan_invites(struct Channel *chptr, struct Client *sptr)
{
  dlink_node *lp;
  dlink_node *vptr;
  struct Channel *cp;

  /* loop is nested this way to prevent preferencing channels higher
   * in the vchan list
   */

  for (lp = sptr->user->invited.head; lp; lp = lp->next)
    {
      for (vptr = chptr->vchan_list.head; vptr; vptr = vptr->next)
	{
	  cp = vptr->data;

	  if (lp->data == cp)
	    return cp;
	}
    }

  return NullChn;
}
