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

extern AuthPort *PortList;

static int ReadData(int sockfd);
static void ProcessData(int sockfd, int paramc, char **paramv);
static int EstablishConnection(AuthPort *portptr);
static void AddServer(Server *sptr);
static void DelServer(Server *sptr);

/*
 * List of all connected ircd servers
 */
Server *ServerList = NULL;

/*
 * List of authentication queries
 */
struct AuthRequest *AuthList = NULL;

static void StartAuth(int sockfd, int parc, char **parv);

static struct AuthCommandTable
{
	char *name; /* name of command */
	void (* func)(); /* function to call */
} AuthCommands[] = {
	{ "DoAuth", StartAuth },

	{ 0, 0 }
};

/*
AcceptAuthRequests()
 Enter a select() loop waiting for socket activity from one
of our ircd connections. Once we receive a request, process
it.
*/

void
AcceptAuthRequests()

{
	int activefds; /* result obtained from select() */
	fd_set readfds,
	       writefds;
	struct timeval TimeOut;
	AuthPort *portptr;
	Server *sptr,
	       *tmp;

	for (;;)
	{
		TimeOut.tv_sec = 1;
		TimeOut.tv_usec = 0;

		FD_ZERO(&readfds);
		FD_ZERO(&writefds);

		/*
		 * Check all of our listen ports
		 */
		for (portptr = PortList; portptr; portptr = portptr->next)
			if (portptr->sockfd != NOSOCK)
				FD_SET(portptr->sockfd, &readfds);

		/*
		 * Check all of our server connections
		 */
		for (sptr = ServerList; sptr; sptr = sptr->next)
			FD_SET(sptr->sockfd, &readfds);

		activefds = select(FD_SETSIZE, &readfds, &writefds, 0, &TimeOut);

		if (activefds > 0)
		{
			/* we got something */

			for (sptr = ServerList; sptr; sptr = sptr->next)
			{
				if (FD_ISSET(sptr->sockfd, &readfds))
				{
					fprintf(stderr, "socket [%d] is ready\n", sptr->sockfd);
					if (!ReadData(sptr->sockfd))
					{
						/*
						 * Connection has been closed
						 */

						close(sptr->sockfd);

						tmp = sptr->prev;

						DelServer(sptr);

						if (!tmp)
							break;
						else
							sptr = tmp;

						continue;
					}
				}
			}

			for (portptr = PortList; portptr; portptr = portptr->next)
			{
				if (portptr->sockfd == NOSOCK)
					continue;

				if (FD_ISSET(portptr->sockfd, &readfds))
				{
					/*
					 * We have received a new connection, possibly
					 * from an ircd - accept it
					 */
					if (!EstablishConnection(portptr))
						continue;
				}
			}
		}
		else if ((activefds == (-1)) && (errno != EINTR))
		{
			/*
			 * Not good - the connection was closed
			 */
		}
	} /* for (;;) */
} /* AcceptAuthRequests() */

/*
ReadData()
 Read and parse any incoming data from sockfd

Return: 0 if unsuccessful
        1 if successful
        2 if the socket is ok, but there's no data to be read
*/

static int
ReadData(int sockfd)

{
	int length; /* number of bytes we read */
	char buffer[BUFSIZE + 1];
	register char *ch;
	char *paramv[MAXPARAMS + 1];
	int paramc;

	length = recv(sockfd, buffer, sizeof(buffer) - 1, 0);

	if ((length == (-1)) &&
			((errno == EWOULDBLOCK) || (errno == EAGAIN)))
		return 2; /* no data to read */

	if (length <= 0)
	{
		/*
		 * Connection closed
		 */
	#ifdef bingo
		log(L_ERROR, "IAuth: Read error from server: %s",
			strerror(errno));
	#endif
		return 0;
	}

	buffer[length] = '\0';

	fprintf(stderr, "%s", buffer);

	ch = buffer;

	/*
	 * The following routine works something like this:
	 * We receive a line from the ircd process, but we
	 * wish to break it up by inserting NULLs where
	 * there are spaces, and keeping pointers to the
	 * beginning of each "word". paramv[] will be
	 * our array of pointers and paramc will be the
	 * number of parameters we have in paramv[].
	 */

	paramc = 0;
	paramv[paramc++] = buffer;

	while (*ch)
	{
		register char tmp;

		tmp = *ch;

		if ((tmp == '\n') || (tmp == '\r'))
		{
			/*
			 * We've reached the end of our line - process it
			 */

			*ch = '\0';

			if (paramc)
				ProcessData(sockfd, paramc, paramv);

			break;
		}
		else
		{
			if (tmp == ' ')
			{
				*ch++ = '\0';
				paramv[paramc++] = ch;
			}
		}

		/*
		 * Advance ch to the next letter in buffer[]
		 */
		++ch;
	} /* while (*ch) */

	return 1;
} /* ReadData() */

