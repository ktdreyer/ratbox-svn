/************************************************************************
 *   IRC - Internet Relay Chat, iauth/sock.c
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

AuthPort *PortList = NULL; /* list of listen ports */

static void DoListen(AuthPort *portptr);
static void SetSocketOptions(int sockfd);

/*
tosock()
 Send the given string to sockfd
*/

void
tosock(int sockfd, char *format, ...)

{
	va_list args;
	int len;
	char buf[BUFSIZE];

	va_start(args, format);

	len = vsprintf(buf, format, args);

	va_end(args);

	fprintf(stderr, "sending %s", buf);
	send(sockfd, buf, len, 0);
} /* tosock() */

/*
DoListen()
 Begin listening on port 'portinfo->port'
*/

static void
DoListen(AuthPort *portptr)

{
	struct sockaddr_in sockname;

	assert(portptr != 0);

	memset((void *) &sockname, 0, sizeof(struct sockaddr));
	sockname.sin_family = AF_INET;
	sockname.sin_addr.s_addr = INADDR_ANY;
	sockname.sin_port = htons(portptr->port);

	if ((portptr->sockfd = socket(PF_INET, SOCK_STREAM, 6)) < 0)
	{
	#ifdef bingo
		log(L_ERROR, "Unable to create stream socket: %s",
			strerror(errno));
	#endif
		portptr->sockfd = NOSOCK;
		return;
	}

	/* set various socket options */
	SetSocketOptions(portptr->sockfd);

	if (bind(portptr->sockfd, (struct sockaddr *)&sockname, sizeof(sockname)) < 0)
	{
	#ifdef bingo
		log(L_ERROR, "Unable to bind port %d: %s",
			portptr->port,
			strerror(errno));
	#endif
		close(portptr->sockfd);
		portptr->sockfd = NOSOCK;
		return;
	}

	if (listen(portptr->sockfd, 4) < 0)
	{
	#ifdef bingo
		log(L_ERROR, "Unable to listen on port %d: %s",
			portptr->port,
			strerror(errno));
	#endif
		close(portptr->sockfd);
		portptr->sockfd = NOSOCK;
		return;
	}

	if (!SetNonBlocking(portptr->sockfd))
	{
	#ifdef bingo
		log(L_ERROR, "Unable to set socket [%d] non-blocking",
			portptr->sockfd);
	#endif
		close(portptr->sockfd);
		portptr->sockfd = NOSOCK;
		return;
	}
} /* DoListen() */

/*
InitListenPorts()
 Initialize listening ports to allow server connections
*/

void
InitListenPorts()

{
	AuthPort *portptr;

	for (portptr = PortList; portptr; portptr = portptr->next)
		DoListen(portptr);
} /* InitListenPorts() */

/*
SetSocketOptions()
 Set various socket options on 'sockfd'
*/

static void
SetSocketOptions(int sockfd)

{
	int option;

	option = 1;

	/*
	 * SO_REUSEADDR will enable immediate local address reuse of
	 * the port this socket is bound to
	 */
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &option, sizeof(option)) < 0)
	{
	#ifdef bingo
		log(L_ERROR, "SetSocketOptions(): setsockopt(SO_REUSEADDR) failed: %s",
			strerror(errno));
	#endif
		return;
	}
} /* SetSocketOptions() */

/*
SetNonBlocking()
 Mark socket as non-blocking

Return: 1 if successful
        0 if unsuccessful
*/

int
SetNonBlocking(int socket)

{
	int flags = 0;

	if ((flags = fcntl(socket, F_GETFL, 0)) == -1)
	{
	#ifdef bingo
		log(L_ERROR,
			"SetNonBlocking: fcntl(%d, F_GETFL, 0) failed: %s",
			socket,
			strerror(errno));
	#endif
		return 0;
	}
	else
	{
		if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1)
		{
	#ifdef bingo
			log(L_ERROR,
				"SetNonBlocking: fcntl(%d, F_SETFL, %d) failed: %s",
				socket,
				flags | O_NONBLOCK,
				strerror(errno));
	#endif
			return 0;
		}
	}

	return 1;
} /* SetNonBlocking() */
