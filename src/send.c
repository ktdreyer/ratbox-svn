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

#define NEWLINE "\r\n"
#define LOG_BUFSIZE 2048

static  char    sendbuf[2048];
static  int     send_message (struct Client *, char *, int);

static  void vsendto_prefix_one(register struct Client *, register struct Client *, const char *, va_list);
static  void vsendto_one(struct Client *, const char *, va_list);
static  void vsendto_realops(const char *, va_list);

static  unsigned long sentalong[MAXCONNECTIONS];
static unsigned long current_serial=0L;

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
  DBufClear(&to->recvQ);
  DBufClear(&to->sendQ);
  if (!IsPerson(to) && !IsUnknown(to) && !(to->flags & FLAGS_CLOSING))
    sendto_realops(notice, get_client_name(to, FALSE));
  
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
  if (to->from)
    to = to->from; /* shouldn't be necessary */

  if (IsMe(to))
    {
      sendto_realops("Trying to send to myself! [%s]", msg);
      return 0;
    }

  if (to->fd < 0)
    return 0; /* Thou shalt not write to closed descriptors */

  if (IsDead(to))
    return 0; /* This socket has already been marked as dead */

  if (DBufLength(&to->sendQ) > get_sendq(to))
    {
      if (IsServer(to))
        sendto_realops("Max SendQ limit exceeded for %s: %d > %d",
		       get_client_name(to, FALSE),
		       DBufLength(&to->sendQ), get_sendq(to));

      if (IsClient(to))
        to->flags |= FLAGS_SENDQEX;
      return dead_link(to, "Max Sendq exceeded");
    }
  else
    {
      if (len && !dbuf_put(&to->sendQ, msg, len))
        return dead_link(to, "Buffer allocation error for %s");
    }
    /*
    ** Update statistics. The following is slightly incorrect
    ** because it counts messages even if queued, but bytes
    ** only really sent. Queued bytes get updated in SendQueued.
    */
    to->sendM += 1;
    me.sendM += 1;

    /*
     * Now we register a write callback. We *could* try to write some
     * data to the FD, it'd be an optimisation, and we can deal with it
     * later.
     *     -- adrian
     */
    comm_setselect(to->fd, FDLIST_BUSYCLIENT, COMM_SELECT_WRITE,
      send_queued_write, to, 0);
    return 0;
} /* send_message() */

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
  int len, rlen;
  int more = NO;

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

  while (DBufLength(&to->sendQ) > 0) {
    msg = dbuf_map(&to->sendQ, &len);

    /* Returns always len > 0 */
    if ((rlen = deliver_it(to, msg, len)) < 0) {
      dead_link(to,"Write error to %s, closing link");
      return;
    }

    dbuf_delete(&to->sendQ, rlen);
    to->lastsq = DBufLength(&to->sendQ) / 1024;
    if (rlen < len) {    
      /* ..or should I continue until rlen==0? */
      /* no... rlen==0 means the send returned EWOULDBLOCK... */
      break;
    }

    if (DBufLength(&to->sendQ) == 0 && more) {
      /*
       * The sendQ is now emptry, to try send whats left
       * XXX uhm, huh? :-) This is a leftover from ziplinks .. :(
       *   -- adrian
       */
      if (!dbuf_put(&to->sendQ, msg, len)) {
        dead_link(to, "Buffer allocation error for %s");
        return;
      }
    } /* if (DBufLength(&to->sendQ) == 0 && more) */
  } /* while (DBufLength(&to->sendQ) > 0) */

  /* return (IsDead(to)) ? -1 : 0; */
  /* If we have any more data, reschedule a write */
  if (more)
      comm_setselect(fd, FDLIST_BUSYCLIENT, COMM_SELECT_WRITE,
        send_queued_write, to, 0);
} /* send_queued_write() */

/*
** send message to single client
*/

void
sendto_one(struct Client *to, const char *pattern, ...)

{
  va_list       args;

  va_start(args, pattern);

  vsendto_one(to, pattern, args);

  va_end(args);
} /* sendto_one() */

/*
 * vsendto_one()
 * Backend for sendto_one() - send string with variable
 * arguments to client 'to'
 * -wnder
*/

static void
vsendto_one(struct Client *to, const char *pattern, va_list args)

{
  int len; /* used for the length of the current message */
  
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
      sendto_realops("Trying to send [%s] to myself!", sendbuf);
      return;
    }

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

        Debug((DEBUG_SEND,"Sending [%s] to %s",sendbuf,to->name));

        (void)send_message(to, sendbuf, len);
} /* vsendto_one() */

void
sendto_channel_butone(struct Client *one, struct Client *from,
		      struct Channel *chptr, 
                      const char *pattern, ...)

