/************************************************************************
 *   IRC - Internet Relay Chat, iauth/auth.c
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

#include "headers.h"
#include "res.h"

#include <netdb.h>

static struct AuthRequest *CreateAuthRequest();
static void FreeAuthRequest(struct AuthRequest *request);
static void LinkAuthRequest(struct AuthRequest *request, struct AuthRequest **list);
static void UnlinkAuthRequest(struct AuthRequest *request, struct AuthRequest **list);

static int BeginIdentQuery(struct AuthRequest *auth);
static char *GetValidIdent(char *buf);

static void BeginDNSQuery(struct AuthRequest *auth);
static void AuthDNSCallback(void* vptr, struct DNSReply* reply);

static void CompleteAuthRequest(struct AuthRequest *auth);

/*
 * List of pending authentication queries
 */
struct AuthRequest *AuthPollList = NULL;

/*
 * List of incomplete authentication queries
 */
struct AuthRequest *AuthIncompleteList = NULL;

/*
StartAuth()
 Begin the authentication process

 parv[0] = "DoAuth"
 parv[1] = Client ID
 parv[2] = Client IP Address in unsigned int format
 parv[3] = Client's remote port
 parv[4] = Server's local port
*/

void
StartAuth(int sockfd, int parc, char **parv)

{
	struct AuthRequest *auth;

	if (parc < 5)
		return; /* paranoia */

	auth = CreateAuthRequest();

	auth->ip.s_addr = (unsigned int) atol(parv[2]);

	/*
	 * If the DNS query fails, we will use the client's
	 * ip address
	 * bingo - the dns routine should do this when it fails
	 */
	strcpy(auth->hostname, inet_ntoa(auth->ip));

	if (strlen(parv[1]) > IDLEN)
	{
		/*
		 * This should never happen, but just to be paranoid,
		 * cancel the auth request
		 */
		tosock(auth->serverfd,
			"DoneAuth %s ~ %s\n",
			parv[1],
			auth->hostname);

		FreeAuthRequest(auth);
		return;
	}

	strcpy(auth->clientid, parv[1]);
	auth->remoteport = (unsigned int) atoi(parv[3]);
	auth->localport = (unsigned int) atoi(parv[4]);

	auth->serverfd = sockfd;

	/*
	 * Begin dns query
	 */
	BeginDNSQuery(auth);

	/*
	 * Begin ident query
	 */
	if (BeginIdentQuery(auth))
		LinkAuthRequest(auth, &AuthPollList);
	else if (IsDNSPending(auth))
		LinkAuthRequest(auth, &AuthIncompleteList);
	else
	{
		CompleteAuthRequest(auth);
		FreeAuthRequest(auth);
	}
} /* StartAuth() */

/*
CreateAuthRequest()
 Allocate a new AuthRequest structure and return a pointer
to it.
*/

static struct AuthRequest *
CreateAuthRequest()

{
	struct AuthRequest *request;

	request = (struct AuthRequest *) malloc(sizeof(struct AuthRequest));

	memset(request, 0, sizeof(struct AuthRequest));

	request->identfd = NOSOCK;
	request->timeout = time(NULL) + CONNECTTIMEOUT;

	return (request);
} /* CreateAuthRequest() */

/*
FreeAuthRequest()
 Free the given auth request
*/

static void
FreeAuthRequest(struct AuthRequest *request)

{
	free(request);
} /* FreeAuthRequest() */

/*
LinkAuthRequest()
 Link auth request to the specified list
*/

static void
LinkAuthRequest(struct AuthRequest *request, struct AuthRequest **list)

{
	request->prev = NULL;
	request->next = *list;

	if (*list)
		(*list)->prev = request;
	*list = request;
} /* LinkAuthRequest() */

/*
UnlinkAuthRequest()
 Remove auth request from the specified list
*/

static void
UnlinkAuthRequest(struct AuthRequest *request, struct AuthRequest **list)

