/************************************************************************
 *   IRC - Internet Relay Chat, src/s_auth.c
 *   Copyright (C) 1992 Darren Reed
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
 *
 * Changes:
 *   July 6, 1999 - Rewrote most of the code here. When a client connects
 *     to the server and passes initial socket validation checks, it
 *     is owned by this module (auth) which returns it to the rest of the
 *     server when dns and auth queries are finished. Until the client is
 *     released, the server does not know it exists and does not process
 *     any messages from it.
 *     --Bleep  Thomas Helvey <tomh@inxpress.net>
 */
#include "s_auth.h"
#include "blalloc.h"
#include "client.h"
#include "common.h"
#include "event.h"
#include "fdlist.h"              /* fdlist_add */
#include "irc_string.h"
#include "ircd.h"
#include "ircdauth.h"
#include "numeric.h"
#include "packet.h"
#include "res.h"
#include "s_bsd.h"
#include "s_log.h"
#include "s_stats.h"
#include "send.h"

#include <netdb.h>               /* struct hostent */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include "memdebug.h"

/*
 * a bit different approach
 * this replaces the original sendheader macros
 */
static struct {
  const char* message;
  size_t      length;
} HeaderMessages [] = {
  /* 123456789012345678901234567890123456789012345678901234567890 */
  { "NOTICE AUTH :*** Looking up your hostname...\r\n",    46 },
  { "NOTICE AUTH :*** Found your hostname\r\n",            38 },
  { "NOTICE AUTH :*** Found your hostname, cached\r\n",    46 },
  { "NOTICE AUTH :*** Couldn't look up your hostname\r\n", 49 },
  { "NOTICE AUTH :*** Checking Ident\r\n",                 33 },
  { "NOTICE AUTH :*** Got Ident response\r\n",             37 },
  { "NOTICE AUTH :*** No Ident response\r\n",              36 },
  { "NOTICE AUTH :*** Your forward and reverse DNS do not match, " \
    "ignoring hostname.\r\n",                              80 }
};

typedef enum {
  REPORT_DO_DNS,
  REPORT_FIN_DNS,
  REPORT_FIN_DNSC,
  REPORT_FAIL_DNS,
  REPORT_DO_ID,
  REPORT_FIN_ID,
  REPORT_FAIL_ID,
  REPORT_IP_MISMATCH
} ReportType;

#define sendheader(c, r) \
   send((c)->fd, HeaderMessages[(r)].message, HeaderMessages[(r)].length, 0)

struct AuthRequest* AuthPollList = 0; /* GLOBAL - auth queries pending io */

/*
 * Global - list of clients who have completed their authentication
 *          check, but must still complete their authorization checks
 */
struct AuthRequest *AuthClientList = NULL;

static EVH timeout_auth_queries_event;
static BlockHeap *auth_bl = NULL;

static PF read_auth_reply;
static CNCB auth_connect_callback;

/*
 * init_auth()
 *
 * Initialise the auth code
 */
void
init_auth(void)
{
  eventAdd("timeout_auth_queries_event", timeout_auth_queries_event, NULL,
    1, 0);
  auth_bl = BlockHeapCreate(sizeof(struct AuthRequest), AUTH_BLOCK_SIZE);
}

/*
 * make_auth_request - allocate a new auth request
 */
static struct AuthRequest* make_auth_request(struct Client* client)
{
  struct AuthRequest* request = (struct AuthRequest *)BlockHeapAlloc(auth_bl);
  assert(0 != request);
  memset(request, 0, sizeof(struct AuthRequest));
  request->fd      = -1;
  request->client  = client;
  request->timeout = CurrentTime + CONNECTTIMEOUT;
  return request;
}

/*
 * free_auth_request - cleanup auth request allocations
 */
void free_auth_request(struct AuthRequest* request)
{
    BlockHeapFree(auth_bl, request);
}

/*
 * unlink_auth_request - remove auth request from a list
 */
static void unlink_auth_request(struct AuthRequest* request,
                                struct AuthRequest** list)
{
  if (request->next)
    request->next->prev = request->prev;
  if (request->prev)
    request->prev->next = request->next;
  else
    *list = request->next;
}

/*
 * link_auth_request - add auth request to a list
 */
static void link_auth_request(struct AuthRequest* request,
                              struct AuthRequest** list)
{
  request->prev = 0;
  request->next = *list;
  if (*list)
    (*list)->prev = request;
  *list = request;
}

/*
 * release_auth_client - release auth client from auth system
 * this adds the client into the local client lists so it can be read by
 * the main io processing loop
 */
