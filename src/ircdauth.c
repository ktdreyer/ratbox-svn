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
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>

#include "common.h"
#include "client.h"
#include "ircd_defs.h"
#include "s_log.h"
#include "irc_string.h"
#include "s_bsd.h"
#include "ircdauth.h"
#include "s_auth.h"

/*
 * This structure will contain the information for the IAuth
 * server
 */
struct IrcdAuthentication iAuth;

static void ProcessIAuthData(int parc, char **parv);

/*
ConnectToIAuth()
 Attempt to connect to the Ircd Authentication server.
*/

int
ConnectToIAuth()

{
	struct sockaddr_in ServAddr;
	register struct hostent *hostptr;
	char ip[30];
	struct in_addr *ptr;

	if ((hostptr = gethostbyname(iAuth.hostname)) == NULL)
	{
		log(L_ERROR,
			"Unable to connect to IAuth server: Unknown host");
		iAuth.socket = NOSOCK;
		return(NOSOCK);
	}

	memset((void *) &ServAddr, 0, sizeof(ServAddr));

	ptr = (struct in_addr *) *hostptr->h_addr_list;
	strcpy(ip, inet_ntoa(*ptr));
	ServAddr.sin_addr.s_addr = inet_addr(ip);

	ServAddr.sin_family = AF_INET;
	ServAddr.sin_port = htons(iAuth.port);

	if ((iAuth.socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		log(L_ERROR,
			"ConnectToIAuth(): Unable to open stream socket");
		close(iAuth.socket);
		iAuth.socket = NOSOCK;
		return(NOSOCK);
	}

	if (connect(iAuth.socket, (struct sockaddr *) &ServAddr, sizeof(ServAddr)) < 0)
	{
		log(L_ERROR,
			"Unable to connect to IAuth server: %s",
			strerror(errno));
		close(iAuth.socket);
		iAuth.socket = NOSOCK;
		return(NOSOCK);
	}

	return(iAuth.socket);
} /* ConnectToIAuth() */

/*
IAuthQuery()
 Called when a client connects - send the client's information
to the IAuth server to begin an authentication. The syntax
for an authentication query is as follows:

    DoAuth <ID> <IP Address> <RemotePort> <LocalPort>

  <ID>            - A unique ID for the client so when the
                    authentication completes, we can re-find
                    the client.

  <IP Address>    - IP Address of the client. This is represented
                    in unsigned int form.

  <RemotePort>    - Client's remote port for the connection.

  <LocalPort>     - Port the client connected to us on.
*/

void
IAuthQuery(struct Client *client)

{
	char buf[BUFSIZE];
	int len;
	struct sockaddr_in us;
	struct sockaddr_in them;
	int ulen = sizeof(struct sockaddr_in);
	int tlen = sizeof(struct sockaddr_in);

	assert(iAuth.socket != NOSOCK);

	if (getsockname(client->fd, (struct sockaddr *)&us,   &ulen) ||
			getpeername(client->fd, (struct sockaddr *)&them, &tlen))
	{
		log(L_INFO, "auth get{sock,peer}name error for %s:%m",
			get_client_name(client, SHOW_IP));
		return;
	}

	/*
	 * The client ID will be the memory address of the
	 * client. This is acceptable, because as long
	 * as the client exists, no other client will have
	 * the same address, thus ensuring a unique ID for
	 * each client.
	 */

	len = sprintf(buf,
		"DoAuth %p %u %u %u\n",
		client,
		(unsigned int) client->ip.s_addr,
		(unsigned int) ntohs(them.sin_port),
		(unsigned int) ntohs(us.sin_port));

	send(iAuth.socket, buf, len, 0);
} /* IAuthQuery() */

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
	int length;
	char buffer[BUFSIZE + 1];
	char *ch;
	int paramc;
	char *paramv[MAXPARAMS + 1];

	length = recv(iAuth.socket, buffer, BUFSIZE, 0);

	if ((length == (-1)) &&
			((errno == EWOULDBLOCK) || (errno == EAGAIN)))
		return 2;

	if (length <= 0)
		return 0;

	buffer[length] = '\0';

	fprintf(stderr, "%s", buffer);

	ch = buffer;

	paramc = 0;
	paramv[paramc++] = buffer;

	while (*ch)
	{
		register char tmp;

		tmp = *ch;

		if (IsEol(tmp))
		{
			/*
			 * We've reached the end of our line - process it
			 */

			*ch = '\0';

			if (paramc)
				ProcessIAuthData(paramc, paramv);

			break;
		}
		else if (IsSpace(tmp))
		{
			*ch++ = '\0';
			paramv[paramc++] = ch;
		}

		/*
		 * Advance ch to the next letter in buffer[]
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
	struct AuthRequest *auth;
	long id;

	if (!strcasecmp(parv[0], "DoneAuth"))
	{
		id = strtol(parv[1], 0, 0);
		for (auth = AuthPollList; auth; auth = auth->next)
		{
			/*
			 * Remember: the client id is the memory address
			 * of auth->client, so if it matches id, we
			 * found our client.
			 */
			if ((void *) auth->client == (void *) id)
			{
				fprintf(stderr, "GOT IT: %s %s %s\n",
					parv[1],
					parv[2],
					parv[3]);

				strncpy_irc(auth->client->username, parv[2], USERLEN);
				strncpy_irc(auth->client->host, parv[3], HOSTLEN);

				/*
				 * The IAuth server will return a "~" if the ident
				 * query failed.
				 */
				if (*auth->client->username != '~')
					SetGotId(auth->client);

				remove_auth_request(auth);

				break;
			}
		}
	}
} /* ProcessIAuthData() */
