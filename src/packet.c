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
** dopacket
**      cptr - pointer to client structure for which the buffer data
**             applies.
**      buffer - pointr to the buffer containing the newly read data
**      length - number of valid bytes of data in the buffer
**
** Note:
**      It is implicitly assumed that dopacket is called only
**      with cptr of "local" variation, which contains all the
**      necessary fields (buffer etc..)
*/
int dopacket(struct Client *cptr, char *buffer, size_t length)
{
  char  *ch1;
  char  *ch2;
  register char *cptrbuf;

  cptrbuf = cptr->buffer;
  me.receiveB += length; /* Update bytes received */
  cptr->receiveB += length;

  if (cptr->receiveB > 1023)
    {
      cptr->receiveK += (cptr->receiveB >> 10);
      cptr->receiveB &= 0x03ff; /* 2^10 = 1024, 3ff = 1023 */
    }

  if (me.receiveB > 1023)
    {
      me.receiveK += (me.receiveB >> 10);
      me.receiveB &= 0x03ff;
    }
  ch1 = cptrbuf + cptr->count;
  ch2 = buffer;

      /* While there is "stuff" in uncompressed input to deal with
       * loop around parsing it. -Dianora
       */
      while (length-- > 0)
        {
          register char g;
          g = (*ch1 = *ch2++);
          /*
           * Yuck.  Stuck.  To make sure we stay backward compatible,
           * we must assume that either CR or LF terminates the message
           * and not CR-LF.  By allowing CR or LF (alone) into the body
           * of messages, backward compatibility is lost and major
           * problems will arise. - Avalon
           */

          /* The previous code is just silly, you do at least one test
           * to see if g is less than 16, then at least one more, total of two
           * its gotta be a '\r' or a '\n' before anything happens, so why
           * not just check for either '\n' or '\r' ?
           * -Dianora
           */
          /*      if ( g < '\16' && (g== '\n' || g == '\r')) */

          if ( g == '\n' || g == '\r' )
            {
              if (ch1 == cptrbuf)
                continue; /* Skip extra LF/CR's */
              *ch1 = '\0';
              me.receiveM += 1; /* Update messages received */
              cptr->receiveM += 1;
              cptr->count = 0; /* ...just in case parse returns with
                               ** CLIENT_EXITED without removing the
                               ** structure pointed by cptr... --msa
                               */
              if (parse(cptr, cptr->buffer, ch1) == CLIENT_EXITED)
                /*
                ** CLIENT_EXITED means actually that cptr
                ** structure *does* not exist anymore!!! --msa
                */
                return CLIENT_EXITED;
              /*
              ** Socket is dead so exit (which always returns with
              ** CLIENT_EXITED here).  - avalon
              */
              if (cptr->flags & FLAGS_DEADSOCKET)
                return exit_client(cptr, cptr, &me, (cptr->flags & FLAGS_SENDQEX) ?
                                   ((IsDoingList(cptr)) ?
                                    "Local kill by /list (so many channels!)" :
                                   "SendQ exceeded") : "Dead socket");

              ch1 = cptrbuf;
            }
          else if (ch1 < cptrbuf + (sizeof(cptr->buffer)-1))
            ch1++; /* There is always room for the null */
        }

  cptr->count = ch1 - cptrbuf;
  return 1;
}


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


