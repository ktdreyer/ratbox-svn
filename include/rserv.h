/* $Id$ */
#ifndef INCLUDED_rserv_h
#define INCLUDED_rserv_h

#define BUFSIZE 512
#define MAXPARA	15
#define MAX_DATE_STRING	32

extern struct timeval system_time;
#define CURRENT_TIME system_time.tv_sec

extern void set_time(void);

extern void die(const char *reason);

extern int have_md5_crypt;

#endif
