/************************************************************************
 *   IRC - Internet Relay Chat, iauth/iauth.c
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

int
main()

{
#ifndef DEBUGMODE
	int pid;
#endif

	LoadConfig(ICONF);

#ifndef DEBUGMODE

	pid = fork();
	switch (pid)
	{
		case -1:
		{
			fprintf(stderr, "Unable to fork, exiting\n");
			exit (-1);
		}

		/*
		 * Child: do nothing
		 */
		case 0:
		{
			break;
		}

		/*
		 * Parent: exit
		 */
		default:
		{
			exit (0);
		}
	} /* switch (pid) */

#endif /* !DEBUGMODE */

	fprintf(stderr, "IAuth loaded\n");

	/* Initialize listening ports */
	InitListenPorts();

	/*
	 * Enter this loop constantly waiting for incoming data
	 * or connections
	 */
	while (1)
	{
		AcceptAuthRequests();
	}
} /* main() */