static void release_auth_client(struct Client* client)
{
  if (client->fd > highest_fd)
    highest_fd = client->fd;

  /*
   * When a client has auth'ed, we want to start reading what it sends
   * us. This is what read_packet() does.
   *     -- adrian
   */
  comm_setselect(client->fd, FDLIST_IDLECLIENT, COMM_SELECT_READ, read_packet,
    client, 0);
  client->localClient->allow_read = MAX_FLOOD_PER_SEC;
  comm_setflush(client->fd, 1, flood_recalc, client);
  add_client_to_list(client);
}
 
/*
 * auth_dns_callback - called when resolver query finishes
 * if the query resulted in a successful search, hp will contain
 * a non-null pointer, otherwise hp will be null.
 * set the client on it's way to a connection completion, regardless
 * of success of failure
 */
static void auth_dns_callback(void* vptr, struct DNSReply* reply)
{
  struct AuthRequest* auth = (struct AuthRequest*) vptr;

  ClearDNSPending(auth);
  if (reply)
    {
      struct hostent* hp = reply->hp;
      int i;
      /*
       * Verify that the host to ip mapping is correct both ways and that
       * the ip#(s) for the socket is listed for the host.
       */
      for (i = 0; hp->h_addr_list[i]; ++i)
	{
	  if (0 == memcmp(hp->h_addr_list[i],
			  (char*) &auth->client->localClient->ip,
			  sizeof(struct in_addr)))
	    break;
	}
      if (!hp->h_addr_list[i])
	sendheader(auth->client, REPORT_IP_MISMATCH);
      else
	{
	  ++reply->ref_count;
	  auth->client->localClient->dns_reply = reply;
	  strncpy_irc(auth->client->host, hp->h_name, HOSTLEN);
	  sendheader(auth->client, REPORT_FIN_DNS);
	}
    }
  else
    {
      /*
       * this should have already been done by s_bsd.c in add_connection
       */
      strcpy(auth->client->host, auth->client->localClient->sockhost);
      sendheader(auth->client, REPORT_FAIL_DNS);
    }
  auth->client->host[HOSTLEN] = '\0';
  if (!IsDoingAuth(auth)) {
    release_auth_client(auth->client);
    unlink_auth_request(auth, &AuthPollList);
#ifdef USE_IAUTH
    link_auth_request(auth, &AuthClientList);
#endif
    /*free_auth_request(auth);*/
  }
}

/*
 * authsenderr - handle auth send errors
 */
static void auth_error(struct AuthRequest* auth)
{
  ++ServerStats->is_abad;

  fd_close(auth->fd);
  auth->fd = -1;

  ClearAuth(auth);
  sendheader(auth->client, REPORT_FAIL_ID);

  if (!IsDNSPending(auth))
  {
    unlink_auth_request(auth, &AuthPollList);
    release_auth_client(auth->client);
    link_auth_request(auth, &AuthClientList);
    /*free_auth_request(auth);*/
  }
}

/*
 * start_auth_query - Flag the client to show that an attempt to 
 * contact the ident server on
 * the client's host.  The connect and subsequently the socket are all put
 * into 'non-blocking' mode.  Should the connect or any later phase of the
 * identifing process fail, it is aborted and the user is given a username
 * of "unknown".
 */
static int start_auth_query(struct AuthRequest* auth)
{
  struct sockaddr_in sock;
  struct sockaddr_in localaddr;
  size_t             locallen = sizeof(struct sockaddr_in);
  int                fd;

  if ((fd = comm_open(AF_INET, SOCK_STREAM, 0, "ident")) == -1) {
    report_error("creating auth stream socket %s:%s", 
                 get_client_name(auth->client, TRUE), errno);
    log(L_ERROR, "Unable to create auth socket for %s:%m",
        get_client_name(auth->client, SHOW_IP));
    ++ServerStats->is_abad;
    return 0;
  }
  if ((MAXCONNECTIONS - 10) < fd) {
    sendto_realops_flags(FLAGS_ALL,"Can't allocate fd for auth on %s",
			 get_client_name(auth->client, TRUE));

    fd_close(fd);
    return 0;
  }

  sendheader(auth->client, REPORT_DO_ID);
  if (!set_non_blocking(fd)) {
    report_error(NONB_ERROR_MSG, get_client_name(auth->client, SHOW_IP), errno);
    fd_close(fd);
    return 0;
  }

  /* 
   * get the local address of the client and bind to that to
   * make the auth request.  This used to be done only for
   * ifdef VIRTTUAL_HOST, but needs to be done for all clients
   * since the ident request must originate from that same address--
   * and machines with multiple IP addresses are common now
   */
  memset(&localaddr, 0, locallen);
  getsockname(auth->client->fd, (struct sockaddr*) &localaddr, (int *)&locallen);
  localaddr.sin_port = htons(0);

  memcpy(&sock.sin_addr, &auth->client->localClient->ip,
	 sizeof(struct in_addr));
  auth->fd = fd;
  SetAuthConnect(auth);
  comm_connect_tcp(fd, inetntoa((char *)&auth->client->localClient->ip), 113, 
    (struct sockaddr *)&localaddr, locallen, auth_connect_callback, auth);

  return 1; /* We suceed here for now */
}

