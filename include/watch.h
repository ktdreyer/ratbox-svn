/* $Id$ */
#ifndef INCLUDED_watch_h
#define INCLUDED_watch_h

#define WATCH_OPERSERV		0x00000001
#define WATCH_GLOBAL		0x00000002
#define WATCH_OPERBOT		0x00000004
#define WATCH_JUPESERV		0x00000008
#define WATCH_CSADMIN		0x00000010
#define WATCH_CSOPER		0x00000020
#define WATCH_CSREGISTER	0x00000040
#define WATCH_USADMIN		0x00000080
#define WATCH_USOPER		0x00000100
#define WATCH_USREGISTER	0x00000200
#define WATCH_NSADMIN		0x00000400
#define WATCH_NSREGISTER	0x00000800
#define WATCH_BANSERV		0x00001000
#define WATCH_AUTH		0x00002000

void PRINTFLIKE(5, 6) watch_send(unsigned int flag, struct client *client_p,
				struct lconn *conn_p, int oper, const char *format, ...);

#endif