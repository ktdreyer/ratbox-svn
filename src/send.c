/************************************************************************
 *   IRC - Internet Relay Chat, src/send.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                    University of Oulu, Computing Center
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
#include "send.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "handlers.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_serv.h"
#include "sprintf_irc.h"
#include "s_conf.h"
#include "list.h"
#include "s_debug.h"
#include "s_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <sys/errno.h>

#define LOG_BUFSIZE 2048

static  char    sendbuf[IRCD_BUFSIZE*4];

static  int
send_message (struct Client *, char *, int);

static void
send_message_remote(struct Client *to, struct Client *from,
		    const char *sendbuf, int len);


/* global for now *sigh* */
unsigned long current_serial=0L;

static void
sendto_list_local(dlink_list *list, const char *sendbuf, int len);

void
send_channel_members(struct Client *one, struct Client *from,
		     dlink_list *list,
		     const char *local_sendbuf, int local_len,
		     const char *remote_sendbuf, int remote_len
		     );

static int
send_format(char *sendbuf, const char *pattern, va_list args);

/*
** dead_link
**      An error has been detected. The link *must* be closed,
**      but *cannot* call ExitClient (m_bye) from here.
**      Instead, mark it with FLAGS_DEADSOCKET. This should
**      generate ExitClient from the main loop.
**
**      If 'notice' is not NULL, it is assumed to be a format
**      for a message to local opers. I can contain only one
**      '%s', which will be replaced by the sockhost field of
**      the failing link.
**
**      Also, the notice is skipped for "uninteresting" cases,
**      like Persons and yet unknown connections...
*/

static int
dead_link(struct Client *to, char *notice)

{
  to->flags |= FLAGS_DEADSOCKET;

  /*
   * If because of BUFFERPOOL problem then clean dbuf's now so that
   * notices don't hurt operators below.
   */
  linebuf_donebuf(&to->localClient->buf_recvq);
  linebuf_donebuf(&to->localClient->buf_sendq);
  if (!IsPerson(to) && !IsUnknown(to) && !(to->flags & FLAGS_CLOSING))
    sendto_realops_flags(FLAGS_ALL,
			 notice, get_client_name(to, FALSE));
  
  Debug((DEBUG_ERROR, notice, get_client_name(to, FALSE)));

  return (-1);
} /* dead_link() */


/*
** send_message
**      Internal utility which delivers one message buffer to the
**      socket. Takes care of the error handling and buffering, if
**      needed.
**
**      if msg is a null pointer, we are flushing connection
*/
static int
send_message(struct Client *to, char *msg, int len)
{
  /* XXX send_message could become send_message_local
   * to->from shouldn't be non NULL in that case
   */
  if (to->from)
    to = to->from; /* shouldn't be necessary */

  if (IsMe(to))
    {
      sendto_realops_flags(FLAGS_ALL,
			   "Trying to send to myself! [%s]", msg);
      return 0;
    }

  if (to->fd < 0)
    return 0; /* Thou shalt not write to closed descriptors */

  if (IsDead(to))
    return 0; /* This socket has already been marked as dead */

  if (linebuf_len(&to->localClient->buf_sendq) > get_sendq(to))
    {
      if (IsServer(to))
        sendto_realops_flags(FLAGS_ALL,
			     "Max SendQ limit exceeded for %s: %d > %d",
			     get_client_name(to, FALSE),
          linebuf_len(&to->localClient->buf_sendq), get_sendq(to));
      if (IsClient(to))
        to->flags |= FLAGS_SENDQEX;
      return dead_link(to, "Max Sendq exceeded");
    }
  else
    {
      if (len)
          linebuf_put(&to->localClient->buf_sendq, msg, len);
    }
    /*
    ** Update statistics. The following is slightly incorrect
    ** because it counts messages even if queued, but bytes
    ** only really sent. Queued bytes get updated in SendQueued.
    */
    to->localClient->sendM += 1;
    me.localClient->sendM += 1;

    /*
     * Now we register a write callback. We *could* try to write some
     * data to the FD, it'd be an optimisation, and we can deal with it
     * later.
     *     -- adrian
     */
    comm_setselect(to->fd, FDLIST_IDLECLIENT, COMM_SELECT_WRITE,
      send_queued_write, to, 0);
    return 0;
} /* send_message() */

