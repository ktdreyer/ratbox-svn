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
#ifndef ANFL_LAPTOP
#include <sqlite.h>
#endif
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

#ifndef ANFL_LAPTOP
struct sqlite *rserv_db;
#endif

struct timeval system_time;

int have_md5_crypt;
int current_mark;
int testing_conf = 0;

static int need_rehash = 0;
static void sig_hup(int);
static void sig_term(int);
static void check_rehash(void *);

void
die(const char *format, ...)
{
	char buf[BUFSIZE];
	va_list args;

#ifndef ANFL_LAPTOP
	if(rserv_db)
		sqlite_close(rserv_db);
#endif

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(testing_conf)
	{
		fprintf(stderr, "Services terminated: (%s)\n", buf);
		exit(1);
	}

	sendto_all(0, "Services terminated: (%s)", buf);
	mlog("ratbox-services terminated: (%s)", buf);
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
	printf("ratbox-services [-h|-v|-f|-t]\n");
	printf(" -h show this help\n");
	printf(" -v show version\n");
	printf(" -f foreground mode\n");
	printf(" -t test config\n");
}

static void
check_md5_crypt(void)
{
	if(strcmp((crypt("validate", "$1$tEsTiNg1")), "$1$tEsTiNg1$Orp/Maa6pOxfOpGWjmtVE/") == 0)
		have_md5_crypt = 1;
	else
		have_md5_crypt = 0;
}

static void
print_startup(int pid, int nofork)
{
	printf("ratbox-services: version %s(%s)\n",
		RSERV_VERSION, SERIALNUM);
	printf("ratbox-services: pid %d\n", pid);
	printf("ratbox-services: running in %s\n",
		nofork ? "foreground" : "background");
}

int 
main(int argc, char *argv[])
{
#ifndef ANFL_LAPTOP
	char *errmsg;
#endif
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

	while((c = getopt(argc, argv, "hvft")) != -1)
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
			case 't':
				testing_conf = 1;
				break;
		}
	}

	set_time();

#ifdef __CYGWIN__
        nofork = 1;
#endif

	if(testing_conf)
		nofork = 1;

        if(!testing_conf)
	{
        	check_pidfile();

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
					print_startup(childpid, nofork);
					return 0;
			}
		}
		else
			print_startup(getpid(), nofork);
	}

	/* log requires time is set */
	open_logfile();

	mlog("ratbox-services started%s",
		testing_conf ? " (config test)" : "");

	signal(SIGHUP, sig_hup);
	signal(SIGTERM, sig_term);

	signal(SIGTRAP, SIG_IGN); /* Needed on FreeBSD and possibly others */
	signal(SIGPIPE, SIG_IGN);

	current_mark = 0;

	init_events();

	/* balloc requires events */
        init_balloc();

	/* tools requires balloc */
	init_tools();

#ifndef ANFL_LAPTOP
	if((rserv_db = sqlite_open(DB_PATH, 0, &errmsg)) == NULL)
	{
		die("Failed to open db file: %s", errmsg);
	}
#endif

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
#ifdef ENABLE_OPERSERV
	init_s_operserv();
#endif
#ifdef ENABLE_JUPESERV
	init_s_jupeserv();
#endif
#ifdef ENABLE_GLOBAL
	init_s_global();
#endif

	first_time = CURRENT_TIME;

	if(testing_conf)
		fprintf(stderr, "Conf check started\n");

	/* conf requires log is opened */
        newconf_init();
	/* must be done after adding services. */
	conf_parse(1);

	if(testing_conf)
	{
		fprintf(stderr, "\nConf check finished\n");
		exit(0);
	}

	eventAdd("update_service_floodcount", update_service_floodcount, 
		NULL, 1);
	eventAdd("check_rehash", check_rehash, NULL, 2);

       	write_pidfile();

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
#ifndef ANFL_LAPTOP
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
#endif
}

char *
rebuild_params(const char **parv, int parc, int start)
{
	static char buf[BUFSIZE];

	buf[0] = '\0';

	if (start < parc)
	{
		strlcat(buf, parv[start], sizeof(buf));
		start++;

		for(; start < parc; start++)
		{
			strlcat(buf, " ", sizeof(buf));

			if(strlcat(buf, parv[start], sizeof(buf)) >= sizeof(buf))
				break;
		}
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

