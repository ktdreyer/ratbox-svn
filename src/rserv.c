/* src/rserv.c
 *   Contains initialisation stuff for ratbox-services.
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include <signal.h>
#include <sys/resource.h>
#include <sqlite.h>
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#include "rserv.h"
#include "conf.h"
#include "io.h"
#include "event.h"
#include "scommand.h"
#include "ucommand.h"
#include "client.h"
#include "channel.h"
#include "log.h"
#include "c_init.h"
#include "service.h"
#include "balloc.h"
#include "cache.h"
#include "newconf.h"
#include "serno.h"

struct sqlite *rserv_db;

struct timeval system_time;

int have_md5_crypt;
int current_mark;

static int need_rehash = 0;
static void sig_hup(int);
static void sig_term(int);
static void check_rehash(void *);

void
die(const char *reason)
{
	if(rserv_db)
		sqlite_close(rserv_db);

	sendto_all(0, "Services terminated: (%s)", reason);
	mlog("ratbox-services terminated: (%s)", reason);
	exit(1);
}

void
set_time(void)
{
	struct timeval newtime;

	newtime.tv_sec = newtime.tv_usec = 0;

	if(gettimeofday(&newtime, NULL) == -1)
		die("Clock failure.");

	system_time.tv_sec = newtime.tv_sec;
	system_time.tv_usec = newtime.tv_usec;
}

static void
setup_corefile(void)
{
	struct rlimit rlim;

	if(!getrlimit(RLIMIT_CORE, &rlim))
	{
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}
}

static void
check_pidfile(void)
{
	FILE *fb;
	char buf[32];
	pid_t filepid;

	if((fb = fopen(PID_PATH, "r")) != NULL)
	{
		if(fgets(buf, 20, fb) != NULL && errno != ENOENT)
		{
			filepid = atoi(buf);
			if(!kill(filepid, 0))
			{
				printf("ratbox-services: daemon already running\n");
				exit(-1);
			}
		}

		fclose(fb);
	}
}

static void
write_pidfile(void)
{
	FILE *fb;
	char buf[32];
	
	if((fb = fopen(PID_PATH, "w")) != NULL)
	{
		snprintf(buf, sizeof(buf), "%u\n", (unsigned int) getpid());

		if(fputs(buf, fb) == -1)
			mlog("ERR: Error writing to pid file %s (%s)",
			     PID_PATH, strerror(errno));
		fflush(fb);
		fclose(fb);
	}
	else
		mlog("ERR: Error opening pid file %s", PID_PATH);
}

static void
print_help(void)
{
	printf("ratbox-services [-h|-v|-f]\n");
	printf(" -h show this help\n");
	printf(" -v show version\n");
	printf(" -f foreground mode\n");
}

static void
check_md5_crypt(void)
{
	if(strcmp((crypt("validate", "$1$tEsTiNg1")), "$1$tEsTiNg1$Orp/Maa6pOxfOpGWjmtVE/") == 0)
		have_md5_crypt = 1;
	else
		have_md5_crypt = 0;
}

int 
main(int argc, char *argv[])
{
	char **errmsg;
	char c;
	int nofork = 0;
	int childpid;

	if(geteuid() == 0)
	{
		printf("ratbox-services will not run as root\n");
		return -1;
	}

	check_md5_crypt();

	setup_corefile();

	while((c = getopt(argc, argv, "hvf")) != -1)
	{
		switch(c)
		{
			case 'h':
				print_help();
				exit(0);
				break;
			case 'v':
				printf("ratbox-services: version %s(%s)\n",
					RSERV_VERSION, SERIALNUM);
				exit(0);
				break;
			case 'f':
				nofork = 1;
				break;
		}
	}

	set_time();

#ifdef __CYGWIN__
        nofork = 1;
#endif

        if(!nofork)
        	check_pidfile();

	printf("ratbox-services: version %s(%s)\n",
		RSERV_VERSION, SERIALNUM);

	if(!nofork)
	{
		childpid = fork();

		switch (childpid)
		{
			case -1:
				perror("fork()");
				exit(3);
			case 0:
				close(STDIN_FILENO);
				close(STDOUT_FILENO);
				close(STDERR_FILENO);
				if (setsid() == -1)
					die("setsid() error");

				break;
			default:
				printf("ratbox-services: pid %d\n", childpid);
				printf("ratbox-services: running in background\n");
				return 0;
		}
	}

        if(!nofork)
        	write_pidfile();

	/* log requires time is set */
	open_logfile();

	mlog("ratbox-services started");

	signal(SIGHUP, sig_hup);
	signal(SIGTERM, sig_term);

	signal(SIGTRAP, SIG_IGN); /* Needed on FreeBSD and possibly others */
	signal(SIGPIPE, SIG_IGN);

	current_mark = 0;

	if((rserv_db = sqlite_open(DB_PATH, 0, errmsg)) == NULL)
	{
		mlog("ERR: Failed to open db file: %s", *errmsg);
		exit(-1);
	}

	init_events();

	/* balloc requires events */
        init_balloc();

	/* tools requires balloc */
	init_tools();

	/* conf requires log is opened */
        newconf_init();

	/* commands require cache */
	init_cache();
	init_scommand();
	init_ucommand();
	init_client();
	init_channel();

	/* load specific commands */
        add_scommand_handler(&error_command);
	add_scommand_handler(&mode_command);
	add_scommand_handler(&privmsg_command);

        add_ucommand_handler(NULL, &stats_ucommand, NULL);

	/* load our services.. */
#ifdef ENABLE_ALIS
	init_s_alis();
#endif
#ifdef ENABLE_OPERBOT
	init_s_operbot();
#endif
#ifdef ENABLE_USERSERV
	init_s_userserv();
#ifdef ENABLE_CHANSERV
	init_s_chanserv();
#endif
#endif
#ifdef ENABLE_JUPESERV
	init_s_jupeserv();
#endif

	first_time = CURRENT_TIME;

	/* must be done after adding services. */
	conf_parse(1);

	eventAdd("update_service_floodcount", update_service_floodcount, NULL, 1);
	eventAdd("check_rehash", check_rehash, NULL, 2);

	connect_to_server(NULL);

	/* enter main IO loop */
	read_io();

	return 0;
}

void sig_hup(int sig)
{
	need_rehash = 1;
	signal(SIGHUP, sig_hup);
}

void sig_term(int sig)
{
	die("Got signal SIGTERM");
}

void check_rehash(void *unused)
{
	if (need_rehash)
	{
		rehash(1); /* Caught a signal */
		need_rehash = 0;
	}
}

void
loc_sqlite_exec(db_callback cb, const char *format, ...)
{
	va_list args;
	char *errmsg;
	int i;

	va_start(args, format);
	if((i = sqlite_exec_vprintf(rserv_db, format, cb, NULL, &errmsg, args)))
	{
		mlog("fatal error: problem with db file: %s", errmsg);
		die("problem with db file");
	}
	va_end(args);
}

char *
rebuild_params(const char **parv, int parc, int start)
{
	static char buf[BUFSIZE];

	buf[0] = '\0';

	while(start < parc)
	{
		if(strlcat(buf, parv[start], sizeof(buf)) >= REASONLEN)
		{
			buf[REASONLEN] = '\0';
			break;
		}

		strlcat(buf, " ", sizeof(buf));

		start++;
	}

	return buf;
}

int
valid_servername(const char *name)
{
	int dots = 0;

	if(IsDigit(*name))
		return 0;

	for(; *name; name++)
	{
		if(!IsServChar(*name))
			return 0;
		else if(*name == '.')
			dots++;
	}

	if(!dots)
		return 0;

	return 1;
}

