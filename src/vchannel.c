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

void    add_vchan_to_client_cache(struct Client *sptr,
				  struct Channel *base_chan,
				  struct Channel *vchan)
{
  int i=0;

  assert(sptr != NULL);

  /* oops its the top channel of the subchans */
  if( base_chan == vchan )
    return;

  while(sptr->vchan_map[i].base_chan != NULL)
    {
      i++;
    }
  assert(i != MAXCHANNELSPERUSER);

  sptr->vchan_map[i].base_chan = base_chan;
  sptr->vchan_map[i].vchan = vchan;
}

/* Given vchan pointer remove from translation table cache */

void del_vchan_from_client_cache(struct Client *sptr, struct Channel *vchan)
{
  int i;

  assert(sptr != NULL);

  for(i=0;sptr->vchan_map[i].base_chan;i++)
    {
      if( sptr->vchan_map[i].vchan == vchan )
	{
	  sptr->vchan_map[i].vchan = NULL;
	  sptr->vchan_map[i].base_chan = NULL;
	}
    }
}

/* see if this client given by sptr is on a subchan already */

int on_sub_vchan(struct Channel *chptr, struct Client *sptr)
{
  int i;

  assert(sptr != NULL);

  /* they are in the root chan */
  if (IsMember(sptr, chptr))
    return YES;

  for(i=0;sptr->vchan_map[i].base_chan;i++)
    {
      if( sptr->vchan_map[i].base_chan == chptr )
	return YES;
    }

  return NO;
}

/* return matching vchan given base chan and sptr */
struct Channel* map_vchan(struct Channel *chptr, struct Client *sptr)
{
  int i;

  assert(sptr != NULL);

  /* they're in the root chan */
  if (IsMember(sptr, chptr))
    return chptr;

  for(i=0;sptr->vchan_map[i].base_chan;i++)
    {
      if( sptr->vchan_map[i].base_chan == chptr )
	return (sptr->vchan_map[i].vchan);
    }

  return NullChn;
}

/* return matching bchan given vchan and sptr */
struct Channel* map_bchan(struct Channel *chptr, struct Client *sptr)
{
  int i;

  assert(sptr != NULL);

  for(i=0;sptr->vchan_map[i].base_chan;i++)
    {
      if( sptr->vchan_map[i].vchan == chptr )
	return (sptr->vchan_map[i].base_chan);
    }

  return NullChn;
}

/* return the base chan from a vchan, this is less efficient than
 * map_bchan, but only needs a chan pointer. */
struct Channel* find_bchan(struct Channel *chptr)
{
  struct Channel *chtmp;

  for(chtmp = chptr; chtmp; chtmp = chtmp->prev_vchan)
    if (!IsVchan(chtmp))
      return chtmp;

  return NullChn;
}

/* show available vchans */
void show_vchans(struct Client *cptr,
                        struct Client *sptr,
                        struct Channel *chptr,
                        char *command)
{
   struct Channel *chtmp;
   int no_of_vchans = 0;
   int reply_to_send = 1;
   int len;
   char key_list[BUFSIZE];

   *key_list = '\0';

   for (chtmp = chptr; chtmp; chtmp = chtmp->next_vchan)
     if (chtmp->users)
       no_of_vchans++;

   sendto_one(sptr, form_str(RPL_VCHANEXIST),
              me.name, sptr->name, chptr->chname, no_of_vchans);

   len = ( strlen(me.name) + NICKLEN + strlen(chptr->chname) + 8 );

   for (chtmp = chptr; chtmp; chtmp = chtmp->next_vchan)
     {
       /* Obey the rules of /list */
       if(SecretChannel(chtmp))
	 continue;

       /* XXX */
       if (chtmp->chanops.head)
	 {
	   strcat(key_list, "!");
	   strcat(key_list, pick_vchan_id(chtmp));
	   strcat(key_list, " ");

	   if ((len + strlen(key_list)) > (BUFSIZE - NICKLEN - 3))
	     {
	       sendto_one(sptr, form_str(RPL_VCHANLIST),
			  me.name, sptr->name, chptr->chname, key_list);
	       key_list[0] = '\0';

	       /* last one in the list, we won't be sending any more */
	       if (!chtmp->next_vchan)
		 reply_to_send = 0;
	     }
	 }
     }

   if (reply_to_send)
     sendto_one(sptr, form_str(RPL_VCHANLIST),
                me.name, sptr->name, chptr->chname, key_list);

   sendto_one(sptr, form_str(RPL_VCHANHELP),
              me.name, sptr->name, command, chptr->chname);
}

/* pick a name from the channel.  the topic setters nick if available,
 * otherwise we use who's been there longest according to the server */
char* pick_vchan_id(struct Channel *chptr)
{
  dlink_node *lp;
  struct Client *acptr;
  char *topic_nick, *p;

  /* see if we can use the nick of who set the topic */
  if (chptr->topic_info)
    {
      topic_nick = (char *)MyMalloc(strlen(chptr->topic_info));
      strcpy(topic_nick, chptr->topic_info);

      /* cut off anything after a '!' */
      p = strchr(topic_nick, '!');
      if (p)
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
 * or NULL if no invites are found */

struct Channel* vchan_invites(struct Channel *chptr, struct Client *sptr)
{
  dlink_node *lp;
  struct Channel *cp;

  /* loop is nested this way to prevent preferencing channels higher
     in the vchan list */

  for (lp = sptr->user->invited.head; lp; lp = lp->next)
    for (cp = chptr; cp; cp = cp->next_vchan)
      if (lp->data == cp)
        return cp;

  return NullChn;
}