/*
 * send_message_remote
 * 
 */
static void
send_message_remote(struct Client *to, struct Client *from,
		    const char *sendbuf, int len)

{
  if(to->from)
    to = to->from;

/* Optimize by checking if (from && to) before everything */
  if (!MyClient(from) && IsPerson(to) && (to->from == from->from))
    {
      if (IsServer(from))
        {
          sendto_realops_flags(FLAGS_ALL,
		      "Send message to %s[%s] dropped from %s(Fake Dir)",
			      to->name, to->from->name, from->name);
          return;
        }

      sendto_realops_flags(FLAGS_ALL,
			   "Ghosted: %s[%s@%s] from %s[%s@%s] (%s)",
			   to->name, to->username, to->host,
			   from->name, from->username, from->host,
			   to->from->name);
      
      sendto_serv_butone(NULL, ":%s KILL %s :%s (%s[%s@%s] Ghosted %s)",
                         me.name, to->name, me.name, to->name,
                         to->username, to->host, to->from->name);

      to->flags |= FLAGS_KILLED;

      if (IsPerson(from))
        sendto_one(from, form_str(ERR_GHOSTEDCLIENT),
                   me.name, from->name, to->name, to->username,
                   to->host, to->from);

      exit_client(NULL, to, &me, "Ghosted client");
      
      return;
    } /* if (!MyClient(from) && IsPerson(to) && (to->from == from->from)) */
  
  Debug((DEBUG_SEND,"Sending [%s] to %s",sendbuf,to->name));

  /* XXX This stuff below should(?) be a common function
   * called by send_message() and send_message_remote()
   * lets think about it, its late...
   */
  if (to->fd < 0)
    return; /* Thou shalt not write to closed descriptors */

  if (IsDead(to))
    return; /* This socket has already been marked as dead */

  if (linebuf_len(&to->localClient->buf_sendq) > get_sendq(to))
    {
      if (IsServer(to))
        sendto_realops_flags(FLAGS_ALL,
			     "Max SendQ limit exceeded for %s: %d > %d",
			     get_client_name(to, FALSE),
          linebuf_len(&to->localClient->buf_sendq), get_sendq(to));
      if (IsClient(to))
	{
	  to->flags |= FLAGS_SENDQEX;
	  dead_link(to, "Max Sendq exceeded");
	}
      return;
    }
  else
    {
      if (len)
          linebuf_put(&to->localClient->buf_sendq, sendbuf, len);
    }
    /*
    ** Update statistics. The following is slightly incorrect
    ** because it counts messages even if queued, but bytes
    ** only really sent. Queued bytes get updated in SendQueued.
    */
    to->localClient->sendM += 1;
    me.localClient->sendM += 1;

    /*
     * Now we register a write callback. We *could* try to write some
     * data to the FD, it'd be an optimisation, and we can deal with it
     * later.
     *     -- adrian
     */
    comm_setselect(to->fd, FDLIST_IDLECLIENT, COMM_SELECT_WRITE,
      send_queued_write, to, 0);
    return;
} /* send_message_remote() */

/*
** send_queued_write
**      This is called when there is a chance the some output would
**      be possible. This attempts to empty the send queue as far as
**      possible, and then if any data is left, a write is rescheduled.
*/
void
send_queued_write(int fd, void *data)
{
  struct Client *to = data;
  const char *msg;
  int retlen;

  /*
  ** Once socket is marked dead, we cannot start writing to it,
  ** even if the error is removed...
  */
  if (IsDead(to)) {
    /*
     * Actually, we should *NEVER* get here--something is
     * not working correct if send_queued is called for a
     * dead socket... --msa
     */
    return;
  } /* if (IsDead(to)) */

  /* Next, lets try to write some data */
  if (linebuf_len(&to->localClient->buf_sendq)) {
    retlen = linebuf_flush(to->fd, &to->localClient->buf_sendq);
    if ((retlen < 0) && (ignoreErrno(errno))) {
      /* we have a non-fatal error, so just continue */
    } else if (retlen < 0) {
      /* We have a fatal error */
      dead_link(to, "Write error to %s, closing link");
      return;
    } else if (retlen == 0) {
      /* 0 bytes is an EOF .. */
      dead_link(to, "EOF during write to %s, closing link");
      return;
    } else {
      /* We have some data written .. update counters */
      to->localClient->sendB += retlen;
      me.localClient->sendB += retlen;
      if (to->localClient->sendB > 1023) { 
        to->localClient->sendK += (to->localClient->sendB >> 10);
        to->localClient->sendB &= 0x03ff;        /* 2^10 = 1024, 3ff = 1023 */
      } else if (me.localClient->sendB > 1023) { 
        me.localClient->sendK += (me.localClient->sendB >> 10);
        me.localClient->sendB &= 0x03ff;
      }
    }
  }

  /* Finally, if we have any more data, reschedule a write */
  if (linebuf_len(&to->localClient->buf_sendq))
      comm_setselect(fd, FDLIST_IDLECLIENT, COMM_SELECT_WRITE,
        send_queued_write, to, 0);
} /* send_queued_write() */

