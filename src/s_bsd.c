/************************************************************************
 *   IRC - Internet Relay Chat, src/s_bsd.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
 *  $Id$
 */
#include "fdlist.h"
#include "s_bsd.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "event.h"
#include "irc_string.h"
#include "ircdauth.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "numeric.h"
#include "packet.h"
#include "res.h"
#include "restart.h"
#include "s_auth.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_stats.h"
#include "send.h"
#include "s_debug.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/param.h>    /* NOFILE */
#include <arpa/inet.h>

#ifndef IN_LOOPBACKNET
#define IN_LOOPBACKNET        0x7f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

extern struct sockaddr_in vserv;               /* defined in s_conf.c */

const char* const NONB_ERROR_MSG   = "set_non_blocking failed for %s:%s"; 
const char* const OPT_ERROR_MSG    = "disable_sock_options failed for %s:%s";
const char* const SETBUF_ERROR_MSG = "set_sock_buffers failed for server %s:%s";

struct Client* local[MAXCONNECTIONS];

static char               readBuf[READBUF_SIZE];

static const char *comm_err_str[] = { "Comm OK", "Error during bind()",
  "Error during DNS lookup", "connect timeout", "Error during connect()",
  "Comm Error" };

static void comm_connect_callback(int fd, int status);
static PF comm_connect_timeout;
static void comm_connect_dns_callback(void *vptr, struct DNSReply *reply);
static PF comm_connect_tryconnect;

/* close_all_connections() can be used *before* the system come up! */

void close_all_connections(void)
{
  int i;
  for (i = 0; i < MAXCONNECTIONS; ++i) {
    if (fd_table[i].flags.open)
        fd_close(i);
    else
        close(i);
    local[i] = 0;
  }
}

/*
 * get_sockerr - get the error value from the socket or the current errno
 *
 * Get the *real* error from the socket (well try to anyway..).
 * This may only work when SO_DEBUG is enabled but its worth the
 * gamble anyway.
 */
int get_sockerr(int fd)
{
  int errtmp = errno;
#ifdef SO_ERROR
  int err = 0;
  int len = sizeof(err);

  if (-1 < fd && !getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*) &err, &len)) {
    if (err)
      errtmp = err;
  }
  errno = errtmp;
#endif
  return errtmp;
}

/*
 * report_error - report an error from an errno. 
 * Record error to log and also send a copy to all *LOCAL* opers online.
 *
 *        text        is a *format* string for outputing error. It must
 *                contain only two '%s', the first will be replaced
 *                by the sockhost from the cptr, and the latter will
 *                be taken from sys_errlist[errno].
 *
 *        cptr        if not NULL, is the *LOCAL* client associated with
 *                the error.
 *
 * Cannot use perror() within daemon. stderr is closed in
 * ircd and cannot be used. And, worse yet, it might have
 * been reassigned to a normal connection...
 * 
 * Actually stderr is still there IFF ircd was run with -s --Rodder
 */

void report_error(const char* text, const char* who, int error) 
{
  who = (who) ? who : "";

  sendto_realops_flags(FLAGS_DEBUG, text, who, strerror(error));

  log(L_ERROR, text, who, strerror(error));
}

/*
 * set_sock_buffers - set send and receive buffers for socket
 * returns true (1) if successful, false (0) otherwise
 */
int set_sock_buffers(int fd, int size)
{
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*) &size, sizeof(size)) ||
      setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*) &size, sizeof(size)))
    return 0;
  return 1;
}

/*
 * disable_sock_options - if remote has any socket options set, disable them 
 * returns true (1) if successful, false (0) otherwise
 */
int disable_sock_options(int fd)
{
#if defined(IP_OPTIONS) && defined(IPPROTO_IP)
  if (setsockopt(fd, IPPROTO_IP, IP_OPTIONS, NULL, 0))
    return 0;
#endif
  return 1;
}

/*
 * set_non_blocking - Set the client connection into non-blocking mode. 
 * If your system doesn't support this, you're screwed, ircd will run like
 * crap.
 * returns true (1) if successful, false (0) otherwise
 */
