/* $Id$ */
#ifndef INCLUDED_rserv_h
#define INCLUDED_rserv_h

#define BUFSIZE 512
#define BUFSIZE_SAFE 450

#define MAXPARA	15
#define MAX_DATE_STRING	32

extern struct timeval system_time;
#define CURRENT_TIME system_time.tv_sec

extern void set_time(void);

extern void die(const char *reason);

extern int have_md5_crypt;

const char *get_crypt(const char *password, const char *csalt);

typedef int (*db_callback) (void *, int, char **, char **);
void loc_sqlite_exec(db_callback cb, const char *format, ...);

char *rebuild_params(const char **, int, int);

#endif
