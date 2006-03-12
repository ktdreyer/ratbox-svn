/* $Id$ */
#ifndef INCLUDED_rserv_h
#define INCLUDED_rserv_h

#define BUFSIZE 512
#define BUFSIZE_SAFE 450

#define MAX_DATE_STRING	32

#define PASSWDLEN	35
#define EMAILLEN	100
#define OPERNAMELEN	30

int current_mark;
int testing_conf;

extern struct timeval system_time;
#define CURRENT_TIME system_time.tv_sec

extern void set_time(void);

extern void PRINTFLIKE(1, 2) die(const char *format, ...);

extern int have_md5_crypt;

void init_crypt_seed(void);
const char *get_crypt(const char *password, const char *csalt);
const char *get_password(void);

char *rebuild_params(const char **, int, int);

int valid_servername(const char *);

/* snprintf.c */
int rs_snprintf(char *, const size_t, const char *, ...);
int rs_vsnprintf(char *, const size_t, const char *, va_list);

#endif
