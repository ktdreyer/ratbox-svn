/************************************************************************
 *   IRC - Internet Relay Chat, include/s_bsd.h
 *   Copyright (C) 1992 Darren Reed
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
 *
 */
#ifndef INCLUDED_s_bsd_h
#define INCLUDED_s_bsd_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

#define READBUF_SIZE    16384   /* used in s_bsd *AND* s_zip.c ! */

/* Type of IO */
#define	COMM_SELECT_READ		1
#define	COMM_SELECT_WRITE		2

struct Client;
struct ConfItem;
struct hostent;
struct DNSReply;
struct Listener;

extern int   highest_fd;
extern int   readcalls;
extern const char* const NONB_ERROR_MSG; 
extern const char* const SETBUF_ERROR_MSG;

extern void  add_connection(struct Listener*, int);
extern int   check_client(struct Client*, char *,char **);
extern void  close_connection(struct Client*);
extern void  close_all_connections(void);
extern int   connect_server(struct ConfItem*, struct Client*, 
                            struct DNSReply*);
extern void  get_my_name(struct Client *, char *, int);
extern void  report_error(const char*, const char*, int);
extern int   set_non_blocking(int);
extern int   set_sock_buffers(int, int);
extern int   send_queued(struct Client*);
extern int   deliver_it(struct Client*, const char*, int);

extern int   completed_connection(struct Client*);
extern int   read_packet(struct Client*);
extern void  error_exit_client(struct Client*, int);
extern int   get_sockerr(int);
extern int   parse_client_queued(struct Client*);
extern int   ignoreErrno(int ierrno);

/* These must be defined in the network IO loop code of your choice */
extern void  comm_setselect(int fd, unsigned int type, PF *handler, 
                 void *client_data, time_t timeout);
extern void  init_netio(void);
extern int   read_message (time_t, unsigned char);
extern int   comm_select(time_t);

#endif /* INCLUDED_s_bsd_h */

