/* $Id$ */
#ifndef INCLUDED_log_h
#define INCLUDED_log_h

struct client;

extern void open_logfile(void);
extern void open_service_logfile(struct client *service_p);
extern void reopen_logfiles(void);

extern void mlog(const char *format, ...);

extern void slog(struct client *, int level, const char *format, ...);

#endif
