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
#include "irc_string.h"
#include "ircd.h"
#include "parse.h"
#include "s_zip.h"

#include <assert.h>


/*
 * dopacket
 *      cptr - pointer to client structure for which the buffer data
 *             applies.
 *      buffer - pointr to the buffer containing the newly read data
 *      length - number of valid bytes of data in the buffer
 *
 *      The buffer might be partially or totally zipped.
 *      At the beginning of the compressed flow, it is possible that
 *      an uncompressed ERROR message will be found.  This occurs when
 *      the connection fails on the other server before switching
 *      to compressed mode.
 *
 * Note:
 *      It is implicitly assumed that dopacket is called only
 *      with cptr of "local" variation, which contains all the
 *      necessary fields (buffer etc..)
 */
int dopacket(struct Client* cptr, char* buffer, size_t length)
{
  char* dest;
  char* src;
  char* cptrbuf;
  char* endp;
  int   zipped = NO;
  int   done_unzip = NO;

  /* 
   * Update bytes received
   */
  me.receiveB    += length;
  cptr->receiveB += length;

  if (cptr->receiveB > 1023) {
    cptr->receiveK += (cptr->receiveB >> 10);
    cptr->receiveB &= 0x03ff; /* 2^10 = 1024, 3ff = 1023 */
  }

  if (me.receiveB > 1023) {
    me.receiveK += (me.receiveB >> 10);
    me.receiveB &= 0x03ff;
  }

  cptrbuf = cptr->buffer;
  endp    = cptr->buffer + BUFSIZE - 1;

  dest = cptrbuf + cptr->count;
  src = buffer;

  if (cptr->flags2 & FLAGS2_ZIPFIRST) {
    if (IsEol(*src)) {
      ++src;
      --length;
    }
    cptr->flags2 &= ~FLAGS2_ZIPFIRST;
  }
  else
    done_unzip = YES;

  if (cptr->flags2 & FLAGS2_ZIP) {
    /*
     * uncompressed buffer first
     */
    zipped = length;
    /* 
     * unnecessary but nicer for debugging
     */
    cptr->zip->inbuf[0] = '\0';
    cptr->zip->incount = 0;

    src = unzip_packet(cptr, src, &zipped);

    if (zipped == -1)
      return exit_client(cptr, cptr, &me,
                         "fatal error in unzip_packet(1)");
    length = zipped;
    zipped = 1;
  }
  /* While there is "stuff" in the compressed input to deal with,
   * keep loop parsing it. I have to go through this loop at least once.
   * -Dianora
   */
  do {
    while (length-- > 0) {
      *dest = *src++;

      if (dest < endp && !IsEol(*dest))
        ++dest; /* There is always room for the null */

      else if (dest < endp) {
        if (dest == cptrbuf) {
           /* 
            * Skip extra LF/CR's
            */
           continue;
        }
        *dest = '\0';
        /* 
         * Update messages received
         */
        ++me.receiveM;
        ++cptr->receiveM;

        cptr->count = 0; 

        if (CLIENT_EXITED == parse(cptr, cptr->buffer, dest)) {
          /*
           * CLIENT_EXITED means actually that cptr
           * structure *does* not exist anymore!!! --msa
           */
           return CLIENT_EXITED;
        }
        /*
         * Socket is dead so exit (which always returns with
         * CLIENT_EXITED here).  - avalon
         */
        if (cptr->flags & FLAGS_DEADSOCKET)
          return exit_client(cptr, cptr, &me, "Dead socket");

        if ((cptr->flags2 & FLAGS2_ZIP) && (zipped == 0) && (length > 0)) {
          /*
           * beginning of server connection, the buffer
           * contained PASS/CAPAB/SERVER and is now 
           * zipped!
           * Ignore the '\n' that should be here.
           *
           * Checked RFC1950: \r or \n can't start a zlib stream  -orabidoo
           */

          zipped = length;
          if (zipped > 0 && IsEol(*src)) {
            ++src;
            --zipped;
          }

          cptr->flags2 &= ~FLAGS2_ZIPFIRST;
          src = unzip_packet(cptr, src, &zipped);
          if (zipped == -1)
            return exit_client(cptr, cptr, &me,
                               "fatal error in unzip_packet(2)");
          length = zipped;
          zipped = 1;
        }
        dest = cptrbuf;
      }
    }
    /* Now see if anything is left uncompressed in the input
     * If so, uncompress it and continue to parse
     * -Dianora
     */
    if ((cptr->flags2 & FLAGS2_ZIP) && cptr->zip->incount) {
      /*
       * This call simply finishes unzipping whats left
       * second parameter is not used. -Dianora
       */
      src = unzip_packet(cptr, 0, &zipped);
      if (zipped == -1)
	return exit_client(cptr, cptr, &me,
			   "fatal error in unzip_packet(1)");
      length = zipped;
      zipped = 1;

      dest = src + length;
      done_unzip = NO;
    }
    else
      done_unzip = YES;

  } while (!done_unzip);
 
  cptr->count = dest - cptrbuf;
  return 1;
}

/*
 * dopacket
 *      cptr - pointer to client structure for which the buffer data
 *             applies.
 *      buffer - pointr to the buffer containing the newly read data
 *      length - number of valid bytes of data in the buffer
 *
 *      The buffer might be partially or totally zipped.
 *      At the beginning of the compressed flow, it is possible that
 *      an uncompressed ERROR message will be found.  This occurs when
 *      the connection fails on the other server before switching
 *      to compressed mode.
 *
 * Note:
 *      It is implicitly assumed that dopacket is called only
 *      with cptr of "local" variation, which contains all the
 *      necessary fields (buffer etc..)
 */
int client_dopacket(struct Client *cptr, size_t length)
{
  assert(0 != cptr);

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

  cptr->count = 0;

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


