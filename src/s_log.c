/*
 *  ircd-ratbox: A slightly useful ircd.
 *  s_log.c: Logger functions.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "client.h"		/* Needed for struct Client */
#include "s_log.h"
#include "fileio.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "event.h"		/* Needed for EVH etc. */
#include "s_conf.h"
#include "s_serv.h"
#include "memory.h"

#define LOG_BUFSIZE 2000

#ifdef USE_LOGFILE
static FBFILE *logFile;
#endif
static int logLevel = INIT_LOG_LEVEL;

static EVH user_log_resync;
static FBFILE *user_log_fb = NULL;

static const char *logLevelToString[] = { "L_CRIT",
	"L_ERROR",
	"L_WARN",
	"L_NOTICE",
	"L_TRACE",
	"L_INFO",
	"L_DEBUG"
};

/*
 * open_log - open ircd logging file
 * returns true (1) if successful, false (0) otherwise
 */
#if defined(USE_LOGFILE)

static int
open_log(const char *filename)
{
	logFile = fbopen(filename, "a");
	if(logFile == NULL)
		return 0;
	return 1;
}
#endif


#if defined(USE_LOGFILE)
static void
write_log(const char *message)
{
	char buf[LOG_BUFSIZE];

	if(logFile == NULL)
		return;

	ircsnprintf(buf, LOG_BUFSIZE, "[%s] %s\n", smalldate(), message);
	fbputs(buf, logFile);
}
#endif

#ifdef __vms
void send_opcom(const char *message)
{
	struct {
		struct hdr {                        /* Trust me, this is necessary */
			unsigned char type;         /*  Read up on SYS$SNDOPR and you'll */
			unsigned short target_0_15; /*  see why. */
			unsigned char target_16_23;
			unsigned long rqst_id;
		} h;
		char msg[200];
	} opc_request;
	struct dsc$descriptor opc;

	opc_request.h.type = OPC$_RQ_RQST;  /* Send out the string */
	opc_request.h.target_0_15 = OPC$M_NM_CENTRL;    /* To main operator */
	opc_request.h.target_16_23 = 0;
	opc_request.h.rqst_id = 0L;         /* Default it */

	strcpy(opc_request.msg, message);         /* Copy the string */

	opc.dsc$a_pointer = &opc_request;   /* Build a descriptor for the block */
	opc.dsc$w_length = strlen(message) + sizeof(struct hdr);

	sys$sndopr(&opc, 0);
}
#endif

void
ilog(int priority, const char *fmt, ...)
{
	char buf[LOG_BUFSIZE];
	va_list args;
	s_assert(-1 < priority);
	if(fmt == NULL)
		return;

	if(priority > logLevel)
		return;

	va_start(args, fmt);
	ircvsprintf(buf, fmt, args);
	va_end(args);

#if defined(USE_LOGFILE)
	write_log(buf);
#endif
#ifdef __vms
	send_opcom(buf);
#endif
}

void
init_log(const char *filename)
{
#if defined(USE_LOGFILE)
	open_log(filename);
#endif
	eventAddIsh("user_log_resync", user_log_resync, NULL, 60);
}

void
reopen_log(const char *filename)
{
#if defined(USE_LOGFILE)
	fbclose(logFile);
	open_log(filename);
#endif

}

void
set_log_level(int level)
{
	if(L_ERROR < level && level <= L_DEBUG)
		logLevel = level;
}

int
get_log_level(void)
{
	return (logLevel);
}

const char *
get_log_level_as_string(int level)
{
	if(level > L_DEBUG)
		level = L_DEBUG;
	else if(level < L_ERROR)
		level = L_ERROR;

	return (logLevelToString[level]);
}


/*
 * log_user_exit
 *
 * inputs	- pointer to connecting client
 * output	- NONE
 * side effects - Current exiting client is logged to file
 */
void
log_user_exit(struct Client *source_p)
{
	time_t on_for;

	on_for = CurrentTime - source_p->firsttime;

	char linebuf[BUFSIZ];

	/*
	 * This conditional makes the logfile active only after
	 * it's been created - thus logging can be turned off by
	 * removing the file.
	 * -Taner
	 */
	if(IsPerson(source_p))
	{
		if(user_log_fb == NULL)
		{
			if((ConfigFileEntry.fname_userlog[0] != '\0')
					&& (user_log_fb =
						fbopen(ConfigFileEntry.fname_userlog, "r")) != NULL)
			{
				fbclose(user_log_fb);
				user_log_fb = fbopen(ConfigFileEntry.fname_userlog, "a");
			}
		}

		if(user_log_fb != NULL)
		{
			ircsnprintf(linebuf, sizeof(linebuf),
					"%s (%3ld:%02ld:%02ld): %s!%s@%s %d/%d\n",
					myctime(source_p->firsttime),
					(signed long) on_for / 3600,
					(signed long) (on_for % 3600) /
					60, (signed long) on_for % 60,
					source_p->name,
					source_p->username,
					source_p->host,
					source_p->localClient->sendK,
					source_p->localClient->receiveK);

			fbputs(linebuf, user_log_fb);
		}
	}
}