/*
** send message to single client
*/

void
sendto_one(struct Client *to, const char *pattern, ...)

{
  int len;
  va_list args;

  /* send remote if to->from non NULL */
  if (to->from)
    to = to->from;
  
  if (to->fd < 0)
    {
      Debug((DEBUG_ERROR,
             "Local socket %s with negative fd... AARGH!",
             to->name));
    }
  else if (IsMe(to))
    {
      sendto_realops_flags(FLAGS_ALL,
			   "Trying to send [%s] to myself!", sendbuf);
      return;
    }

  va_start(args, pattern);
  len = send_format(sendbuf, pattern, args);
  va_end(args);

  (void)send_message(to, (char *)sendbuf, len);
  Debug((DEBUG_SEND,"Sending [%s] to %s",sendbuf,to->name));
} /* sendto_one() */

/*
 * sendto_channel_butone
 *
 * inputs	- pointer to client(server) to NOT send message to
 *		- pointer to cient that is sending this message
 *		- pointer to channel being sent to
 *		- vargs message
 * output	- NONE
 * side effects	-
 * BUGS		- This function is now too long.
 */
void
sendto_channel_butone(struct Client *one, struct Client *from,
		      struct Channel *chptr, 
                      const char *pattern, ...)
{
  va_list    args;
  char buf[IRCD_BUFSIZE*2];
  char local_prefix[NICKLEN+HOSTLEN+USERLEN+5];
  char local_sendbuf[IRCD_BUFSIZE*2];
  char remote_prefix[NICKLEN+HOSTLEN+USERLEN+5];
  char remote_sendbuf[IRCD_BUFSIZE*2];
  int local_len;
  int remote_len;

  va_start(args, pattern);
  (void)send_format(buf,pattern,args);
  va_end(args);

  if(IsServer(from))
    {
      ircsprintf(local_prefix,":%s ",
		 from->name);
    }
  else
    {
      ircsprintf(local_prefix,":%s!%s@%s ",
		 from->name,
		 from->username,
		 from->host);
    }

  ircsprintf(remote_prefix,":%s ", from->name);

  ircsprintf(remote_sendbuf, "%s%s",remote_prefix,buf);
  remote_len = strlen(remote_sendbuf);

  if(remote_len > 510)
    {
      remote_sendbuf[IRCD_BUFSIZE-2] = '\r';
      remote_sendbuf[IRCD_BUFSIZE-1] = '\n';
      remote_sendbuf[IRCD_BUFSIZE] = '\0';
      remote_len = IRCD_BUFSIZE;
    }

  ircsprintf(local_sendbuf, "%s%s",local_prefix,buf);
  local_len = strlen(local_sendbuf);

  if(local_len > 510)
    {
      local_sendbuf[IRCD_BUFSIZE-2] = '\r';
      local_sendbuf[IRCD_BUFSIZE-1] = '\n';
      local_sendbuf[IRCD_BUFSIZE] = '\0';
      local_len = IRCD_BUFSIZE;
    }

  ++current_serial;
  
  send_channel_members(one, from, &chptr->chanops,
		       (const char *)local_sendbuf, local_len,
		       (const char *)remote_sendbuf, remote_len);

  send_channel_members(one, from, &chptr->voiced,
		       (const char *)local_sendbuf, local_len,
		       (const char *)remote_sendbuf, remote_len);

  send_channel_members(one, from, &chptr->halfops,
		       (const char *)local_sendbuf, local_len,
		       (const char *)remote_sendbuf, remote_len);

  send_channel_members(one, from, &chptr->peons,
		       (const char *)local_sendbuf, local_len,
		       (const char *)remote_sendbuf, remote_len);

} /* sendto_channel_butone() */