int set_non_blocking(int fd)
{
  /*
   * NOTE: consult ALL your relevant manual pages *BEFORE* changing
   * these ioctl's.  There are quite a few variations on them,
   * as can be seen by the PCS one.  They are *NOT* all the same.
   * Heed this well. - Avalon.
   */
  /* This portion of code might also apply to NeXT.  -LynX */
#ifdef NBLOCK_SYSV
  int res = 1;

  if (ioctl(fd, FIONBIO, &res) == -1)
    return 0;

#else /* !NBLOCK_SYSV */
  int nonb = 0;
  int res;

#ifdef NBLOCK_POSIX
  nonb |= O_NONBLOCK;
#endif
#ifdef NBLOCK_BSD
  nonb |= O_NDELAY;
#endif

  res = fcntl(fd, F_GETFL, 0);
  if (-1 == res || fcntl(fd, F_SETFL, res | nonb) == -1)
    return 0;
#endif /* !NBLOCK_SYSV */
  fd_table[fd].flags.nonblocking = 1;
  return 1;
}

/*
 * deliver_it
 *      Attempt to send a sequence of bytes to the connection.
 *      Returns
 *
 *      < 0     Some fatal error occurred, (but not EWOULDBLOCK).
 *              This return is a request to close the socket and
 *              clean up the link.
 *      
 *      >= 0    No real error occurred, returns the number of
 *              bytes actually transferred. EWOULDBLOCK and other
 *              possibly similar conditions should be mapped to
 *              zero return. Upper level routine will have to
 *              decide what to do with those unwritten bytes...
 *
 *      *NOTE*  alarm calls have been preserved, so this should
 *              work equally well whether blocking or non-blocking
 *              mode is used...
 */
int deliver_it(struct Client* cptr, const char* str, int len)
{
  int   retval;

  retval = send(cptr->fd, str, len, 0);
  /*
  ** Convert WOULDBLOCK to a return of "0 bytes moved". This
  ** should occur only if socket was non-blocking. Note, that
  ** all is Ok, if the 'write' just returns '0' instead of an
  ** error and errno=EWOULDBLOCK.
  **
  */
  if (retval < 0 && (errno == EWOULDBLOCK || errno == EAGAIN ||
                     errno == ENOBUFS))
    {
      retval = 0;
      cptr->flags |= FLAGS_BLOCKED;
      return(retval);  /* Just get out now... */
    }
  else if (retval > 0)
    {
      cptr->flags &= ~FLAGS_BLOCKED;
    }

  if (retval > 0)
    {
      cptr->sendB += retval;
      me.sendB += retval;
      if (cptr->sendB > 1023)
        {
          cptr->sendK += (cptr->sendB >> 10);
          cptr->sendB &= 0x03ff;        /* 2^10 = 1024, 3ff = 1023 */
        }
      else if (me.sendB > 1023)
        {
          me.sendK += (me.sendB >> 10);
          me.sendB &= 0x03ff;
        }
    }
  return(retval);
}

/*
 * close_connection
 *        Close the physical connection. This function must make
 *        MyConnect(cptr) == FALSE, and set cptr->from == NULL.
 */