{
	if (request->next)
		request->next->prev = request->prev;

	if (request->prev)
		request->prev->next = request->next;
	else
		*list = request->next;
} /* UnlinkAuthRequest() */

/*
BeginIdentQuery()
 Begin an ident query for the given auth structure
*/

static int
BeginIdentQuery(struct AuthRequest *auth)

{
	struct sockaddr_in remoteaddr,
	                   localaddr;
	int length;
	int fd;

	assert(auth != 0);

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
	#ifdef bingo
		log(L_ERROR,
			"BeginIdentQuery(): Unable to open stream socket: %s",
			strerror(errno));
	#endif
		return 0;
	}

	if (!SetNonBlocking(fd))
	{
	#ifdef bingo
		log("BeginIdentQuery(): Unable to set socket [%d] non-blocking",
			fd);
	#endif
		close(fd);
		return 0;
	}

	length = sizeof(struct sockaddr_in);
	memset((void *) &localaddr, 0, length);

	localaddr.sin_port = htons(0);

	if (bind(fd, (struct sockaddr *) &localaddr, sizeof(localaddr)) < 0)
	{
	#ifdef bingo
		log("BeginIdentQuery(): Unable to bind socket [%d]: %s",
			fd,
			strerror(errno));
	#endif
		close(fd);
		return 0;
	}

	memcpy(&remoteaddr.sin_addr, &auth->ip, sizeof(struct in_addr));

	remoteaddr.sin_port = htons(113);
	remoteaddr.sin_family = AF_INET;

	/*
	 * Now, attempt the connection
	 */
	if ((connect(fd, (struct sockaddr *) &remoteaddr, sizeof(remoteaddr)) == (-1)) &&
			(errno != EINPROGRESS))
	{
	#ifdef bingo
		log("BeginIdentQuery(): Unable to connect to ident port of %s: %s",
			inet_ntoa(auth->ip),
			strerror(errno));
	#endif
		close(fd);
		return 0;
	}

	auth->identfd = fd;

	SetIdentConnect(auth);

	return 1;
} /* BeginIdentQuery() */

/*
IdentError()
 An error has occured during the ident process - cleanup
*/

static void
IdentError(struct AuthRequest *auth)

{
	assert(auth != 0);

	close(auth->identfd);
	auth->identfd = NOSOCK;

	ClearAuth(auth);

	UnlinkAuthRequest(auth, &AuthPollList);

	if (IsDNSPending(auth))
		LinkAuthRequest(auth, &AuthIncompleteList);
	else
	{
		CompleteAuthRequest(auth);
		FreeAuthRequest(auth);
	}
} /* IdentError() */

/*
SendIdentQuery()
 Send an ident query to the auth client
*/

void
SendIdentQuery(struct AuthRequest *auth)

{
	char authbuf[32];

	assert(auth != 0);

	sprintf(authbuf, "%u , %u\r\n",
		auth->remoteport,
		auth->localport);

	if (send(auth->identfd, authbuf, strlen(authbuf), 0) == (-1))
	{
	#ifdef bingo
		log("SendIdentQuery(): Error sending ident request: %s",
			strerror(errno));
	#endif
		IdentError(auth);
		return;
	}

	ClearIdentConnect(auth);
	SetIdentPending(auth);
} /* SendIdentQuery() */

/*
ReadIdentReply()
 Read a client's ident reply. We only give it one shot - if
the reply is not valid, fail the authentication.
*/

void
ReadIdentReply(struct AuthRequest *auth)

