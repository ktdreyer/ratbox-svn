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

#include "common.h"
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
GenerateClientID()
 Generate a unique ID based on cptr. It is imperative that
no two clients may have the same ID.
*/

int
GenerateClientID(const struct Client *cptr)

{
	/* bingo */
	return (1);
} /* GenerateClientID() */

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
	char *user;

	if (!strcasecmp(parv[0], "DoneAuth"))
	{
		id = strtol(parv[1], 0, 0);
		for (auth = AuthPollList; auth; auth = auth->next)
		{
			if (auth->clientid == (void *) id)
			{
				fprintf(stderr, "GOT IT: %s %s %s\n",
					parv[1],
					parv[2],
					parv[3]);

				user = (char *)auth->client->username;
				strcpy(user, parv[2]);
				strcpy(auth->client->host, parv[3]);

				remove_auth_request(auth);

				break;
			}
		}
	}
} /* ProcessIAuthData() */
