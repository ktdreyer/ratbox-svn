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
#include "vchannel.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "send.h"

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

  for(i=0;sptr->vchan_map[i].base_chan;i++)
    {
      if( sptr->vchan_map[i].base_chan == chptr )
	return (sptr->vchan_map[i].vchan);
    }

  return NullChn;
}

/* show info on vchans, XXXX this needs to be improved! */
void show_vchans(struct Client *cptr,
                        struct Client *sptr,
                        struct Channel *chptr)
{
   int no_of_vchans = 0;
   struct Channel *chtmp1, *chtmp2;

   for (chtmp1 = chptr; chtmp1; chtmp1 = chtmp1->next_vchan)
     no_of_vchans++;

   sendto_one(sptr,
              ":%s NOTICE %s *** %d channels are available for %s",
              me.name, sptr->name, no_of_vchans, chptr->chname);
   sendto_one(sptr,
              ":%s NOTICE %s *** Type /join %s <key> to join the one you wish to join",
               me.name, sptr->name, chptr->chname);

   for (chtmp2 = chptr; chtmp2; chtmp2 = chtmp2->next_vchan)
      sendto_one(sptr,
                 ":%s NOTICE %s *** !%s",
                  me.name, sptr->name, chtmp2->chan_id);
}

/* return matching vchan, or NULL if there isn't one */
struct Channel* find_vchan(struct Channel *chptr, char *key)
{
  struct Channel *chtmp;
  
  key++; /* go past the '!' */

  for (chtmp = chptr; chtmp; chtmp = chtmp->next_vchan)
    if (!irccmp(chtmp->chan_id, key))
      return chtmp;
  
  return NullChn;
}
