/*
 *  ircd-ratbox: A slightly useful ircd.
 *  fdlist.h: The file descriptor list header.
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

#ifndef INCLUDED_fdlist_h
#define INCLUDED_fdlist_h

#include "config.h"
#include "ircd_defs.h"
#define FD_DESC_SZ 128		/* hostlen + comment */

/*
 * priority values used in fdlist code
 */
#define FDL_SERVER   0x01
#define FDL_BUSY     0x02
#define FDL_OPER     0x04
#define FDL_DEFAULT  0x08
#define FDL_ALL      0xFF

/* FD type values */
enum
{
	FD_NONE,
	FD_LOG,
	FD_FILE,
	FD_FILECLOSE,
	FD_SOCKET,
	FD_PIPE,
	FD_UNKNOWN
};

enum
{
	COMM_OK,
	COMM_ERR_BIND,
	COMM_ERR_DNS,
	COMM_ERR_TIMEOUT,
	COMM_ERR_CONNECT,
	COMM_ERROR,
	COMM_ERR_MAX
};

typedef enum fdlist_t
{
	FDLIST_NONE,
	FDLIST_SERVICE,
	FDLIST_SERVER,
	FDLIST_IDLECLIENT,
	FDLIST_BUSYCLIENT,
	FDLIST_MAX
}
fdlist_t;

typedef struct _fde fde_t;

/* Callback for completed IO events */
typedef void PF(int, void *);

/* Callback for completed connections */
/* int fd, int status, void * */
typedef void CNCB(int, int, void *);

extern int highest_fd;
extern int number_fd;

struct Client;

struct _fde
{
	/* New-school stuff, again pretty much ripped from squid */
	/*
	 * Yes, this gives us only one pending read and one pending write per
	 * filedescriptor. Think though: when do you think we'll need more?
	 */
	int fd;			/* So we can use the fde_t as a callback ptr */
	int type;
	fdlist_t list;		/* Which list this FD should sit on */
	int comm_index;		/* where in the poll list we live */
	char desc[FD_DESC_SZ];
	PF *read_handler;
	void *read_data;
	PF *write_handler;
	void *write_data;
	PF *timeout_handler;
	void *timeout_data;
	time_t timeout;
	PF *flush_handler;
	void *flush_data;
	time_t flush_timeout;
	struct DNSQuery *dns_query;
	struct
	{
		unsigned int open:1;
		unsigned int close_request:1;
		unsigned int write_daemon:1;
		unsigned int closing:1;
		unsigned int socket_eof:1;
		unsigned int nolinger:1;
		unsigned int nonblocking:1;
		unsigned int ipc:1;
		unsigned int called_connect:1;
	}
	flags;
	struct
	{
		/* We don't need the host here ? */
		struct sockaddr_storage S;
		struct sockaddr_storage hostaddr;
		CNCB *callback;
		void *data;
		/* We'd also add the retry count here when we get to that -- adrian */
	}
	connect;
	int pflags;
};


extern fde_t *fd_table;

void fdlist_init(void);

extern void fd_open(int, unsigned int, const char *);
extern void fd_close(int);
extern void fd_dump(struct Client *source_p);
#ifndef __GNUC__
extern void fd_note(int fd, const char *format, ...);
#else
extern void fd_note(int fd, const char *format, ...) __attribute__ ((format(printf, 2, 3)));
#endif

#define FB_EOF  0x01
#define FB_FAIL 0x02

struct FileBuf
{
	int fd;			/* file descriptor */
	char *endp;		/* one past the end */
	char *ptr;		/* current read pos */
	char *pbptr;		/* pointer to push back char */
	int flags;		/* file state */
	char buf[BUFSIZ];	/* buffer */
	char pbuf[BUFSIZ + 1];	/* push back buffer */
};

/*
 * FileBuf is a mirror of the ANSI FILE struct, but it works for any
 * file descriptor. FileBufs are allocated when a file is opened with
 * fbopen, and they are freed when the file is closed using fbclose.
 */
typedef struct FileBuf FBFILE;

extern FBFILE *fbopen(const char *filename, const char *mode);
extern FBFILE *fdbopen(int fd, const char *mode);
extern void fbclose(FBFILE * fb);
extern int fbgetc(FBFILE * fb);
extern char *fbgets(char *buf, size_t len, FBFILE * fb);
extern void fbungetc(char c, FBFILE * fb);
extern int fbputs(const char *str, FBFILE * fb);
extern int fbstat(struct stat *sb, FBFILE * fb);
extern FBFILE *fbpopen(const char *, const char *);



#endif /* INCLUDED_fdlist_h */
