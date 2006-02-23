/* $Id$ */
#ifndef INCLUDED_rserv_h
#define INCLUDED_rserv_h

#define BUFSIZE 512
#define BUFSIZE_SAFE 450

#define MAX_DATE_STRING	32

int current_mark;
int testing_conf;

extern struct timeval system_time;
#define CURRENT_TIME system_time.tv_sec

extern void set_time(void);

extern void PRINTFLIKE(1, 2) die(const char *format, ...);

extern int have_md5_crypt;

const char *get_crypt(const char *password, const char *csalt);

char *rebuild_params(const char **, int, int);

int valid_servername(const char *);

#endif
