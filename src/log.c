/* src/log.c
 *  Contains code for writing to logfile
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "fileio.h"
#include "log.h"
#include "io.h"

static FBFILE *logfile;

void
init_log(void)
{
	open_logfile();
}

void
open_logfile(void)
{
	logfile = fbopen(LOG_PATH, "a");
}

void
close_logfile(void)
{
	if(logfile != NULL)
		fbclose(logfile);
}

static const char *
smalldate(void)
{
	static char buf[MAX_DATE_STRING];
	struct tm *lt;
	time_t ltime = CURRENT_TIME;

	lt = localtime(&ltime);

	snprintf(buf, sizeof(buf), "%d/%d/%d %02d.%02d",
		 lt->tm_year + 1900, lt->tm_mon + 1,
		 lt->tm_mday, lt->tm_hour, lt->tm_min);

	return buf;
}

void
slog(const char *format, ...)
{
	char buf[BUFSIZE];
	char buf2[BUFSIZE];
	va_list args;

	if(logfile == NULL)
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	snprintf(buf2, sizeof(buf2), "%s %s\n", smalldate(), buf);
	fbputs(buf2, logfile);
}

void
slog_send(const char *format, ...)
{
        char buf[BUFSIZE];
        va_list args;

        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);

        slog("%s", buf);
        sendto_connections("%s", buf);
}