/*
 * user_log_resync
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	-
 */
static void
user_log_resync(void *notused)
{
	if(user_log_fb != NULL)
	{
		fbclose(user_log_fb);
		user_log_fb = NULL;
	}
}

/*
 * log_oper
 *
 * inputs	- pointer to client
 * output	- none
 * side effects - FNAME_OPERLOG is written to, if its present
 */
void
log_oper(struct Client *source_p, const char *name)
{
	FBFILE *oper_fb;
	char linebuf[BUFSIZE];

	if(ConfigFileEntry.fname_operlog[0] == '\0')
		return;

	if(EmptyString(name))
		return;

	if(IsPerson(source_p))
	{
		if((oper_fb = fbopen(ConfigFileEntry.fname_operlog, "r")) != NULL)
		{
			fbclose(oper_fb);
			oper_fb = fbopen(ConfigFileEntry.fname_operlog, "a");
		}

		if(oper_fb != NULL)
		{
			ircsnprintf(linebuf, sizeof(linebuf), "%s OPER (%s) by (%s!%s@%s)\n",
				   myctime(CurrentTime), name,
				   source_p->name, source_p->username, source_p->host);

			fbputs(linebuf, oper_fb);
			fbclose(oper_fb);
		}
	}
}

/* log_foper()
 *
 * inputs       - pointer to client
 * output       -
 * side effects - FNAME_FOPERLOG is written to, if present
 */
void
log_foper(struct Client *source_p, const char *name)
{
	FBFILE *oper_fb;
	char linebuf[BUFSIZE];

	if(ConfigFileEntry.fname_foperlog[0] == '\0')
		return;

	if(EmptyString(name))
		return;

	if(IsPerson(source_p))
	{
		if((oper_fb = fbopen(ConfigFileEntry.fname_foperlog, "r")) != NULL)
		{
			fbclose(oper_fb);
			oper_fb = fbopen(ConfigFileEntry.fname_foperlog, "a");
		}

		if(oper_fb != NULL)
		{
			ircsnprintf(linebuf, sizeof(linebuf),
				   "%s FAILED OPER (%s) by (%s!%s@%s)\n",
				   myctime(CurrentTime), name,
				   source_p->name, source_p->username, source_p->host);
			fbputs(linebuf, oper_fb);
			fbclose(oper_fb);
		}
	}
}

/* log_operspy()
 *
 * inputs	- pointer to client
 * output	- 
 * side effects - FNAME_OPERSPYLOG/FNAME_OPERSPYREMOTELOG is written to
 */
void
log_operspy(struct Client *source_p, const char *token, const char *arg)
{
	FBFILE *operspy_fb;
	char linebuf[BUFSIZE];
	char *filename_ptr;

	/* if its not my client its already propagated */
	if(MyClient(source_p))
	{
		sendto_match_servs(source_p, "*", CAP_ENCAP,
				   "ENCAP * OPERSPY %s %s",
				   token, arg ? arg : "");
		filename_ptr = ConfigFileEntry.fname_operspylog;
	}
	else
		filename_ptr = ConfigFileEntry.fname_operspyremotelog;

	sendto_realops_flags(UMODE_OPERSPY,
			     ConfigFileEntry.operspy_admin_only ? L_ADMIN : L_ALL,
			     "OPERSPY %s %s %s",
			     get_oper_name(source_p), token,
			     arg ? arg : "");

	if(EmptyString(filename_ptr))
		return;

	if((operspy_fb = fbopen(filename_ptr, "r")) != NULL)
	{
		fbclose(operspy_fb);
		operspy_fb = fbopen(filename_ptr, "a");
	}

	if(operspy_fb != NULL)
	{
		ircsnprintf(linebuf, sizeof(linebuf),
			 "%s OPERSPY %s %s %s\n",
			 smalldate(), get_oper_name(source_p),
			 token, arg ? arg : "");
		fbputs(linebuf, operspy_fb);
		fbclose(operspy_fb);
	}
}

/* smalldate()
 *
 * returns a date in the form YY/MM/DD HH.MM
 */
const char *
smalldate(void)
{
	static char buf[MAX_DATE_STRING];
	struct tm *lt;
	time_t lclock;

	lclock = CurrentTime;
	lt = localtime(&lclock);

	ircsnprintf(buf, sizeof(buf), "%d/%d/%d %02d.%02d",
		   lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min);

	return buf;
}

#ifdef __vms
const char *
ircd$format_error(int status)
{
	static char msg[257];
	struct dsc$descriptor msgd;
	int msg_len;
	char temp[512];

	msg_len = 0;
	msgd.dsc$w_length = 256;
	msgd.dsc$a_pointer = msg;
	sys$getmsg(status, &msg_len, &msgd, 0, &temp);
	msg[msg_len] = '\0';
	return msg + 1;
}
#endif