void
send_channel_members(struct Client *one, struct Client *from,
		     dlink_list *list,
		     const char *local_sendbuf, int local_len,
		     const char *remote_sendbuf, int remote_len
		     )
{
  dlink_node *ptr;
  struct Client *acptr;

  for (ptr = list->head; ptr; ptr = ptr->next)
    {
      acptr = ptr->data;
      
      if (acptr->from == one)
        continue;
      
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
          if(acptr->serial != current_serial)
	    {
	      send_message(acptr, (char *)local_sendbuf, local_len);
	      acptr->serial = current_serial;
	    }
        }
      else
        {
          /*
           * Now check whether a message has been sent to this
           * remote link already
           */
          if(acptr->from->serial != current_serial)
            {
	      send_message_remote(acptr, from, 
				  (char *)remote_sendbuf, remote_len);
              acptr->from->serial = current_serial;
            }
        }
    }
}

/*
 * sendto_serv_butone
 *
 * Send a message to all connected servers except the client 'one'.
 */
void
sendto_serv_butone(struct Client *one, const char *pattern, ...)
{
  int len;
  va_list args;
  struct Client *cptr;
  dlink_node *ptr;

  va_start(args, pattern);
  len = send_format(sendbuf,pattern,args);
  va_end(args);
  
  for(ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      cptr = ptr->data;

      if (one && (cptr == one->from))
        continue;
      
      send_message(cptr, (char *)sendbuf, len);
    }
} /* sendto_serv_butone() */

/*
 * sendto_cap_serv_butone
 *
 * Send a message to all connected servers except the client 'one'.
 */
void
sendto_cap_serv_butone(int cap, struct Client *one, const char *pattern, ...)
{
  int len;
  va_list args;
  struct Client *cptr;
  dlink_node *ptr;

  va_start(args, pattern);
  len = send_format(sendbuf,pattern,args);
  va_end(args);
  
  for(ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      cptr = ptr->data;

      if (one && (cptr == one->from))
        continue;
      
      if (IsCapable(cptr,cap))
	send_message(cptr, (char *)sendbuf, len);
    }
} /* sendto_cap_serv_butone() */

/*
 * sendto_common_channels_local()
 *
 * inputs	- pointer to client
 *		- pattern to send
 * output	- NONE
 * side effects	- Sends a message to all people on local server who are
 * 		  in same channel with user. 
 *		  used by m_nick.c and exit_one_client.
 */
void
sendto_common_channels_local(struct Client *user, const char *pattern, ...)

{
  int len;
  va_list args;
  dlink_node *ptr;
  struct Channel *chptr;

  va_start(args, pattern);
  len = send_format(sendbuf, pattern, args);
  va_end(args);

  ++current_serial;

  if (user->user)
    {
      for (ptr = user->user->channel.head; ptr; ptr = ptr->next)
	{
	  chptr = ptr->data;

	  sendto_list_local(&chptr->chanops, sendbuf, len);
	  sendto_list_local(&chptr->halfops, sendbuf, len);
	  sendto_list_local(&chptr->voiced, sendbuf, len);
	  sendto_list_local(&chptr->peons, sendbuf, len);
	}
    }
} /* sendto_common_channels() */

/*
 * sendto_channel_local
 *
 * inputs	-
 * output	- NONE
 * side effects - Send a message to all members of a channel that are
 *		  locally connected to this server.
 */
void
sendto_channel_local(int type,
		     struct Channel *chptr,
		     const char *pattern, ...)
{
  int len;
  va_list args;

  va_start(args, pattern);
  len = send_format(sendbuf, pattern, args);
  va_end(args);

  /* Serial number checking isn't strictly necessary, but won't hurt */
  ++current_serial;

  switch(type)
    {
    default:
    case ALL_MEMBERS:
      sendto_list_local(&chptr->chanops, sendbuf, len);
      sendto_list_local(&chptr->halfops, sendbuf, len);
      sendto_list_local(&chptr->voiced,  sendbuf, len);
      sendto_list_local(&chptr->peons,   sendbuf, len);
      break;

    case NON_CHANOPS:
      sendto_list_local(&chptr->peons,   sendbuf, len);
      break;

    case ONLY_CHANOPS_VOICED:
      sendto_list_local(&chptr->chanops, sendbuf, len);
      sendto_list_local(&chptr->halfops, sendbuf, len);
      sendto_list_local(&chptr->voiced,  sendbuf, len);
      break;

    case ONLY_CHANOPS:
      sendto_list_local(&chptr->chanops, sendbuf, len);
      sendto_list_local(&chptr->halfops, sendbuf, len);
      break;
    }

} /* sendto_channel_local() */

