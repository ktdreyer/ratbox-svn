/************************************************************************
 *   IRC - Internet Relay Chat, iauth/conf.c
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

/*
LoadConfig()
 Load configuration file 'filename'

Return: 1 if successful
        0 if unsuccessful
*/

int
LoadConfig(char *filename)

{
	AuthPort *portptr;

	/*
	 * bingo - this is a fast hack just to get a working iauth asap
	 */

#ifdef bingo
	portptr = (AuthPort *) MyMalloc(sizeof(AuthPort));
#endif
	portptr = (AuthPort *) malloc(sizeof(AuthPort));
	portptr->port = 4444;
	portptr->sockfd = NOSOCK;
	portptr->next = PortList;
	PortList = portptr;

	return 1;
} /* LoadConfig() */