{
  va_list       args;
  register struct SLink *lp;
  register struct Client *acptr;
  register int index; /* index of sentalong[] to flag client
		       * as having received message
		       */

  va_start(args, pattern);

  ++current_serial;
  
  for (lp = chptr->members; lp; lp = lp->next)
    {
      acptr = lp->value.cptr;
      
      if (acptr->from == one)
        continue;       /* ...was the one I should skip */
      
      index = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
          vsendto_prefix_one(acptr, from, pattern, args);
          sentalong[index] = current_serial;
        }
      else
        {
          /*
           * Now check whether a message has been sent to this
           * remote link already
           */
          if(sentalong[index] != current_serial)
            {
              vsendto_prefix_one(acptr, from, pattern, args);
              sentalong[index] = current_serial;
            }
        }
    }

        va_end(args);
} /* sendto_channel_butone() */

void
sendto_channel_type(struct Client *one, struct Client *from, struct Channel *chptr,
                    int type,
                    const char *nick,
                    const char *cmd,
                    const char *message)

{
  register struct SLink *lp;
  register struct Client *acptr;
  register int i;
  char char_type;

  ++current_serial;

  if(type&MODE_VOICE)
    char_type = '+';
  else
    char_type = '@';

  for (lp = chptr->members; lp; lp = lp->next)
    {
      if (!(lp->flags & type))
        continue;

      acptr = lp->value.cptr;
      if (acptr->from == one)
        continue;

      i = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
          sendto_prefix_one(acptr, from,
                  ":%s %s %c%s :%s",
                  from->name,
                  cmd,                    /* PRIVMSG or NOTICE */
                  char_type,              /* @ or + */
                  nick,
                  message);
        }
      else
        {
          /*
           * If the target's server can do CAP_CHW, only
           * one send is needed, otherwise, I do a bunch of
           * send's to each target on that server. (kludge)
           *
           * -Dianora
           */
          if(!IsCapable(acptr->from,CAP_CHW))
            {
              /* Send it individually to each opered or voiced
               * client on channel
               */
              if (sentalong[i] != current_serial)
                {
                  sendto_prefix_one(acptr, from,
                    ":%s NOTICE %s :%s",
                    from->name,
                    lp->value.cptr->name, /* target name */
                    message);
                }
              sentalong[i] = current_serial;
            }
          else
            {
              /* Now check whether a message has been sent to this
               * remote link already
               */
              if (sentalong[i] != current_serial)
                {
                  sendto_prefix_one(acptr, from,
                  ":%s NOTICE %c%s :%s",
                  from->name,
                  char_type,
                  nick,
                  message);
                  sentalong[i] = current_serial;
                }
            }
        }
      } /* for (lp = chptr->members; lp; lp = lp->next) */

} /* sendto_channel_type() */


/* 
** sendto_channel_type_notice()  - sends a message to all users on a channel who meet the
** type criteria (chanop/voice/whatever).
** message is also sent back to the sender if they have those privs.
** used in knock/invite/privmsg@+/notice@+
** -good
*/
void
sendto_channel_type_notice(struct Client *from, struct Channel *chptr,
			   int type, char *message)

{
        register struct SLink *lp;
        register struct Client *acptr;
        register int i;

        for (lp = chptr->members; lp; lp = lp->next)
        {
                if (!(lp->flags & type))
                        continue;

                acptr = lp->value.cptr;

                i = acptr->from->fd;
                if (IsRegisteredUser(acptr))
                {
                        sendto_prefix_one(acptr, from, ":%s NOTICE %s :%s",
                                from->name, 
                                acptr->name, message);
                }
        }
} /* sendto_channel_type_notice() */


/*
 * sendto_serv_butone
 *
 * Send a message to all connected servers except the client 'one'.
 */

void
sendto_serv_butone(struct Client *one, const char *pattern, ...)

{
  va_list args;
  register struct Client *cptr;

  /*
   * USE_VARARGS IS BROKEN someone volunteer to fix it :-) -Dianora
   *
   * fixed! :-)
   * -wnder
   */

  va_start(args, pattern);
  
  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (one && (cptr == one->from))
        continue;
      
      vsendto_one(cptr, pattern, args);
    }

  va_end(args);
} /* sendto_serv_butone() */

/*
 * sendto_common_channels()
 *
 * Sends a message to all people (excluding user) on local server who are
 * in same channel with user.
 */

void
sendto_common_channels(struct Client *user, const char *pattern, ...)

