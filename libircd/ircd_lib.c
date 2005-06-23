/*
 * $Id$
 */

#include "stdinc.h"
#include "tools.h"
#include "ircd_lib.h"
#include "snprintf.h"
#include "commio.h"
#include "balloc.h"
#include "event.h"

static log_cb *ircd_log;
static restart_cb *ircd_restart;
static die_cb *ircd_die;

static char errbuf[512];

void
lib_ilog(const char *format, ...)
{
	va_list args;
	if(ircd_log == NULL)
		return;
	va_start(args, format);
	ircvsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_log(errbuf);
}

void
lib_die(const char *format, ...)
{
	va_list args;
	if(ircd_die == NULL)
		return;
	va_start(args, format);
	ircvsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_die(errbuf);
}

void
lib_restart(const char *format, ...)
{
	va_list args;
	if(ircd_restart == NULL)
		return;
	va_start(args, format);
	ircvsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_restart(errbuf);
}


void
set_time(void)
{
	struct timeval newtime;
	newtime.tv_sec = 0;
	newtime.tv_usec = 0;
#ifdef HAVE_GETTIMEOFDAY
	if(gettimeofday(&newtime, NULL) == -1)
#else
	if(time(&newtime.tv_sec) == -1)
#endif
	{
		lib_ilog("Clock Failure (%d)", errno);
		lib_restart("Clock Failure");
	}

	if(newtime.tv_sec < SystemTime.tv_sec)
		set_back_events(SystemTime.tv_sec - newtime.tv_sec);

	SystemTime.tv_sec = newtime.tv_sec;
	SystemTime.tv_usec = newtime.tv_usec;
}


void
ircd_lib(log_cb *ilog, restart_cb *irestart, die_cb *idie)
{
	ircd_log = ilog;
	ircd_restart = irestart;
	ircd_die = idie;

	fdlist_init();
	init_netio();
	eventInit();
	initBlockHeap();
	init_dlink_nodes();
	linebuf_init();
}

