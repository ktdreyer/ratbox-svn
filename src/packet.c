/************************************************************************
 *   IRC - Internet Relay Chat, src/packet.c
 *   Copyright (C) 1990  Jarkko Oikarinen and
 *                       University of Oulu, Computing Center
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
 *   $Id$
 */ 
#include "packet.h"
#include "client.h"
#include "common.h"
#include "ircd.h"
#include "list.h"
#include "parse.h"
#include "irc_string.h"

#include <assert.h>


/*
 * client_dopacket - copy packet to client buf and parse it
 *      cptr - pointer to client structure for which the buffer data
 *             applies.
 *      buffer - pointr to the buffer containing the newly read data
 *      length - number of valid bytes of data in the buffer
 *
 * Note:
 *      It is implicitly assumed that dopacket is called only
 *      with cptr of "local" variation, which contains all the
 *      necessary fields (buffer etc..)
 */
int client_dopacket(struct Client *cptr, char *buffer, size_t length)
{
  assert(0 != cptr);
  assert(0 != buffer);

  strncpy_irc(cptr->buffer, buffer, BUFSIZE);
  length = strlen(cptr->buffer); 

  /* 
   * Update messages received
   */
  ++me.receiveM;
  ++cptr->receiveM;

  /* 
   * Update bytes received
   */
  cptr->receiveB += length;

  if (cptr->receiveB > 1023) {
    cptr->receiveK += (cptr->receiveB >> 10);
    cptr->receiveB &= 0x03ff; /* 2^10 = 1024, 3ff = 1023 */
  }

  me.receiveB += length;

  if (me.receiveB > 1023) {
    me.receiveK += (me.receiveB >> 10);
    me.receiveB &= 0x03ff;
  }

  cptr->count = 0;    /* ...just in case parse returns with */
  if (CLIENT_EXITED == parse(cptr, cptr->buffer, cptr->buffer + length)) {
    /*
     * CLIENT_EXITED means actually that cptr
     * structure *does* not exist anymore!!! --msa
     */
    return CLIENT_EXITED;
  }
  else if (cptr->flags & FLAGS_DEADSOCKET) {
    /*
     * Socket is dead so exit (which always returns with
     * CLIENT_EXITED here).  - avalon
     */
    return exit_client(cptr, cptr, &me, 
            (cptr->flags & FLAGS_SENDQEX) ? "SendQ exceeded" : "Dead socket");
  }
  return 1;
}


