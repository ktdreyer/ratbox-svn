/************************************************************************
 *   IRC - Internet Relay Chat, src/m_drop.c
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
 * a number of behaviours in set_mode() have been rewritten
 * These flags can be set in a define if you wish.
 *
 * OLD_P_S      - restore xor of p vs. s modes per channel
 *                currently p is rather unused, so using +p
 *                to disable "knock" seemed worth while.
 * OLD_MODE_K   - new mode k behaviour means user can set new key
 *                while old one is present, mode * -k of old key is done
 *                on behalf of user, with mode * +k of new key.
 *                /mode * -key results in the sending of a *, which
 *                can be used to resynchronize a channel.
 * OLD_NON_RED  - Current code allows /mode * -s etc. to be applied
 *                even if +s is not set. Old behaviour was not to allow
 *                mode * -p etc. if flag was clear
 *
 *
 * $Id$
 */
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "s_serv.h"       /* captab */
#include "s_user.h"
#include "send.h"
#include "whowas.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#ifdef HUB
/* Only HUB's need drop */

/*
** m_drop
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = channel password (key)
**
**      "drop" a channel from consideration on a lazy link
*/

int     m_drop(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  char *name;
  struct Channel *chptr;

  if(parc < 2 || *parv[1] == '\0')
    return 0;

  /* If not a server just ignore it */
  if ( !IsServer(cptr) )
    return 0;

  name = parv[1];

#ifdef DEBUGLL
  sendto_realops("DROP called by %s for %s", cptr->name, name );
#endif

  if(!(chptr=hash_find_channel(name, NullChn)))
    return -1;

  if(cptr->serverMask) /* JIC */
    chptr->lazyLinkChannelExists &= ~cptr->serverMask;
  return 0;
}
#endif
