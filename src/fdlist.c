/*
 *  ircd-ratbox: A slightly useful ircd.
 *  fdlist.c: Maintains a list of file descriptors.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */
#include "stdinc.h"
#include "config.h"		/* option settings */
#include "fdlist.h"
#include "client.h"		/* struct Client */
#include "event.h"
#include "ircd.h"		/* GlobalSetOptions */
#include "s_bsd.h"		/* highest_fd */
#include "send.h"
#include "memory.h"
#include "numeric.h"
#include "s_log.h"

fde_t *fd_table = NULL;

static void fdlist_update_biggest(int fd, int opening);

/* Highest FD and number of open FDs .. */
int highest_fd = -1;		/* Its -1 because we haven't started yet -- adrian */
int number_fd = 0;

static void
fdlist_update_biggest(int fd, int opening)
{
	if(fd < highest_fd)
		return;
	s_assert(fd < MAXCONNECTIONS);

	if(fd > highest_fd)
	{
		/*  
		 * s_assert that we are not closing a FD bigger than
		 * our known biggest FD
		 */
		s_assert(opening);
		highest_fd = fd;
		return;
	}
	/* if we are here, then fd == Biggest_FD */
	/*
	 * s_assert that we are closing the biggest FD; we can't be
	 * re-opening it
	 */
	s_assert(!opening);
	while (highest_fd >= 0 && !fd_table[highest_fd].flags.open)
		highest_fd--;
}


void
fdlist_init(void)
{
	static int initialized = 0;

	if(!initialized)
	{
		/* Since we're doing this once .. */
		fd_table = MyMalloc((MAXCONNECTIONS + 1) * sizeof(fde_t));
		initialized = 1;
	}
}

/* Called to open a given filedescriptor */
void
fd_open(int fd, unsigned int type, const char *desc)
{
	fde_t *F = &fd_table[fd];
	s_assert(fd >= 0);

	if(F->flags.open)
	{
		fd_close(fd);
	}
	s_assert(!F->flags.open);
	F->fd = fd;
	F->type = type;
	F->flags.open = 1;
#ifdef NOTYET
	F->defer.until = 0;
	F->defer.n = 0;
	F->defer.handler = NULL;
#endif
	fdlist_update_biggest(fd, 1);
	F->comm_index = -1;
	F->list = FDLIST_NONE;
	if(desc)
		strlcpy(F->desc, desc, sizeof(F->desc));
	number_fd++;
}


/* Called to close a given filedescriptor */
void
fd_close(int fd)
{
	fde_t *F = &fd_table[fd];
	s_assert(F->flags.open);

	/* All disk fd's MUST go through file_close() ! */
	s_assert(F->type != FD_FILE);
	if(F->type == FD_FILE)
	{
		s_assert(F->read_handler == NULL);
		s_assert(F->write_handler == NULL);
	}
	comm_setselect(fd, FDLIST_NONE, COMM_SELECT_WRITE | COMM_SELECT_READ, NULL, NULL, 0);

	F->flags.open = 0;
	fdlist_update_biggest(fd, 0);
	number_fd--;
	memset(F, '\0', sizeof(fde_t));
	F->timeout = 0;
	/* Unlike squid, we're actually closing the FD here! -- adrian */
	close(fd);
}


/*
 * fd_dump() - dump the list of active filedescriptors
 */
void
fd_dump(struct Client *source_p)
{
	int i;

	for (i = 0; i <= highest_fd; i++)
	{
		if(!fd_table[i].flags.open)
			continue;

		sendto_one_numeric(source_p, RPL_STATSDEBUG, 
				   "F :fd %-3d desc '%s'",
				   i, fd_table[i].desc);
	}
}

/*
 * fd_note() - set the fd note
 *
 * Note: must be careful not to overflow fd_table[fd].desc when
 *       calling.
 */
void
fd_note(int fd, const char *format, ...)
{
	va_list args;

	if(format)
	{
		va_start(args, format);
		ircvsnprintf(fd_table[fd].desc, FD_DESC_SZ, format, args);
		va_end(args);
	}
	else
		fd_table[fd].desc[0] = '\0';
}