void close_connection(struct Client *cptr)
{
  struct ConfItem *aconf;
  assert(0 != cptr);

  if (IsServer(cptr))
    {
      ServerStats->is_sv++;
      ServerStats->is_sbs += cptr->sendB;
      ServerStats->is_sbr += cptr->receiveB;
      ServerStats->is_sks += cptr->sendK;
      ServerStats->is_skr += cptr->receiveK;
      ServerStats->is_sti += CurrentTime - cptr->firsttime;
      if (ServerStats->is_sbs > 2047)
        {
          ServerStats->is_sks += (ServerStats->is_sbs >> 10);
          ServerStats->is_sbs &= 0x3ff;
        }
      if (ServerStats->is_sbr > 2047)
        {
          ServerStats->is_skr += (ServerStats->is_sbr >> 10);
          ServerStats->is_sbr &= 0x3ff;
        }
      /*
       * If the connection has been up for a long amount of time, schedule
       * a 'quick' reconnect, else reset the next-connect cycle.
       */
      if ((aconf = find_conf_exact(cptr->name, cptr->username,
                                   cptr->host, CONF_CONNECT_SERVER)))
        {
          /*
           * Reschedule a faster reconnect, if this was a automatically
           * connected configuration entry. (Note that if we have had
           * a rehash in between, the status has been changed to
           * CONF_ILLEGAL). But only do this if it was a "good" link.
           */
          aconf->hold = time(NULL);
          aconf->hold += (aconf->hold - cptr->since > HANGONGOODLINK) ?
            HANGONRETRYDELAY : ConfConFreq(aconf);
          if (nextconnect > aconf->hold)
            nextconnect = aconf->hold;
        }

    }
  else if (IsClient(cptr))
    {
      ServerStats->is_cl++;
      ServerStats->is_cbs += cptr->sendB;
      ServerStats->is_cbr += cptr->receiveB;
      ServerStats->is_cks += cptr->sendK;
      ServerStats->is_ckr += cptr->receiveK;
      ServerStats->is_cti += CurrentTime - cptr->firsttime;
      if (ServerStats->is_cbs > 2047)
        {
          ServerStats->is_cks += (ServerStats->is_cbs >> 10);
          ServerStats->is_cbs &= 0x3ff;
        }
      if (ServerStats->is_cbr > 2047)
        {
          ServerStats->is_ckr += (ServerStats->is_cbr >> 10);
          ServerStats->is_cbr &= 0x3ff;
        }
    }
  else
    ServerStats->is_ni++;
  
  if (cptr->dns_reply) {
    --cptr->dns_reply->ref_count;
    cptr->dns_reply = 0;
  }
  if (-1 < cptr->fd) {
    /* attempt to flush any pending dbufs. Evil, but .. -- adrian */
    if (!IsDead(cptr))
        send_queued_write(cptr->fd, cptr);
    local[cptr->fd] = NULL;
    fd_close(cptr->fd);
    cptr->fd = -1;
  }

  DBufClear(&cptr->sendQ);
  DBufClear(&cptr->recvQ);
  memset(cptr->passwd, 0, sizeof(cptr->passwd));
  /*
   * clean up extra sockets from P-lines which have been discarded.
   */
  if (cptr->listener) {
    assert(0 < cptr->listener->ref_count);
    if (0 == --cptr->listener->ref_count && !cptr->listener->active) 
      close_listener(cptr->listener);
    cptr->listener = 0;
  }

  det_confs_butmask(cptr, 0);
  cptr->from = NULL; /* ...this should catch them! >:) --msa */
}

/*
 * add_connection - creates a client which has just connected to us on 
 * the given fd. The sockhost field is initialized with the ip# of the host.
 * The client is sent to the auth module for verification, and not put in
 * any client list yet.
 */
void add_connection(struct Listener* listener, int fd)
{
  struct Client*           new_client;
  struct sockaddr_in addr;
  int                len = sizeof(struct sockaddr_in);

  assert(0 != listener);

#ifdef USE_IAUTH
  if (iAuth.socket == NOSOCK)
  {
    send(fd,
      "NOTICE AUTH :*** Ircd Authentication Server is temporarily down, please connect later\r\n",
      87,
      0);
    fd_close(fd);
    return;
  }
#endif

  /* 
   * get the client socket name from the socket
   * the client has already been checked out in accept_connection
   */
  if (getpeername(fd, (struct sockaddr*) &addr, &len)) {
    report_error("Failed in adding new connection %s :%s", 
                 get_listener_name(listener), errno);
    ServerStats->is_ref++;
    fd_close(fd);
    return;
  }

  new_client = make_client(NULL);

  /* 
   * copy address to 'sockhost' as a string, copy it to host too
   * so we have something valid to put into error messages...
   */
  strncpy_irc(new_client->sockhost, 
              inetntoa((char*) &addr.sin_addr), HOSTIPLEN);
  strcpy(new_client->host, new_client->sockhost);
  new_client->ip.s_addr = addr.sin_addr.s_addr;
  new_client->port      = ntohs(addr.sin_port);
  new_client->fd        = fd;

  new_client->listener  = listener;
  ++listener->ref_count;

  if (!set_non_blocking(new_client->fd))
    report_error(NONB_ERROR_MSG, get_client_name(new_client, TRUE), errno);
  if (!disable_sock_options(new_client->fd))
    report_error(OPT_ERROR_MSG, get_client_name(new_client, TRUE), errno);
  start_auth(new_client);
}

/*
 * parse_client_queued - parse client queued messages
 */