{
  va_list args;
  register struct SLink *channels;
  register struct SLink *users;
  register struct Client *cptr;

  va_start(args, pattern);
  
  ++current_serial;
  if (user->fd >= 0)
    sentalong[user->fd] = current_serial;

  if (user->user)
    {
      for (channels = user->user->channel; channels; channels = channels->next)
        for(users = channels->value.chptr->members; users; users = users->next)
          {
            cptr = users->value.cptr;
          /* "dead" clients i.e. ones with fd == -1 should not be
           * looked at -db
           */
            if (!MyConnect(cptr) || (cptr->fd < 0) ||
              (sentalong[cptr->fd] == current_serial))
              continue;
            
            sentalong[cptr->fd] = current_serial;
            
            vsendto_prefix_one(cptr, user, pattern, args);
          }
    }

  if (MyConnect(user))
    vsendto_prefix_one(user, user, pattern, args);

  va_end(args);
} /* sendto_common_channels() */

/*
 * sendto_channel_butserv
 *
 * Send a message to all members of a channel that are connected to this
 * server.
 */

void
sendto_channel_butserv(struct Channel *chptr, struct Client *from, 
                       const char *pattern, ...)

{
  va_list args;
  register struct SLink *lp;
  register struct Client *acptr;

  va_start(args, pattern);

  for (lp = chptr->members; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      vsendto_prefix_one(acptr, from, pattern, args);
  
  va_end(args);
} /* sendto_channel_butserv() */

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
 * sendto_match_servs
 *
 * send to all servers the channel given
 */

void
sendto_match_servs(struct Channel *chptr, struct Client *from, const char *pattern, ...)

{
  va_list args;
  register struct Client *cptr;
  
  va_start(args, pattern);

  if (chptr)
    {
      if (*chptr->chname == '&')
        return;
    }
  else
    return; /* an ooopsies */

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == from)
        continue;

      if(ConfigFileEntry.hub && IsCapable(cptr,CAP_LL))
        {
          if( !(chptr->lazyLinkChannelExists & cptr->serverMask) )
             continue;
        }

      vsendto_one(cptr, pattern, args);
    }

  va_end(args);
} /* sendto_match_servs() */

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
  va_list args;
  register struct Client *cptr;

  va_start(args, pattern);

  if (chptr)
    {
      if (*chptr->chname == '&')
        return;
    }

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == from)
        continue;
      
      if(!IsCapable(cptr, cap))
        continue;
      
      vsendto_one(cptr, pattern, args);
    }

  va_end(args);
} /* sendto_match_cap_servs() */

/*
 * sendto_match_butone
 *
 * Send to all clients which match the mask in a way defined on 'what';
 * either by user hostname or user servername.
 */

void
sendto_match_butone(struct Client *one, struct Client *from, char *mask, 
                    int what, const char *pattern, ...)

{
  va_list args;
  register struct Client *cptr;

  va_start(args, pattern);

  /* scan the local clients */
  for(cptr = LocalClientList; cptr; cptr = cptr->next_local_client)
    {
      if (cptr == one)  /* must skip the origin !! */
        continue;
      
      if (match_it(cptr, mask, what))
        vsendto_prefix_one(cptr, from, pattern, args);
    }

  /* Now scan servers */
  for (cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
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

      vsendto_prefix_one(cptr, from, pattern, args);
    } /* for (cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client) */

  va_end(args);
} /* sendto_match_butone() */

/*
** send_operwall -- Send Wallop to All Opers on this server
**
*/

void
send_operwall(struct Client *from, char *type_message, char *message)

{
  char sender[NICKLEN + USERLEN + HOSTLEN + 5];
  struct Client *acptr;
  struct User *user;
  
  if (!from || !message)
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
      for (acptr = oper_cptr_list; acptr; acptr = acptr->next_oper_client)
	{
	  if (!SendOperwall(acptr))
	    continue; /* has to be oper if in this linklist */
	  sendto_one(acptr, ":%s WALLOPS :%s %s", sender,
		     type_message, message);
	}
    }
  else
    {
      for (acptr = oper_cptr_list; acptr; acptr = acptr->next_oper_client)
	{
	  if (!SendOperwall(acptr))
	    continue; /* has to be oper if in this linklist */

	  sendto_one(acptr, ":%s WALLOPS :%s", sender, message);
	}
    }
} /* send_operwall() */

/*
 * to - destination client
 * from - client which message is from
 *
 * NOTE: NEITHER OF THESE SHOULD *EVER* BE NULL!!
 * -avalon
 *
 */

void
sendto_prefix_one(register struct Client *to, register struct Client *from, 
                  const char *pattern, ...)

{
  va_list args;

  va_start(args, pattern);

  vsendto_prefix_one(to, from, pattern, args);

  va_end(args);
} /* sendto_prefix_one() */

/*
 * vsendto_prefix_one()
 * Backend to sendto_prefix_one(). stdarg.h does not work
 * well when variadic functions pass their arguments to other
 * variadic functions, so we can call this function in those
 * situations.
 *  This function must ALWAYS be passed a string of the form:
 * ":%s COMMAND <other args>"
 * 
 * -wnder
 */