{
	char buf[ID_BUFSIZE + 1];
	int len;
	char *s = NULL,
	     *t;
	int count;

	len = recv(auth->identfd, buf, ID_BUFSIZE, 0);

	if (len > 0)
	{
		buf[len] = '\0';

		if ((s = GetValidIdent(buf)))
		{
			t = auth->username;
			for (count = USERLEN; *s && count; s++)
			{
				if (*s == '@')
					break;

				if ( !isspace(*s) && *s != ':' )
				{
					*t++ = *s;
					--count;
				}
			}
			*t = '\0';
		}
	}

	close(auth->identfd);
	auth->identfd = NOSOCK;
	ClearAuth(auth);
  
	if (!s)
		strcpy(auth->username, "unknown");

	UnlinkAuthRequest(auth, &AuthPollList);

	if (IsDNSPending(auth))
		LinkAuthRequest(auth, &AuthIncompleteList);
	else
	{
		CompleteAuthRequest(auth);
		FreeAuthRequest(auth);
	}
} /* ReadIdentReply() */

/*
 * GetValidIdent - parse ident query reply from identd server
 * 
 * Inputs        - pointer to ident buf
 * Output        - NULL if no valid ident found, otherwise pointer to name
 * Side effects        -
 */

static char *
GetValidIdent(char *buf)

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
} /* GetValidIdent() */

/*
BeginDNSQuery()
 Initiate a non-blocking dns query for auth->ip
*/

static void
BeginDNSQuery(struct AuthRequest *auth)

{
	struct DNSQuery query;

	assert(auth != 0);

	query.vptr = auth;
	query.callback = AuthDNSCallback;

	auth->dns_reply = gethost_byaddr((char *) &auth->ip, &query);
	if (auth->dns_reply)
	{
		/*
		 * The client's ip was cached
		 */
		strncpy_irc(auth->hostname, auth->dns_reply->hp->h_name, HOSTLEN);
	}
	else
		SetDNSPending(auth);
} /* BeginDNSQuery() */

/*
 * AuthDNSCallback - called when resolver query finishes
 * if the query resulted in a successful search, hp will contain
 * a non-null pointer, otherwise hp will be null.
 * set the client on it's way to a connection completion, regardless
 * of success of failure
 */
static void
AuthDNSCallback(void* vptr, struct DNSReply* reply)

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
      if (0 == memcmp(hp->h_addr_list[i], (char*) &auth->ip, sizeof(struct in_addr)))
         break;
    }

    if (hp->h_addr_list[i])
    {
      auth->dns_reply = reply;
      strncpy_irc(auth->hostname, hp->h_name, HOSTLEN);
    }
    /* else IP Mismatch */
  }
  else
  {
    /*
     * DNS query failed - use the ip address as their hostname
     */
    strcpy(auth->hostname, inet_ntoa(auth->ip));
  }

  auth->hostname[HOSTLEN] = '\0';

  if (!IsDoingAuth(auth))
  {
    UnlinkAuthRequest(auth, &AuthIncompleteList);
    FreeAuthRequest(auth);
  }
} /* AuthDNSCallback() */

/*
CompleteAuthRequest()
 We've completed ident and dns authentication for this client.
Now we must determine if the client passes the other checks, and
if so, tell the client's server the client is acceptable. This
is done as follows:

    DoneAuth <ID> <username> <hostname>

    <ID>           - unique ID for the client
    <username>     - Client's username (ident reply)
    <hostname>     - Client's hostname

 If, however, the client fails one of the checks, a reply
will be given to the client's server of the form:

    BadAuth <ID> :<Reason>

    <ID>           - unique ID for the client
    <Reason>       - Reason the client failed authentication
*/

static void
CompleteAuthRequest(struct AuthRequest *auth)

{
	int badauth = 0;
	char buf[BUFSIZE],
	     reason[BUFSIZE];

	*reason = '\0';

	if (badauth)
	{
		sprintf(buf, "BadAuth %s :%s\n",
			auth->clientid,
			reason);
	}
	else
	{
		/*
		 * If the ident query failed, make their username "~",
		 * which will tell the ircd server to use the given
		 * ident in the USER statement.
		 */
		if (!*auth->username)
			strcpy(auth->username, "~");

		sprintf(buf, "DoneAuth %s %s %s\n",
			auth->clientid,
			auth->username,
			auth->hostname);
	}

	send(auth->serverfd, buf, strlen(buf), 0);
} /* CompleteAuthRequest() */