/*
 * GetValidIdent - parse ident query reply from identd server
 * 
 * Inputs        - pointer to ident buf
 * Output        - NULL if no valid ident found, otherwise pointer to name
 * Side effects        -
 */
static char* GetValidIdent(char *buf)
{
  int   remp = 0;
  int   locp = 0;
  char* colon1Ptr;
  char* colon2Ptr;
  char* colon3Ptr;
  char* commaPtr;
  char* remotePortString;

  /* All this to get rid of a sscanf() fun. */
  remotePortString = buf;
  
  colon1Ptr = strchr(remotePortString,':');
  if(!colon1Ptr)
    return 0;

  *colon1Ptr = '\0';
  colon1Ptr++;
  colon2Ptr = strchr(colon1Ptr,':');
  if(!colon2Ptr)
    return 0;

  *colon2Ptr = '\0';
  colon2Ptr++;
  commaPtr = strchr(remotePortString, ',');

  if(!commaPtr)
    return 0;

  *commaPtr = '\0';
  commaPtr++;

  remp = atoi(remotePortString);
  if(!remp)
    return 0;
              
  locp = atoi(commaPtr);
  if(!locp)
    return 0;

  /* look for USERID bordered by first pair of colons */
  if(!strstr(colon1Ptr, "USERID"))
    return 0;

  colon3Ptr = strchr(colon2Ptr,':');
  if(!colon3Ptr)
    return 0;
  
  *colon3Ptr = '\0';
  colon3Ptr++;
  return(colon3Ptr);
}

/*
 * start_auth - starts auth (identd) and dns queries for a client
 */
void start_auth(struct Client* client)
{
  struct DNSQuery     query;
  struct AuthRequest* auth = 0;

  assert(0 != client);

  auth = make_auth_request(client);

#if 0
  link_auth_request(auth, &AuthPollList);

	/*
	 *  Now we send the client's information to the IAuth
	 * server. The syntax for an authentication request is:
	 *
	 *      DoAuth <id> <ip address>
	 *        <id>         = unique id for this client so we
	 *                       can re-locate the client when
	 *                       the authentication completes.
	 *
	 *        <ip address> = client's ip address in long form.
	 *
	 *  The client's id will be the memory address of the
	 * client structure. This is acceptable because as long
	 * as the client exists, no other client can have the
	 * same memory address, therefore each client will have
	 * a unique id.
	 */

	SendIAuth("%s %p %u\n",
		IAS_DOAUTH,
		client,
		(unsigned int) client->ip.s_addr);

	/* IAuthQuery(client); */
#endif /* 0 */

  query.vptr     = auth;
  query.callback = auth_dns_callback;

  sendheader(client, REPORT_DO_DNS);

  /* No DNS cache now, remember? -- adrian */
  gethost_byaddr((const char*) &client->localClient->ip, &query);
  SetDNSPending(auth);
  start_auth_query(auth);
  link_auth_request(auth, &AuthPollList);
}

/*
 * timeout_auth_queries - timeout resolver and identd requests
 * allow clients through if requests failed
 */
static void
timeout_auth_queries_event(void *notused)
{
  struct AuthRequest* auth;
  struct AuthRequest* auth_next = 0;

  for (auth = AuthPollList; auth; auth = auth_next) {
    auth_next = auth->next;
    if (auth->timeout < CurrentTime) {
      if (-1 < auth->fd)
        fd_close(auth->fd);

      if (IsDoingAuth(auth))
        sendheader(auth->client, REPORT_FAIL_ID);
      if (IsDNSPending(auth)) {
        delete_resolver_queries(auth);
        sendheader(auth->client, REPORT_FAIL_DNS);
      }
      log(L_INFO, "DNS/AUTH timeout %s",
          get_client_name(auth->client, SHOW_IP));

      auth->client->since = CurrentTime;
      release_auth_client(auth->client);
      unlink_auth_request(auth, &AuthPollList);
  #ifdef USE_IAUTH
  	link_auth_request(auth, &AuthClientList);
  #else
    free_auth_request(auth);
  #endif
    }
  }

  /* And re-register an event .. */
  /* 
   * These *REALLY* should be part of the socket timeout, but we aren't
   * at that stage yet.   -- adrian
   */
  eventAdd("timeout_auth_queries_event", timeout_auth_queries_event, NULL,
    1, 0);
}

