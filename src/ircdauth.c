/************************************************************************
 *   IRC - Internet Relay Chat, src/ircdauth.c
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>

#include "class.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"
#include "irc_string.h"
#include "ircd_defs.h"
#include "ircd.h"
#include "ircdauth.h"
#include "list.h"
#include "listener.h"
#include "numeric.h"
#include "s_auth.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_user.h"
#include "send.h"

/*
 * This structure will contain the information for the IAuth
 * server.
 */
struct IrcdAuthentication iAuth;

/*
 * Contains data read from IAuth server
 */
static char buffer[BUFSIZE + 1];

/*
 * Array of pointers to each parameter IAuth sends
 */
static char               *param[MAXPARAMS + 1];
static int                paramc;               /* param count */
static char               *nextparam = NULL;    /* pointer to next parameter */

/*
 * If the data read into buffer[] contains several lines,
 * and the last one was cut off in the middle, store it into
 * spill[]. offset is the index of spill[] where we left
 * off.
 */
static char spill[BUFSIZE + 1];
int offset;

static void ProcessIAuthData(int parc, char **parv);
static void GoodAuth(int parc, char **parv);
static void GreetUser(struct Client *client);
static void BadAuth(int parc, char **parv);

/*
ConnectToIAuth()
 Attempt to connect to the Ircd Authentication server.
*/

int
ConnectToIAuth()