/*
 * sendto_list_local
 *
 * inputs	- pointer to all members of this list
 *		- buffer to send
 *		- length of buffer
 * output	- NONE
 * side effects	- all members who are locally on this server on given list
 *		  are sent given message. Right now, its always a channel list
 *		  but there is no reason we could not use another dlink
 *		  list to send a message to a group of people.
 */
static void
sendto_list_local(dlink_list *list, const char *sendbuf, int len)
{
  dlink_node *ptr;
  struct Client *acptr;

  for (ptr = list->head; ptr; ptr = ptr->next)
    {
      if ( (acptr = ptr->data) == NULL )
	continue;

      if (!MyConnect(acptr) || (acptr->fd < 0))
	continue;

      if (acptr->serial == current_serial)
	continue;
      
      acptr->serial = current_serial;

      if (acptr && MyConnect(acptr))
	send_message(acptr, (char *)sendbuf, len);
    }  
} /* sendto_list() */

/*
** send a msg to all ppl on servers/hosts that match a specified mask
** (used for enhanced PRIVMSGs)
**
** addition -- Armin, 8jun90 (gruner@informatik.tu-muenchen.de)
*/

static int
match_it(const struct Client *one, const char *mask, int what)

{
  if(what == MATCH_HOST)
    return match(mask, one->host);
  else
    return match(mask, one->user->server);
} /* match_it() */

/*
 * sendto_channel_remote
 *
 * send to all servers the channel given, except for "from"
 */
void
sendto_channel_remote(struct Channel *chptr,
		      struct Client *from,
		      const char *pattern, ...)
{
  int len;
  va_list args;
  struct Client *cptr;
  dlink_node *ptr;

  va_start(args, pattern);
  len = send_format(sendbuf,pattern,args);
  va_end(args);

  if (chptr)
    {
      if (*chptr->chname == '&')
        return;
    }

  for(ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      cptr = ptr->data;

      if (cptr == from)
        continue;

      if(chptr && ConfigFileEntry.hub && IsCapable(cptr,CAP_LL))
        {
          if( !(chptr->lazyLinkChannelExists & cptr->localClient->serverMask) )
             continue;
        }

      send_message (cptr, (char *)sendbuf, len);
    }
} /* sendto_channel_remote() */

/*
 * sendto_match_cap_servs
 *
 * send to all servers which match the mask at the end of a channel name
 * (if there is a mask present) or to all if no mask, and match the capability
 */

void
sendto_match_cap_servs(struct Channel *chptr, struct Client *from, int cap, 
                       const char *pattern, ...)
{
  int len;
  va_list args;
  struct Client *cptr;
  dlink_node *ptr;

  if (chptr)
    {
      if (*chptr->chname == '&')
        return;
    }

  va_start(args, pattern);
  len = send_format(sendbuf,pattern,args);
  va_end(args);

  for(ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      cptr = ptr->data;

      if (cptr == from)
        continue;
      
      if(!IsCapable(cptr, cap))
        continue;
      
      send_message (cptr, (char *)sendbuf, len);
    }
} /* sendto_match_cap_servs() */

/*
 * sendto_match_butone
 *
 * Send to all clients which match the mask in a way defined on 'what';
 * either by user hostname or user servername.
 *
 * ugh. ONLY used by m_message.c to send an "oper magic" message. ugh.
 */
