/* src/log.c
 *   Contains code for writing to logfile
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "log.h"
#include "io.h"
#include "client.h"
#include "service.h"
#include "conf.h"

static FILE *logfile;

void
open_logfile(void)
{
	logfile = fopen(LOG_PATH, "a");
}

void
open_service_logfile(struct client *service_p)
{
	char buf[PATH_MAX];

	snprintf(buf, sizeof(buf), "%s%s.log", LOG_DIR, lcase(service_p->service->id));

	service_p->service->logfile = fopen(buf, "a");
}

void
reopen_logfiles(void)
{
	struct client *service_p;
	dlink_node *ptr;

	if(logfile != NULL)
		fclose(logfile);

	open_logfile();

	DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		if(service_p->service->logfile != NULL)
			fclose(service_p->service->logfile);

		open_service_logfile(service_p);
	}
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
mlog(const char *format, ...)
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
	fputs(buf2, logfile);
	fflush(logfile);
}

void
slog(struct client *service_p, int loglevel, const char *format, ...)
{
	char buf[BUFSIZE];
	char buf2[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(loglevel == 1 && ServiceWallopAdm(service_p))
	{
		sendto_server(":%s WALLOPS :%s: %s\n",
				MYNAME, service_p->name, buf);
	}

	if(service_p->service->logfile == NULL)
		return;

	if(service_p->service->loglevel < loglevel)
		return;

	snprintf(buf2, sizeof(buf2), "%s %s\n", smalldate(), buf);
	fputs(buf2, service_p->service->logfile);
	fflush(service_p->service->logfile);
}