static void
vsendto_prefix_one(register struct Client *to, register struct Client *from,
                   const char *pattern, va_list args)

{
  static char sender[HOSTLEN + NICKLEN + USERLEN + 5];
  char* par = 0;
  register int parlen;
  register int len;
  static char sendbuf[1024];

  assert(0 != to);
  assert(0 != from);

/* Optimize by checking if (from && to) before everything */
  if (!MyClient(from) && IsPerson(to) && (to->from == from->from))
    {
      if (IsServer(from))
        {
          vsprintf_irc(sendbuf, pattern, args);
          
          sendto_realops(
                     "Send message (%s) to %s[%s] dropped from %s(Fake Dir)",
                     sendbuf, to->name, to->from->name, from->name);
          return;
        }

      sendto_realops("Ghosted: %s[%s@%s] from %s[%s@%s] (%s)",
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
  
  par = va_arg(args, char *);
  if (MyClient(to) && IsPerson(from) && !irccmp(par, from->name))
    {
      strcpy(sender, from->name);
      
      if (*from->username)
        {
          strcat(sender, "!");
          strcat(sender, from->username);
        }

      if (*from->host)
        {
          strcat(sender, "@");
          strcat(sender, from->host);
        }
      
      par = sender;
    } /* if (user) */

  *sendbuf = ':';
  strncpy_irc(sendbuf + 1, par, sizeof(sendbuf) - 2);

  parlen = strlen(par) + 1;
  sendbuf[parlen++] = ' ';

  len = parlen;
  len += vsprintf_irc(sendbuf + parlen, &pattern[4], args);

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

  Debug((DEBUG_SEND,"Sending [%s] to %s",sendbuf,to->name));

  send_message(to, sendbuf, len);
} /* vsendto_prefix_one() */

/*
 * sendto_realops
 *
 *    Send to *local* ops only but NOT +s nonopers.
 */

void
sendto_realops(const char *pattern, ...)

{
  va_list args;

  va_start(args, pattern);

  vsendto_realops(pattern, args);

  va_end(args);
} /* sendto_realops() */

/*
vsendto_realops()
 Send the given string to local operators (not +s)
*/

static void
vsendto_realops(const char *pattern, va_list args)

{
  register struct Client *cptr;
  char nbuf[1024];
  
  for (cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client)
    {
      if (SendServNotice(cptr))
        {
          (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
                           me.name, cptr->name);
          (void)strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
          
          vsendto_one(cptr, nbuf, args);
        }
    }
} /* vsendto_realops() */

/*
 * sendto_realops_flags
 *
 *    Send to *local* ops with matching flags
 */

void
sendto_realops_flags(int flags, const char *pattern, ...)

{
  va_list args;
  register struct Client *cptr;
  char nbuf[1024];

  va_start(args, pattern);

  for (cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client)
    {
      if(cptr->umodes & flags)
        {
          (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
                           me.name, cptr->name);
          (void)strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
          
          vsendto_one(cptr, nbuf, args);
        }
    } /* for (cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client) */

  va_end(args);
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

  va_start(args, pattern);
 
  /*
  ** if we're running with TS_WARNINGS enabled and someone does
  ** something silly like (remotely) connecting a nonTS server,
  ** we'll get a ton of warnings, so we make sure we don't send
  ** more than 5 every 5 seconds.  -orabidoo
  */

  /*
   * hybrid servers always do TS_WARNINGS -Dianora
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

  vsendto_realops(pattern, args);
  vsprintf(buf, pattern, args);
  log(L_CRIT, buf);
  va_end(args);
} /* ts_warn() */


#ifdef SLAVE_SERVERS

extern aConfItem *u_conf;

int
sendto_slaves(struct Client *one, char *message, char *nick, int parc, char *parv[])

{
  struct Client *acptr;
  aConfItem *aconf;

  for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client)
    {
      if (one == acptr)
        continue;
      
      for (aconf = u_conf; aconf; aconf = aconf->next)
        {
          if (match(acptr->name,aconf->name))
            { 
              if(parc > 3)
                sendto_one(acptr,":%s %s %s %s %s :%s",
                           me.name,
                           message,
                           nick,
                           parv[1],
                           parv[2],
                           parv[3]);
              else if(parc > 2)
                sendto_one(acptr,":%s %s %s %s :%s",
                           me.name,
                           message,
                           nick,
                           parv[1],
                           parv[2]);
              else if(parc > 1)
                sendto_one(acptr,":%s %s %s :%s",
                           me.name,
                           message,
                           nick,
                           parv[1]);
            } /* if (match(acptr->name,aconf->name)) */
        } /* for (aconf = u_conf; aconf; aconf = aconf->next) */
    } /* for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client) */

  return 0;
} /* sendto_slaves() */

#endif /* SLAVE_SERVERS */
