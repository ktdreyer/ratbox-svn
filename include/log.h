/* $Id$ */
#ifndef INCLUDED_log_h
#define INCLUDED_log_h

extern void init_log(void);
extern void open_logfile(void);
extern void close_logfile(void);

extern void slog(const char *format, ...);
extern void slog_send(const char *format, ...);

#endif
