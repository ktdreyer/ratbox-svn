/* $Id$ */
#ifndef INCLUDED_watch_h
#define INCLUDED_watch_h

#define WATCH_OPERSERV		0x00000001
#define WATCH_GLOBAL		0x00000002
#define WATCH_OPERBOT		0x00000004
#define WATCH_JUPESERV		0x00000008

void PRINTFLIKE(4, 5) watch_send(unsigned int flag, struct client *client_p,
				struct lconn *conn_p, const char *format, ...);

#endif
