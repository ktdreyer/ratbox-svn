/* $Id$ */
#ifndef INCLUDED_log_h
#define INCLUDED_log_h

struct client;
struct lconn;

extern void open_logfile(void);
extern void open_service_logfile(struct client *service_p);
extern void reopen_logfiles(void);

extern void PRINTFLIKE(1, 2) mlog(const char *format, ...);

extern void PRINTFLIKE(3, 4) slog(struct client *, int level, const char *format, ...);
extern void PRINTFLIKE(7, 8) zlog(struct client *, int loglevel, int watchlevel, int oper,
					struct client *, struct lconn *,
					const char *format, ...);

#endif