/*
ProcessData()
 Inputs: sockfd - socket we read data from
         paramc - number of parameters in paramv[]
         paramv - array of pointers to individual parameters
                  sent by the parent ircd process

 Purpose: Process the data contained in paramv[] and call
          the corresponding function to handle it

 Return: none
*/

static void
ProcessData(int sockfd, int paramc, char **paramv)

{
	struct AuthCommandTable *cmdptr;

	for (cmdptr = AuthCommands; cmdptr->name; cmdptr++)
	{
		if (!strcasecmp(paramv[0], cmdptr->name))
		{
			/*
			 * Call the corresponding function
			 */
			(*cmdptr->func)(sockfd, paramc, paramv);
			break;
		}
	}
} /* ProcessData() */

/*
EstablishConnection()
 Accept the new connection which has occured on portptr->port -
most likely from an ircd connection
*/

static int
EstablishConnection(AuthPort *portptr)

{
	struct sockaddr_in ClientAddr;
	int clientlen;
	Server *newconn;

	assert(portptr != 0);

	fprintf(stderr, "Got connection\n");

	newconn = (Server *) malloc(sizeof(Server));

	clientlen = sizeof(ClientAddr);
	newconn->sockfd = accept(portptr->sockfd, (struct sockaddr *) &ClientAddr, &clientlen);

	if (newconn->sockfd < 0)
	{
	#ifdef bingo
		log(L_ERROR, "EstablishConnection(): Error accepting connection: %s",
			strerror(errno));
	#endif
		free(newconn);
		return 0;
	}

	if (!SetNonBlocking(newconn->sockfd))
	{
	#ifdef bingo
		log(L_ERROR, "EstablishConnection(): Unable to set socket [%d] non-blocking",
			newconn->sockfd);
	#endif
		free(newconn);
		return 0;
	}

	/*
	 * All of newconn's fields have been filled in, just add it
	 * to the list
	 */
	AddServer(newconn);

	return 1;
} /* EstablishConnection() */

/*
AddServer()
 Insert 'sptr' into the beginning of ServerList;
*/

static void
AddServer(Server *sptr)

{
	assert(sptr != 0);

	sptr->prev = NULL;
	sptr->next = ServerList;
	if (sptr->next)
		sptr->next->prev = sptr;
	ServerList = sptr;
} /* AddServer() */

/*
DelServer()
 Delete 'sptr' from ServerList
*/

static void
DelServer(Server *sptr)

{
	assert(sptr != 0);

	if (sptr->next)
		sptr->next->prev = sptr->prev;
	if (sptr->prev)
		sptr->prev->next = sptr->next;
	else
		ServerList = sptr->next;

	free(sptr);
} /* DelServer() */

/*
CreateAuth()
 Allocate a new AuthRequest structure and return a pointer
to it.
*/

static struct AuthRequest *
CreateAuth()

{
	struct AuthRequest *request;

	request = (struct AuthRequest *) malloc(sizeof(struct AuthRequest));

	memset(request, 0, sizeof(struct AuthRequest));

	request->identfd = NOSOCK;
	request->timeout = time(NULL) + CONNECTTIMEOUT;

	return (request);
} /* CreateAuth() */

/*
RequestIdent()
 Begin an ident query for the given auth structure
*/

static void
RequestIdent(struct AuthRequest *auth)

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
			"RequestIdent(): Unable to open stream socket: %s",
			strerror(errno));
	#endif
		auth->flags |= AM_ID_FAILED;
		return;
	}

	if (!SetNonBlocking(fd))
	{
	#ifdef bingo
		log("RequestIdent(): Unable to set socket [%d] non-blocking",
			auth->identfd);
	#endif
		close(fd);
		auth->flags |= AM_ID_FAILED;
		return;
	}

	length = sizeof(struct sockaddr_in);
} /* RequestIdent() */

/*
StartAuth()
 Begin the authentication process

 parv[0] = "DoAuth"
 parv[1] = Client ID
 parv[2] = Client IP Address
*/

static void
StartAuth(int sockfd, int parc, char **parv)

{
	struct AuthRequest *auth;

	auth = CreateAuth();
	auth->serverfd = sockfd;
	strcpy(auth->clientid, parv[1]);

#if 0
	/*
	 * Begin ident query
	 */
	RequestIdent(auth);
#endif

	tosock(auth->serverfd,
		"DoneAuth %s %s %s\n",
		auth->clientid,
		"unknown",
		parv[2]);
} /* StartAuth() */