int parse_client_queued(struct Client* cptr)
{
  int dolen  = 0;

  while (DBufLength(&cptr->recvQ) && !NoNewLine(cptr) &&
         ((cptr->status < STAT_UNKNOWN) || (cptr->since - CurrentTime < 10))) {
    /*
     * If it has become registered as a Server
     * then skip the per-message parsing below.
     */
    if (IsServer(cptr)) {
      /* 
       * This is actually useful, but it needs the ZIP_FIRST
       * kludge or it will break zipped links  -orabidoo
       */
      dolen = dbuf_get(&cptr->recvQ, readBuf, READBUF_SIZE);

      if (0 == dolen)
        break;
      return dopacket(cptr, readBuf, dolen);
    }
    dolen = dbuf_getmsg(&cptr->recvQ, readBuf, READBUF_SIZE);
    /*
     * Devious looking...whats it do ? well..if a client
     * sends a *long* message without any CR or LF, then
     * dbuf_getmsg fails and we pull it out using this
     * loop which just gets the next 512 bytes and then
     * deletes the rest of the buffer contents.
     * -avalon
     */
    if (0 == dolen) {
      if (DBufLength(&cptr->recvQ) < 510) {
        cptr->flags |= FLAGS_NONL;
        break;
      }
      DBufClear(&cptr->recvQ);
      break;
    }
    else if (CLIENT_EXITED == client_dopacket(cptr, readBuf, dolen))
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
   * For server connections, we process as many as we can without
   * worrying about the time of day or anything :)
   */
  if (PARSE_AS_SERVER(cptr)) {
    done = dopacket(cptr, readBuf, length);
  } else {
    /*
     * Before we even think of parsing what we just read, stick
     * it on the end of the receive queue and do it when its
     * turn comes around.
     */
    if (!dbuf_put(&cptr->recvQ, readBuf, length)) {
      exit_client(cptr, cptr, cptr, "dbuf_put fail");
      return;
    }
    
    if (IsPerson(cptr) &&
        (ConfigFileEntry.no_oper_flood && !IsAnyOper(cptr)) &&
        DBufLength(&cptr->recvQ) > CLIENT_FLOOD) {
      exit_client(cptr, cptr, cptr, "Excess Flood");
      return;
    }
    parse_client_queued(cptr);
  }
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

void error_exit_client(struct Client* cptr, int error)
{
  /*
   * ...hmm, with non-blocking sockets we might get
   * here from quite valid reasons, although.. why
   * would select report "data available" when there
   * wasn't... so, this must be an error anyway...  --msa
   * actually, EOF occurs when read() returns 0 and
   * in due course, select() returns that fd as ready
   * for reading even though it ends up being an EOF. -avalon
   */
  char errmsg[255];
  int  current_error = get_sockerr(cptr->fd);

  Debug((DEBUG_ERROR, "READ ERROR: fd = %d %d %d",
         cptr->fd, current_error, error));
  if (IsServer(cptr) || IsHandshake(cptr))
    {
      int connected = CurrentTime - cptr->firsttime;
      
      if (0 == error)
        sendto_ops("Server %s closed the connection",
                   get_client_name(cptr, FALSE));
      else
        report_error("Lost connection to %s:%s", 
                     get_client_name(cptr, TRUE), current_error);
      sendto_ops("%s had been connected for %d day%s, %2d:%02d:%02d",
                 cptr->name, connected/86400,
                 (connected/86400 == 1) ? "" : "s",
                 (connected % 86400) / 3600, (connected % 3600) / 60,
                 connected % 60);
    }
  if (0 == error)
    strcpy(errmsg, "Remote closed the connection");
  else
    ircsprintf(errmsg, "Read error: %d (%s)", 
               current_error, strerror(current_error));
  exit_client(cptr, cptr, &me, errmsg);
}

/*
 * stolen from squid - its a neat (but overused! :) routine which we
 * can use to see whether we can ignore this errno or not. It is
 * generally useful for non-blocking network IO related errnos.
 *     -- adrian
 */
int
ignoreErrno(int ierrno)
{
    switch (ierrno) {
    case EINPROGRESS:
    case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
    case EAGAIN:
#endif
    case EALREADY:
    case EINTR:
#ifdef ERESTART
    case ERESTART:
#endif
        return 1;
    default:
        return 0;
    }
    /* NOTREACHED */
}


/*
 * comm_settimeout() - set the socket timeout
 *
 * Set the timeout for the fd
 */
void
comm_settimeout(int fd, time_t timeout, PF *callback, void *cbdata)
{
    assert(fd > -1);
    assert(fd_table[fd].flags.open);

    fd_table[fd].timeout = CurrentTime + timeout;
    fd_table[fd].timeout_handler = callback;
    fd_table[fd].timeout_data = cbdata;
}


/*
 * comm_checktimeouts() - check the socket timeouts
 *
 * All this routine does is call the given callback/cbdata, without closing
 * down the file descriptor. When close handlers have been implemented,
 * this will happen.
 */
void
comm_checktimeouts(void *notused)
{
    int fd;
    PF *hdl;

    for (fd = 0; fd <= highest_fd; fd++) {
        if (!fd_table[fd].flags.open)
            continue;
        if (fd_table[fd].flags.closing)
            continue;
        if (fd_table[fd].timeout_handler &&
            fd_table[fd].timeout > 0 && fd_table[fd].timeout < CurrentTime) {
            /* Call timeout handler */
            hdl = fd_table[fd].timeout_handler;
            hdl(fd, fd_table[fd].timeout_data);           
            /* .. and clear .. */
            comm_settimeout(fd, 0, NULL, NULL);
        }
    }
    /* .. next .. */
    eventAdd("comm_checktimeouts", comm_checktimeouts, NULL, 1, 0);
}


/*
 * comm_connect_tcp() - connect a given socket to a remote address
 *
 * Begin the process of connecting a socket to a remote host/port.
 * Pass a 'local' address to bind to, a remote host/port, and a callback
 * for completion.
 * This routine binds the socket locally if required, and then formulate
 * a DNS request.
 */
void
comm_connect_tcp(int fd, const char *host, u_short port, 
    struct sockaddr *local, int socklen, CNCB *callback, void *data)
{
    struct DNSQuery query;

    fd_table[fd].flags.called_connect = 1;
    fd_table[fd].connect.callback = callback;
    fd_table[fd].connect.data = data;
    fd_table[fd].connect.port = port;

    /*
     * Note that we're using a passed sockaddr here. This is because
     * generally you'll be bind()ing to a sockaddr grabbed from
     * getsockname(), so this makes things easier.
     * XXX If NULL is passed as local, we should later on bind() to the
     * virtual host IP, for completeness.
     *   -- adrian
     */
    if ((local != NULL) && (bind(fd, local, socklen) < 0)) { 
        /* Failure, call the callback with COMM_ERR_BIND */
        comm_connect_callback(fd, COMM_ERR_BIND);
        /* ... and quit */
        return;
    }

    /*
     * Next, if we have been given an IP, get the addr and skip the
     * DNS check (and head direct to comm_connect_tryconnect().
     */
    if ((fd_table[fd].connect.hostaddr.s_addr = inet_addr(host))
       == INADDR_NONE) {
        /* Send the DNS request, for the next level */
        query.vptr = &fd_table[fd];
        query.callback = comm_connect_dns_callback;
        gethost_byname(host, &query);
    } else {
        /* We have a valid IP, so we just call tryconnect */
        /* Make sure we actually set the timeout here .. */
        comm_settimeout(fd, 30, comm_connect_timeout, NULL);
        comm_connect_tryconnect(fd, NULL);        
    }
}


/*
 * comm_connect_callback() - call the callback, and continue with life
 */
static void
comm_connect_callback(int fd, int status)
{
    CNCB *hdl;

    /* Clear the connect flag + handler */
    hdl = fd_table[fd].connect.callback;
    fd_table[fd].connect.callback = NULL;
    fd_table[fd].flags.called_connect = 0;

    /* Clear the timeout handler */
    comm_settimeout(fd, 0, NULL, NULL);

    /* Call the handler */
    hdl(fd, status, fd_table[fd].connect.data);

    /* Finish! */
}


/*
 * comm_connect_timeout() - this gets called when the socket connection
 * times out. This *only* can be called once connect() is initially
 * called ..
 */
static void
comm_connect_timeout(int fd, void *notused)
{
    /* error! */
    comm_connect_callback(fd, COMM_ERR_TIMEOUT);
}


/*
 * comm_connect_dns_callback() - called at the completion of the DNS request
 *
 * The DNS request has completed, so if we've got an error, return it,
 * otherwise we initiate the connect()
 */
static void
comm_connect_dns_callback(void *vptr, struct DNSReply *reply)
{
    fde_t *F = vptr;

    /* Error ? */
    if (reply == NULL) {
        /* Yes, callback + return */
        comm_connect_callback(F->fd, COMM_ERR_DNS);
        return;
    }

    /* No error, set a 10 second timeout */
    comm_settimeout(F->fd, 30, comm_connect_timeout, NULL);

    /* Copy over the DNS reply info so we can use it in the connect() */
    /*
     * Note we don't fudge the refcount here, because we aren't keeping
     * the DNS record around, and the DNS cache is gone anyway.. 
     *     -- adrian
     */
    memcpy(&F->connect.hostaddr, reply->hp->h_addr, sizeof(struct in_addr));

    /* Now, call the tryconnect() routine to try a connect() */
    comm_connect_tryconnect(F->fd, NULL);
}


/*
 * comm_connect_tryconnect() - called to attempt a connect()
 *
 * Attempt a connect(). If we get a non-fatal error, retry. If we get a fatal
 * error, callback with error. If we suceed, callback with OK.
 */
static void
comm_connect_tryconnect(int fd, void *notused)
{
    int retval;
    struct sockaddr_in sin;

    /* We *COULD* cache this in the fd_table for extra speed .. --adrian */
    sin.sin_addr.s_addr = fd_table[fd].connect.hostaddr.s_addr;
    sin.sin_port = htons(fd_table[fd].connect.port);
    sin.sin_family = AF_INET;

    /* Try the connect() */
    retval = connect(fd, (struct sockaddr *)&sin, sizeof(sin));

    /* Error? */
    if (retval < 0) {
        /*
         * If we get EISCONN, then we've already connect()ed the socket,
         * which is a good thing.
         *   -- adrian
         */
        if (errno == EISCONN)
            comm_connect_callback(fd, COMM_OK);
        else if (ignoreErrno(errno))
            /* Ignore error? Reschedule */
            comm_setselect(fd, FDLIST_SERVER, COMM_SELECT_WRITE,
              comm_connect_tryconnect, NULL, 0);
        else
            /* Error? Fail with COMM_ERR_CONNECT */
            comm_connect_callback(fd, COMM_ERR_CONNECT);
        return;
    }

    /* If we get here, we've suceeded, so call with COMM_OK */
    comm_connect_callback(fd, COMM_OK);
}


/*
 * comm_error_str() - return an error string for the given error condition
 */
const char *
comm_errstr(int error)
{
    if (error < 0 || error >= COMM_ERR_MAX)
        return "Invalid error number!";
    return comm_err_str[error];
}


/*
 * comm_open() - open a socket
 *
 * This is a highly highly cut down version of squid's comm_open() which
 * for the most part emulates socket(), *EXCEPT* it fails if we're about
 * to run out of file descriptors.
 */
int
comm_open(int family, int sock_type, int proto, const char *note)
{
    int fd;
    /* First, make sure we aren't going to run out of file descriptors */
    if (number_fd >= MASTER_MAX)
        return ENFILE;

    /*
     * Next, we try to open the socket. We *should* drop the reserved FD
     * limit if/when we get an error, but we can deal with that later.
     * XXX !!! -- adrian
     */
    fd = socket(family, sock_type, proto);
    if (fd < 0)
        return -1; /* errno will be passed through, yay.. */

    /* Set the socket non-blocking, and other wonderful bits */
    if (!set_non_blocking(fd)) {
        log(L_CRIT, "comm_open: Couldn't set FD %d non blocking!", fd);
        close(fd);
        return -1;
    }

    /* Next, update things in our fd tracking */
    fd_open(fd, FD_SOCKET, note);

    return fd;
}


/*
 * comm_accept() - accept an incoming connection
 *
 * This is a simple wrapper for accept() which enforces FD limits like
 * comm_open() does.
 */
int
comm_accept(int fd, struct sockaddr *pn, socklen_t *addrlen)
{
    int new;

    if (number_fd >= MASTER_MAX)
        return ENFILE;

    /*
     * Next, do the accept(). if we get an error, we should drop the
     * reserved fd limit, but we can deal with that when comm_open()
     * also does it. XXX -- adrian
     */
    new = accept(fd, pn, addrlen);
    if (new < 0)
        return -1;

    /* Set the socket non-blocking, and other wonderful bits */
    if (!set_non_blocking(new)) {
        log(L_CRIT, "comm_accept: Couldn't set FD %d non blocking!", new);
        close(new);
        return -1;
    }

    /* Next, tag the FD as an incoming connection */
    fd_open(new, FD_SOCKET, "Incoming connection");

    /* .. and return */
    return new;
}