void
sendto_match_butone(struct Client *one, struct Client *from,
		    char *mask, int what,
		    const char *pattern, ...)
{
  int len;
  va_list args;
  struct Client *cptr;
  dlink_node *ptr;

  va_start(args, pattern);
  len = send_format(sendbuf, pattern, args);
  va_end(args);

  /* scan the local clients */
  for(ptr = lclient_list.head; ptr; ptr = ptr->next)
    {
      cptr = ptr->data;

      if (cptr == one)  /* must skip the origin !! */
        continue;
      
      if (match_it(cptr, mask, what))
	send_message(cptr, (char *)sendbuf, len);
    }

  /* Now scan servers */
  for (ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      cptr = ptr->data;

      if (cptr == one) /* must skip the origin !! */
        continue;

      /*
       * The old code looped through every client on the
       * network for each server to check if the
       * server (cptr) has at least 1 client matching
       * the mask, using something like:
       *
       * for (acptr = GlobalClientList; acptr; acptr = acptr->next)
       *        if (IsRegisteredUser(acptr) &&
       *                        match_it(acptr, mask, what) &&
       *                        (acptr->from == cptr))
       *   vsendto_prefix_one(cptr, from, pattern, args);
       *
       * That way, we wouldn't send the message to
       * a server who didn't have a matching client.
       * However, on a network such as EFNet, that
       * code would have looped through about 50
       * servers, and in each loop, loop through
       * about 50k clients as well, calling match()
       * in each nested loop. That is a very bad
       * thing cpu wise - just send the message
       * to every connected server and let that
       * server deal with it.
       * -wnder
       */

      send_message_remote(cptr, from, sendbuf, len);
    }


} /* sendto_match_butone() */

/*
** send_operwall -- Send Wallop to All Opers on this server
**  now va'ified  -bill
*/

void
send_operwall(struct Client *from, char *type_message, ...)

{
  char sender[NICKLEN + USERLEN + HOSTLEN + 5];
  char message[514];
  char *format;
  va_list va;
  struct Client *acptr;
  struct User *user;
  dlink_node *ptr;

  va_start(va, type_message);
  format = va_arg(va, char *);
  vsnprintf(message, sizeof(message)-2, format, va);
  if (message[strlen(message)-1] != '\n') strncat(message, "\n\0", 2);

  if (!from || !message[0])
    return;

  user = from->user;

  if(IsPerson(from))
    {
      (void)ircsprintf(sender,"%s!%s@%s",from->name,from->username,from->host);
    }
  else
    {
      (void)strcpy(sender, from->name);
    }

  if(type_message != NULL)
    {
      for (ptr = oper_list.head; ptr; ptr = ptr->next)
	{
	  acptr = ptr->data;

	  if (!SendOperwall(acptr))
	    continue; /* has to be oper if in this linklist */
	  sendto_one(acptr, ":%s WALLOPS :%s %s", sender,
		     type_message, message);
	}
    }
  else
    {
      for (ptr = oper_list.head; ptr; ptr = ptr->next)
	{
	  acptr = ptr->data;
	  if (!SendOperwall(acptr))
	    continue; /* has to be oper if in this linklist */

	  sendto_one(acptr, ":%s WALLOPS :%s", sender, message);
	}
    }
} /* send_operwall() */


/*
 * sendto_anywhere
 *
 * inputs	- pointer to dest client
 * 		- pointer to from client
 * 		- varags
 * output	- NONE
 * side effects	- less efficient than sendto_remote and sendto_one
 * 		  but useful when one does not know where target "lives"
 */
void
sendto_anywhere(struct Client *to, struct Client *from,
		const char *pattern, ...)
{
  int len;
  va_list args;
  char prefix[NICKLEN+HOSTLEN+USERLEN+5];	/* same as USERHOST_REPLYLEN */
  char buf[IRCD_BUFSIZE*2];

  va_start(args, pattern);
  len = send_format(buf, pattern, args);
  va_end(args);

  if(MyClient(to))
    {
      if(IsServer(from))
	{
	  ircsprintf(prefix,":%s ",
		     from->name);
	}
      else
	{
	  ircsprintf(prefix,":%s!%s@%s ",
		     from->name,
		     from->username,
		     from->host);
	}
    }
  else
    {
      ircsprintf(prefix,":%s ", from->name);
    }

  len = ircsprintf(sendbuf,"%s%s", prefix,buf);

  if(len > 510)
    {
      sendbuf[IRCD_BUFSIZE-2] = '\r';
      sendbuf[IRCD_BUFSIZE-1] = '\n';
      sendbuf[IRCD_BUFSIZE] = '\0';
      len = IRCD_BUFSIZE;
    }
      
  if(MyClient(to))
    send_message(to, (char *)sendbuf, len);
  else
    send_message_remote(to, from, (char *)sendbuf, len);
}

