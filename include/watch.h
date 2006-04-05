/* $Id$ */
#ifndef INCLUDED_watch_h
#define INCLUDED_watch_h

#define WATCH_OPERSERV		0x00000001

void PRINTFLIKE(2, 3) watch_send(unsigned int flag, const char *format, ...);

#endif
