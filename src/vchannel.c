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

#include <assert.h>
#include <string.h>
#include <stdlib.h>

void    add_vchan_to_client_cache(struct Client *sptr,
				  struct Channel *vchan,
				  struct Channel *base_chan)
{
  int i=0;

  assert(sptr != NULL);

  while(sptr->vchan_map[i].base_chan)
    {
      i++;
    }
  assert(i != MAXCHANNELSPERUSER);

  sptr->vchan_map[i].base_chan = base_chan;
  sptr->vchan_map[i].vchan = vchan;
}
