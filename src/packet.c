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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "tools.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "client.h"
#include "common.h"
#include "ircd.h"
#include "list.h"
#include "parse.h"
#include "fdlist.h"
#include "packet.h"
#include "irc_string.h"


static char               readBuf[READBUF_SIZE];


/*
 * parse_client_queued - parse client queued messages
 */
int parse_client_queued(struct Client* cptr)
{ 
  int dolen  = 0;

  while ((dolen = linebuf_get(&cptr->localClient->buf_recvq,
                              readBuf, READBUF_SIZE)) > 0) {
    if (CLIENT_EXITED == client_dopacket(cptr, readBuf, dolen))
      return CLIENT_EXITED;
  }
  return 1;
}


/*
 * read_packet - Read a 'packet' of data from a connection and process it.
 */
void
read_packet(int fd, void *data)
{
  struct Client *cptr = data;
  int length = 0;
  int done;

  /*
   * Check for a dead connection here. This is done here for legacy
   * reasons which to me sound like people didn't check the return
   * values of functions, and so we can't just free the cptr in
   * dead_link() :-)
   *     -- adrian
   */
  if (IsDead(cptr)) {
    /* Shouldn't we just do the following? -- adrian */
    /* error_exit_client(cptr, length); */
    exit_client(cptr, cptr, &me, strerror(get_sockerr(cptr->fd)));
    return;
  }

  /*
   * Read some data. We *used to* do anti-flood protection here, but
   * I personally think it makes the code too hairy to make sane.
   *     -- adrian
   */
  length = recv(cptr->fd, readBuf, READBUF_SIZE, 0);
  if (length < 0) {
    /*
     * This shouldn't give an EWOULDBLOCK since we only call this routine
     * when we have data. Therefore, any error we get will be fatal.
     *     -- adrian
     */
    error_exit_client(cptr, length);
    return;
  } else if (length == 0) {
    /* EOF from client */
    error_exit_client(cptr, length);
    return;
  }

#ifdef REJECT_HOLD
  /*
   * If client has been marked as rejected i.e. it is a client
   * that is trying to connect again after a k-line,
   * pretend to read it but don't actually.
   * -Dianora
   *
   * FLAGS_REJECT_HOLD should NEVER be set for non local client
   */
  if (IsRejectHeld(cptr)) {
    goto finish;
  }
#endif

  cptr->lasttime = CurrentTime;
  if (cptr->lasttime > cptr->since)
    cptr->since = cptr->lasttime;
  cptr->flags &= ~(FLAGS_PINGSENT | FLAGS_NONL);

  /*
   * Before we even think of parsing what we just read, stick
   * it on the end of the receive queue and do it when its
   * turn comes around.
   */
  linebuf_parse(&cptr->localClient->buf_recvq, readBuf, length);

  if (IsPerson(cptr) &&
      (ConfigFileEntry.no_oper_flood && !IsAnyOper(cptr)) &&
      linebuf_len(&cptr->localClient->buf_recvq) > CLIENT_FLOOD)
    {
      exit_client(cptr, cptr, cptr, "Excess Flood");
      return;
    }
  parse_client_queued(cptr);
#ifdef REJECT_HOLD
  /* Silence compiler warnings -- adrian */
finish:
#endif
  /* If we get here, we need to register for another COMM_SELECT_READ */
  if (cptr->fd > -1) {
    if (PARSE_AS_SERVER(cptr)) {
      comm_setselect(cptr->fd, FDLIST_SERVER, COMM_SELECT_READ,
        read_packet, cptr, 0);
    } else {
      comm_setselect(cptr->fd, FDLIST_IDLECLIENT, COMM_SELECT_READ,
        read_packet, cptr, 0);
    }
  }
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
  assert(cptr != NULL);
  assert(buffer != NULL);

  /* 
   * Update messages received
   */
  ++me.localClient->receiveM;
  ++cptr->localClient->receiveM;

  /* 
   * Update bytes received
   */
  cptr->localClient->receiveB += length;

  if (cptr->localClient->receiveB > 1023) {
    cptr->localClient->receiveK += (cptr->localClient->receiveB >> 10);
    cptr->localClient->receiveB &= 0x03ff; /* 2^10 = 1024, 3ff = 1023 */
  }

  me.localClient->receiveB += length;

  if (me.localClient->receiveB > 1023)
    {
      me.localClient->receiveK += (me.localClient->receiveB >> 10);
      me.localClient->receiveB &= 0x03ff;
    }

  if (CLIENT_EXITED == parse(cptr, buffer, buffer + length)) {
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