/*
 * auth_connect_callback() - deal with the result of comm_connect_tcp()
 *
 * If the connection failed, we simply close the auth fd and report
 * a failure. If the connection suceeded send the ident server a query
 * giving "theirport , ourport". The write is only attempted *once* so
 * it is deemed to be a fail if the entire write doesn't write all the
 * data given.  This shouldnt be a problem since the socket should have
 * a write buffer far greater than this message to store it in should
 * problems arise. -avalon
 */
static
void auth_connect_callback(int fd, int error, void *data)
{
  struct AuthRequest *auth = data;
  struct sockaddr_in us;
  struct sockaddr_in them;
  char            authbuf[32];
  size_t          ulen = sizeof(struct sockaddr_in);
  size_t          tlen = sizeof(struct sockaddr_in);

  /* Check the error */
  if (error != COMM_OK) {
    /* We had an error during connection :( */
    auth_error(auth);
    return;
  }

  if (getsockname(auth->client->fd, (struct sockaddr *)&us,   (int *) &ulen) ||
      getpeername(auth->client->fd, (struct sockaddr *)&them, (int *) &tlen)) {

    log(L_INFO, "auth get{sock,peer}name error for %s:%m",
        get_client_name(auth->client, SHOW_IP));
    auth_error(auth);
    return;
  }
  ircsprintf(authbuf, "%u , %u\r\n",
             (unsigned int) ntohs(them.sin_port),
             (unsigned int) ntohs(us.sin_port));

  if (send(auth->fd, authbuf, strlen(authbuf), 0) == -1) {
    auth_error(auth);
    return;
  }
  ClearAuthConnect(auth);
  SetAuthPending(auth);
  /* Its idle, because we don't mind this taking a little time -- adrian */
  comm_setselect(auth->fd, FDLIST_IDLECLIENT, COMM_SELECT_READ,
    read_auth_reply, auth, 0);
}


/*
 * read_auth_reply - read the reply (if any) from the ident server 
 * we connected to.
 * We only give it one shot, if the reply isn't good the first time
 * fail the authentication entirely. --Bleep
 */
#define AUTH_BUFSIZ 128

static void
read_auth_reply(int fd, void *data)
{
  struct AuthRequest *auth = data;
  char* s=(char *)NULL;
  char* t=(char *)NULL;
  int   len;
  int   count;
  char  buf[AUTH_BUFSIZ + 1]; /* buffer to read auth reply into */

  len = recv(auth->fd, buf, AUTH_BUFSIZ, 0);
  
  if (len > 0) {
    buf[len] = '\0';

    if( (s = GetValidIdent(buf)) ) {
      t = auth->client->username;
      for (count = USERLEN; *s && count; s++) {
        if(*s == '@') {
            break;
          }
        if ( !IsSpace(*s) && *s != ':' ) {
          *t++ = *s;
          count--;
        }
      }
      *t = '\0';
    }
  }

  fd_close(auth->fd);
  auth->fd = -1;
  ClearAuth(auth);
  
  if (!s) {
    ++ServerStats->is_abad;
    strcpy(auth->client->username, "unknown");
  }
  else {
    sendheader(auth->client, REPORT_FIN_ID);
    ++ServerStats->is_asuc;
    SetGotId(auth->client);
  }

  if (!IsDNSPending(auth))
  {
    unlink_auth_request(auth, &AuthPollList);
    release_auth_client(auth->client);
  #ifdef USE_IAUTH
  	link_auth_request(auth, &AuthClientList);
  #else
    free_auth_request(auth);
  #endif
  }
}

/*
remove_auth_request()
 Remove request 'auth' from AuthClientList, and free it.
There is no need to release auth->client since it has
already been done
*/

void
remove_auth_request(struct AuthRequest *auth)

{
	unlink_auth_request(auth, &AuthClientList);
	free_auth_request(auth);
} /* remove_auth_request() */

/*
FindAuthClient()
 Find the client matching 'id' in the AuthClientList. The
id will match the memory address of the client structure.
*/

struct AuthRequest *
FindAuthClient(long id)

{
	struct AuthRequest *auth;

	auth = AuthClientList;
	while (auth && !((void *) auth->client == (void *) id))
		auth = auth->next;

	return (auth);
} /* FindAuthClient() */