/*
 * sendto_realops_flags
 *
 *    Send to *local* ops only but NOT +s nonopers.
 */

void
sendto_realops_flags(int flags, const char *pattern, ...)

{
  int len;
  struct Client *cptr;
  char nbuf[IRCD_BUFSIZE*2];
  dlink_node *ptr;
  va_list args;

  va_start(args, pattern);
  len = send_format(nbuf, pattern, args);
  va_end(args);

  if(len > (512-60))
    {
      nbuf[512-60] = '\r';
      nbuf[512-59] = '\n';
      nbuf[512-58] = '\0';
    }

  if (flags == FLAGS_ALL)
    {
      for (ptr = oper_list.head; ptr; ptr = ptr->next)
	{
	  cptr = ptr->data;

	  if (SendServNotice(cptr))
	    {
	      (void)ircsprintf(sendbuf, ":%s NOTICE %s :*** Notice -- %s",
			       me.name,
			       cptr->name,
			       nbuf);

	      len = strlen(sendbuf);	/* XXX *sigh* */
	      send_message(cptr, (char *)sendbuf, len);
	    }
	}
    }
  else 
    {
      for (ptr = oper_list.head; ptr; ptr = ptr->next)
	{
	  cptr = ptr->data;

	  if(cptr->umodes & flags)
	    {
	      (void)ircsprintf(sendbuf, ":%s NOTICE %s :*** Notice -- %s",
			       me.name,
			       cptr->name,
			       nbuf);

	      send_message(cptr, (char *)sendbuf, len);
	    }
	}
    }
} /* sendto_realops_flags() */

/*
** ts_warn
**      Call sendto_realops, with some flood checking (at most 5 warnings
**      every 5 seconds)
*/
 
void
ts_warn(const char *pattern, ...)

{
  va_list args;
  char buf[LOG_BUFSIZE];
  static time_t last = 0;
  static int warnings = 0;
  time_t now;
 
  /*
  ** if we're running with TS_WARNINGS enabled and someone does
  ** something silly like (remotely) connecting a nonTS server,
  ** we'll get a ton of warnings, so we make sure we don't send
  ** more than 5 every 5 seconds.  -orabidoo
  */

  now = time(NULL);
  if (now - last < 5)
    {
      if (++warnings > 5)
        return;
    }
  else
    {
      last = now;
      warnings = 0;
    }

  va_start(args, pattern);
  (void)send_format(buf, pattern, args);
  va_end(args);

  sendto_realops_flags(FLAGS_ALL,"%s",buf);
  log(L_CRIT, buf);
} /* ts_warn() */


static int
send_format(char *sendbuf, const char *pattern, va_list args)
{
  int len; /* used for the length of the current message */

  len = vsprintf_irc(sendbuf, pattern, args);

  /*
   * from rfc1459
   *
   * IRC messages are always lines of characters terminated with a CR-LF
   * (Carriage Return - Line Feed) pair, and these messages shall not
   * exceed 512 characters in length,  counting all characters 
   * including the trailing CR-LF.
   * Thus, there are 510 characters maximum allowed
   * for the command and its parameters.  There is no provision for
   * continuation message lines.  See section 7 for more details about
   * current implementations.
   */

  /*
   * We have to get a \r\n\0 onto sendbuf[] somehow to satisfy
   * the rfc. We must assume sendbuf[] is defined to be 513
   * bytes - a maximum of 510 characters, the CR-LF pair, and
   * a trailing \0, as stated in the rfc. Now, if len is greater
   * than the third-to-last slot in the buffer, an overflow will
   * occur if we try to add three more bytes, if it has not
   * already occured. In that case, simply set the last three
   * bytes of the buffer to \r\n\0. Otherwise, we're ok. My goal
   * is to get some sort of vsnprintf() function operational
   * for this routine, so we never again have a possibility
   * of an overflow.
   * -wnder
   */
  if (len > 510)
    {
      sendbuf[IRCD_BUFSIZE-2] = '\r';
      sendbuf[IRCD_BUFSIZE-1] = '\n';
      sendbuf[IRCD_BUFSIZE] = '\0';
      len = IRCD_BUFSIZE;
    }
  else
    {
      sendbuf[len++] = '\r';
      sendbuf[len++] = '\n';
      sendbuf[len] = '\0';
    }

  return(len);
}
