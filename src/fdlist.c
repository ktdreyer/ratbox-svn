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
#include "sprintf_irc.h"

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


/*
 * Wrappers around open() / close() for fileio, since a whole bunch of
 * code that should be using the fbopen() / fbclose() code isn't.
 * Grr. -- adrian
 */

static int
file_open(const char *filename, int mode, int fmode)
{
	int fd;
	fd = open(filename, mode, fmode);
	if(fd == MASTER_MAX)
	{
		fd_close(fd);	/* Too many FDs! */
		errno = ENFILE;
		fd = -1;
	}
	else if(fd >= 0)
		fd_open(fd, FD_FILE, filename);

	return fd;
}

static void
file_close(int fd)
{
	/*
	 * Debug - we set type to FD_FILECLOSE so we can get trapped
	 * in fd_close() with type == FD_FILE. This will allow us to
	 * convert all abusers of fd_close() of a FD_FILE fd over
	 * to file_close() .. mwahaha!
	 */
	s_assert(fd_table[fd].type == FD_FILE);
	fd_table[fd].type = FD_FILECLOSE;
	fd_close(fd);
}

FBFILE *
fbopen(const char *filename, const char *mode)
{
	int openmode = 0;
	int pmode = 0;
	FBFILE *fb = NULL;
	int fd;
	s_assert(filename);
	s_assert(mode);

	if(filename == NULL || mode == NULL)
	{
		errno = EINVAL;
		return NULL;
	}
	while (*mode)
	{
		switch (*mode)
		{
		case 'r':
			openmode = O_RDONLY;
			break;
		case 'w':
			openmode = O_WRONLY | O_CREAT | O_TRUNC;
			pmode = 0644;
			break;
		case 'a':
			openmode = O_WRONLY | O_CREAT | O_APPEND;
			pmode = 0644;
			break;
		case '+':
			openmode &= ~(O_RDONLY | O_WRONLY);
			openmode |= O_RDWR;
			break;
		default:
			break;
		}
		++mode;
	}

	if((fd = file_open(filename, openmode, pmode)) == -1)
	{
		return fb;
	}

	if(NULL == (fb = fdbopen(fd, NULL)))
		file_close(fd);
	return fb;
}

FBFILE *
fdbopen(int fd, const char *mode)
{
	/*
	 * ignore mode, if file descriptor hasn't been opened with the
	 * correct mode, the first use will fail
	 */
	FBFILE *fb = (FBFILE *) MyMalloc(sizeof(FBFILE));
	if(NULL != fb)
	{
		fb->ptr = fb->endp = fb->buf;
		fb->fd = fd;
		fb->flags = 0;
		fb->pbptr = NULL;
	}
	return fb;
}

void
fbclose(FBFILE * fb)
{
	s_assert(fb);
	if(fb != NULL)
	{
		file_close(fb->fd);
		MyFree(fb);
	}
	else
		errno = EINVAL;

}

static int
fbfill(FBFILE * fb)
{
	int n;
	s_assert(fb);
	if(fb == NULL)
	{
		errno = EINVAL;
		return -1;
	}
	if(fb->flags)
		return -1;
	n = read(fb->fd, fb->buf, BUFSIZ);
	if(0 < n)
	{
		fb->ptr = fb->buf;
		fb->endp = fb->buf + n;
	}
	else if(n < 0)
		fb->flags |= FB_FAIL;
	else
		fb->flags |= FB_EOF;
	return n;
}

int
fbgetc(FBFILE * fb)
{
	s_assert(fb);
	if(fb == NULL)
	{
		errno = EINVAL;
		return -1;
	}
	if(fb->pbptr)
	{
		if((fb->pbptr == (fb->pbuf + BUFSIZ)) || (!*fb->pbptr))
			fb->pbptr = NULL;
	}

	if(fb->ptr < fb->endp || fbfill(fb) > 0)
		return *fb->ptr++;
	return EOF;
}

void
fbungetc(char c, FBFILE * fb)
{
	s_assert(fb);
	if(fb == NULL)
	{
		errno = EINVAL;
		return;
	}
	if(!fb->pbptr)
	{
		fb->pbptr = fb->pbuf + BUFSIZ;
	}

	if(fb->pbptr != fb->pbuf)
	{
		fb->pbptr--;
		*fb->pbptr = c;
	}
}

char *
fbgets(char *buf, size_t len, FBFILE * fb)
{
	char *p = buf;
	s_assert(buf);
	s_assert(fb);
	s_assert(0 < len);

	if(fb == NULL || buf == NULL)
	{
		errno = EINVAL;
		return NULL;
	}
	if(fb->pbptr)
	{
		strlcpy(buf, fb->pbptr, len);
		fb->pbptr = NULL;
		return (buf);
	}

	if(fb->ptr == fb->endp && fbfill(fb) < 1)
		return 0;
	--len;
	while (len--)
	{
		*p = *fb->ptr++;
		if('\n' == *p)
		{
			++p;
			break;
		}
		/*
		 * deal with CR's
		 */
		else if('\r' == *p)
		{
			if(fb->ptr < fb->endp || fbfill(fb) > 0)
			{
				if('\n' == *fb->ptr)
					++fb->ptr;
			}
			*p++ = '\n';
			break;
		}
		++p;
		if(fb->ptr == fb->endp && fbfill(fb) < 1)
			break;
	}
	*p = '\0';
	return buf;
}

int
fbputs(const char *str, FBFILE * fb)
{
	int n = -1;
	s_assert(str);
	s_assert(fb);

	if(str == NULL || fb == NULL)
	{
		errno = EINVAL;
		return -1;
	}
	if(0 == fb->flags)
	{
		n = write(fb->fd, str, strlen(str));
		if(-1 == n)
			fb->flags |= FB_FAIL;
	}
	return n;
}

int
fbstat(struct stat *sb, FBFILE * fb)
{
	s_assert(sb);
	s_assert(fb);
	if(sb == NULL || fb == NULL)
	{
		errno = EINVAL;
		return -1;
	}
	return fstat(fb->fd, sb);
}