{
	struct sockaddr_in ServAddr;
	register struct hostent *hostptr;
	struct in_addr *ptr;

	if ((iAuth.socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		log(L_ERROR,
			"ConnectToIAuth(): Unable to open stream socket: %s",
			strerror(errno));
		iAuth.socket = NOSOCK;
		return(NOSOCK);
	}
        fd_open(iAuth.socket, FD_SOCKET, "iAuth socket");

	if ((hostptr = gethostbyname(iAuth.hostname)) == NULL)
	{
		log(L_ERROR,
			"Unable to connect to IAuth server: Unknown host");

		fd_close(iAuth.socket);
		iAuth.socket = NOSOCK;
		return(NOSOCK);
	}

	memset((void *) &ServAddr, 0, sizeof(ServAddr));

	ptr = (struct in_addr *) *hostptr->h_addr_list;
	ServAddr.sin_addr.s_addr = ptr->s_addr;

	ServAddr.sin_family = AF_INET;
	ServAddr.sin_port = htons(iAuth.port);

	if (!set_non_blocking(iAuth.socket))
	{
		log(L_ERROR,
			"ConnectToIAuth(): set_non_blocking() failed");
		fd_close(iAuth.socket);
		iAuth.socket = NOSOCK;
		return (NOSOCK);
	}

	if (connect(iAuth.socket, (struct sockaddr *) &ServAddr, sizeof(ServAddr)) < 0)
	{
		if (errno != EINPROGRESS)
		{
			log(L_ERROR,
				"Unable to connect to IAuth server: %s",
				strerror(errno));
			fd_close(iAuth.socket);
			iAuth.socket = NOSOCK;
			return(NOSOCK);
		}
	}

	SetIAuthConnect(iAuth);

	return(iAuth.socket);
} /* ConnectToIAuth() */

/*
CompleteIAuthConnection()
 Second portion of non-blocking connect sequence to IAuth.
Return: 1 if successful
        0 if unsuccessful
*/

int
CompleteIAuthConnection()

{
	int errval,
	    errlen;

	ClearIAuthConnect(iAuth);

	errval = 0;
	errlen = sizeof(errval);
	if (getsockopt(iAuth.socket, SOL_SOCKET, SO_ERROR, &errval, &errlen) < 0)
	{
		log(L_ERROR,
			"CompleteIAuthConnection(): getsockopt(SO_ERROR) failed: %s",
			strerror(errno));
		return 0;
	}

	if (errval > 0)
	{
		log(L_ERROR,
			"Connect to IAuth server (%s:%d) failed: %s",
			iAuth.hostname,
			iAuth.port,
			strerror(errval));
		return 0;
	}

	return 1;
} /* CompleteIAuthConnection() */

/*
BeginAuthorization()
 Called when a client connects and gives the USER/NICK combo -
send the client's information to the IAuth server to begin
an authorization. The syntax for an auth query is as follows:

    DoAuth <id> <nickname> <username> <hostname> <ip address> [password]

  <id>            - A unique ID for the client so when the
                    authentication completes, we can re-find
                    the client.

  <nickname>      - client's nickname

  <username>      - client's ident reply, or the username
                    given in the USER statement if ident
                    not available.

  <hostname>      - client's resolved hostname.

  <ip address>    - IP Address of the client. This is represented
                    in unsigned int form.

  [password]      - I-line password (if specified)
*/

void
BeginAuthorization(struct Client *client)

{
	char buf[BUFSIZE];
	int len;

	assert(iAuth.socket != NOSOCK);

	/*
	 * The client ID will be the memory address of the
	 * client. This is acceptable, because as long
	 * as the client exists, no other client will have
	 * the same address, thus ensuring a unique ID for
	 * each client.
	 */

	len = sprintf(buf,
		"DoAuth %p %s %s %s %u %s\n",
		client,
		client->name,
		client->username,
		client->host,
		(unsigned int) client->ip.s_addr,
		client->passwd);

	send(iAuth.socket, buf, len, 0);
} /* BeginAuthorization() */

/*
SendIAuth()
 Send a string to the iauth server
*/

void
SendIAuth(char *format, ...)

{
	va_list args;
	char buf[BUFSIZE];
	int len;

	assert(iAuth.socket != NOSOCK);

	va_start(args, format);

	len = vsprintf(buf, format, args);

	va_end(args);

	send(iAuth.socket, buf, len, 0);
} /* SendIAuth() */

/*
ParseIAuth()
 Read and parse any data from the IAuth server

Return: 0 if connection closed
        1 if successful
        2 if socket is ok, but there's nothing to read
*/

int
ParseIAuth()

{
	int length; /* number of bytes we read */
	register char *ch;
	register char *linech;

	/* read in a line */
	length = recv(iAuth.socket, buffer, BUFSIZE, 0);

	if ((length == (-1)) && ((errno == EWOULDBLOCK) || (errno == EAGAIN)))
		return 2; /* no error - there's just nothing to read */

	if (length <= 0)
	{
		log(L_ERROR, "Read error from server: %s",
			strerror(errno));
		return 0; /* the connection was closed */
	}

	/*
	 * buffer may not be big enough to read the whole last line
	 * so it may contain only part of it
	 */
	buffer[length] = '\0';

	fprintf(stderr, "%s", buffer);

	/*
	 * buffer could possibly contain several lines of info,
	 * especially if this is the inital connect burst, so go
	 * through, and record each line (until we hit a \n) and
	 * process it separately
	 */

	ch = buffer;
	linech = spill + offset;

	/*
	 * The following routine works something like this:
	 * buffer may contain several full lines, and then
	 * a partial line. If this is the case, loop through
	 * buffer, storing each character in 'spill' until
	 * we hit a \n or \r.  When we do, process the line.
	 * When we hit the end of buffer, spill will contain
	 * the partial line that buffer had, and offset will
	 * contain the index of spill where we left off, so the
	 * next time we recv() from the hub, the beginning
	 * characters of buffer will be appended to the end of
	 * spill, thus forming a complete line.
	 * If buffer does not contain a partial line, then
	 * linech will simply point to the first index of 'spill'
	 * (offset will be zero) after we process all of buffer's
	 * lines, and we can continue normally from there.
	 */

	while (*ch)
	{
		register char tmp;

		tmp = *ch;
		if (IsEol(tmp))
		{
			*linech = '\0';

			if (nextparam)
			{
				/*
				 * It is possible nextparam will not be NULL here
				 * if there is a line like:
				 * BadAuth id :Blah
				 * where the text after the colon does not have
				 * any spaces, so we reach the \n and do not
				 * execute the code below which sets the next
				 * index of param[] to nextparam. Do it here.
				 */
				param[paramc++] = nextparam;
			}

			/*
			 * Make sure paramc is non-zero, because if the line
			 * starts with a \n, we will immediately come here,
			 * without initializing param[0]
			 */
			if (paramc)
			{
				/* process the line */
				ProcessIAuthData(paramc, param);
			}

			linech = spill;
			offset = 0;
			paramc = 0;
			nextparam = NULL;

			/*
			 * If the line ends in \r\n, then this algorithm would
			 * have only picked up the \r. We don't want an entire
			 * other loop to do the \n, so advance ch here.
			 */
			if (IsEol(*(ch + 1)))
				ch++;
		}
		else
		{
			/* make sure we don't overflow spill[] */
			if (linech >= (spill + (sizeof(spill) - 1)))
			{
				ch++;
				continue;
			}

			*linech++ = tmp;
			if (tmp == ' ')
			{
				/*
				 * Only set the space character to \0 if this is
				 * the very first parameter, or if nextparam is
				 * not NULL. If nextparam is NULL, then we've hit
				 * a parameter that starts with a colon (:), so
				 * leave it as a whole parameter.
				 */
				if (nextparam || (paramc == 0))
					*(linech - 1) = '\0';

				if (paramc == 0)
				{
					/*
					 * Its the first parameter - set it to the beginning
					 * of spill
					 */
					param[paramc++] = spill;
					nextparam = linech;
				}
				else if (nextparam)
				{
					param[paramc++] = nextparam;
					if (*nextparam == ':')
					{
						/*
						 * We've hit a colon, set nextparam to NULL,
						 * so we know not to set any more spaces to \0
						 */
						nextparam = NULL;

						/*
						 * Unfortunately, the first space has already
						 * been set to \0 above, so reset to to a
						 * space character
						 */
						*(linech - 1) = ' ';
					}
					else
						nextparam = linech;

					if (paramc >= MAXPARAMS)
						nextparam = NULL;
				}
			}
			++offset;
		}

		/*
		 * Advance ch to go to the next letter in the buffer
		 */
		++ch;
	} /* while (*ch) */

	return 1;
} /* ParseIAuth() */

/*
ProcessIAuthData()
 Process the data send by the IAuth server
*/

static void
ProcessIAuthData(int parc, char **parv)

{
	int len;

	len = strlen(parv[0]);

	if (!strncasecmp(parv[0], "DoneAuth", len))
		GoodAuth(parc, parv);
	else if (!strncasecmp(parv[0], "BadAuth", len))
		BadAuth(parc, parv);
} /* ProcessIAuthData() */

/*
GoodAuth()
 Called when an authorization request succeeds - grant the
client access to the server

parv[0] = "DoneAuth"
parv[1] = client id
parv[2] = username
*/

static void
GoodAuth(int parc, char **parv)

{
	struct AuthRequest *auth;
	long id;

/*	assert(parc == 3); */

	id = strtol(parv[1], 0, 0);
	for (auth = AuthClientList; auth; auth = auth->next)
	{
		/*
		 * Remember: the client id is the memory address
		 * of auth->client, so if it matches id, we
		 * found our client.
		 */
		if ((void *) auth->client == (void *) id)
		{
			/*
			 * Use IAuth's username, because it may be different
			 * if ident failed, but the client's I: line specified
			 * no tilde character
			 */
			strncpy_irc(auth->client->username, parv[2], USERLEN);

			/*
			 * Register them
			 */
			GreetUser(auth->client);

			remove_auth_request(auth);

			break;
		}
	}
} /* GoodAuth() */

/*
GreetUser()
 Called after a user passes authorization - register them
and send them the motd
*/

static void
GreetUser(struct Client *client)

{
	/* bingo - FIX THIS */
	char *parv[3];
	static char ubuf[12];

	sendto_realops_flags(FLAGS_CCONN,
		"Client connecting: %s (%s@%s) [%s] {%d}",
		client->name,
		client->username,
		client->host,
		inetntoa((char *)&client->ip),
		get_client_class(client));

	if ((++Count.local) > Count.max_loc)
	{
		Count.max_loc = Count.local;
		if (!(Count.max_loc % 10))
			sendto_ops("New Max Local Clients: %d",
				Count.max_loc);
	}

	SetClient(client);

	client->servptr = find_server(client->user->server);
	if (!client->servptr)
	{
		sendto_ops("Ghost killed: %s on invalid server %s",
			client->name,
			client->user->server);

		sendto_one(client, ":%s KILL %s: %s (Ghosted, %s doesn't exist)",
			me.name,
			client->name,
			me.name,
			client->user->server);

		client->flags |= FLAGS_KILLED;

		exit_client(NULL, client, &me, "Ghost");
		return;
	}

	add_client_to_llist(&(client->servptr->serv->users), client);

	/* Increment our total user count here */
	if (++Count.total > Count.max_tot)
		Count.max_tot = Count.total;

	sendto_one(client, form_str(RPL_WELCOME),
		me.name,
		client->name,
		client->name);

	/* This is a duplicate of the NOTICE but see below...*/
	sendto_one(client, form_str(RPL_YOURHOST),
		me.name,
		client->name,
		get_listener_name(client->listener), version);
      
	/*
	** Don't mess with this one - IRCII needs it! -Avalon
	*/
	sendto_one(client,
		"NOTICE %s :*** Your host is %s, running version %s",
		client->name,
		get_listener_name(client->listener),
		version);

	sendto_one(client, form_str(RPL_CREATED),
		me.name,
		client->name,
		creation);

	sendto_one(client, form_str(RPL_MYINFO),
		me.name,
		client->name,
		me.name,
		version);

	parv[0] = client->name;
	parv[1] = parv[2] = NULL;

	show_lusers(client, client, 1, parv);

  if(ConfigFileEntry.short_motd) {
  	sendto_one(client,"NOTICE %s :*** Notice -- motd was last changed at %s",
	  	client->name,
		  ConfigFileEntry.motd.lastChangedDate);

  	sendto_one(client,
	  	"NOTICE %s :*** Notice -- Please read the motd if you haven't read it",
		  client->name);

	  sendto_one(client, form_str(RPL_MOTDSTART),
		  me.name,
		  client->name,
		  me.name);

	  sendto_one(client,
		  form_str(RPL_MOTD),
		  me.name,
		  client->name,
		  "*** This is the short motd ***");

	  sendto_one(client, form_str(RPL_ENDOFMOTD),
		  me.name,
		  client->name);

  } else
	SendMessageFile(client, &ConfigFileEntry.motd);

#ifdef LITTLE_I_LINES
	if (client->confs && client->confs->value.aconf &&
			(client->confs->value.aconf->flags & CONF_FLAGS_LITTLE_I_LINE))
	{
		SetRestricted(client);
		sendto_one(client,"NOTICE %s :*** Notice -- You are in a restricted access mode",
			client->name);

		sendto_one(client,"NOTICE %s :*** Notice -- You can not chanop others",
			client->name);
	}
#endif

	send_umode(NULL, client, 0, SEND_UMODES, ubuf);
	if (!*ubuf)
	{
		ubuf[0] = '+';
		ubuf[1] = '\0';
	}
  
  /* LINKLIST 
   * add to local client link list -Dianora
   * I really want to move this add to link list
   * inside the if (MyConnect(client)) up above
   * but I also want to make sure its really good and registered
   * local client
   *
   * double link list only for clients, traversing
   * a small link list for opers/servers isn't a big deal
   * but it is for clients -Dianora
   */

	if (LocalClientList)
		LocalClientList->previous_local_client = client;

	client->previous_local_client = NULL;
	client->next_local_client = LocalClientList;
	LocalClientList = client;

	sendto_serv_butone(client,
		"NICK %s %d %lu %s %s %s %s :%s",
		client->name,
		client->hopcount + 1,
		client->tsinfo,
		ubuf,
		client->username,
		client->host,
		client->user->server,
		client->info);

	if (ubuf[1])
		send_umode_out(client, client, 0);
} /* GreetUser() */

/*
BadAuth()
 Called when a client fails authorization - exit them

parv[0] = "BadAuth"
parv[1] = client id
parv[2] = :reason
*/

static void
BadAuth(int parc, char **parv)

{
	struct AuthRequest *auth;
	long id;

	id = strtol(parv[1], 0, 0);
	if ((auth = FindAuthClient(id)))
	{
		exit_client(auth->client, auth->client, &me,
			parv[2] + 1);
	}
} /* BadAuth() */
