/* src/rserv.c
 *  Contains generic functions for services.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#include "stdinc.h"
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
#include "fileio.h"
#include "serno.h"

struct timeval system_time;

void
die(const char *reason)
{
	sendto_server(":%s WALLOPS :services terminated: %s", MYNAME, reason);
	slog("ratbox-services terminated: (%s)", reason);
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
	FBFILE *fb;
	char buf[32];
	pid_t filepid;

	if((fb = fbopen(PID_PATH, "r")) != NULL)
	{
		if(fbgets(buf, 20, fb) != NULL && errno != ENOENT)
		{
			filepid = atoi(buf);
			if(!kill(filepid, 0))
			{
				printf("ratbox-services: daemon already running\n");
//				exit(-1);
			}
		}

		fbclose(fb);
	}
}

static void
write_pidfile(void)
{
	FBFILE *fb;
	char buf[32];
	
	if((fb = fbopen(PID_PATH, "w")) != NULL)
	{
		snprintf(buf, sizeof(buf), "%u\n", (unsigned int) getpid());

		if(fbputs(buf, fb) == -1)
			slog("ERR: Error writing to pid file %s (%s)",
			     PID_PATH, strerror(errno));

		fbclose(fb);
	}
	else
		slog("ERR: Error opening pid file %s", PID_PATH);
}

static void
print_help(void)
{
	printf("rserv [-h|-v|-f]\n");
	printf(" -h show this help\n");
	printf(" -v show version\n");
	printf(" -f foreground mode\n");
}

int 
main(int argc, char *argv[])
{
	char c;
	int nofork = 0;
	int childpid;

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

	/* time must be set before logs */
	set_time();
	init_log();

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

	write_pidfile();

	slog("ratbox-services started");

	init_events();
	init_scommand();
	init_ucommand();
	init_client();
	init_channel();

	/* load specific commands */
        add_scommand_handler(&error_command);
	add_scommand_handler(&mode_command);
	add_scommand_handler(&privmsg_command);

        add_ucommand_handler(&help_ucommand);
        add_ucommand_handler(&stats_ucommand);

	/* load our services.. */
	init_s_alis();

	config_file.first_time = CURRENT_TIME;

	/* must be done after adding services. */
	conf_parse();

	eventAdd("update_service_floodcount", update_service_floodcount, NULL, 1);

	connect_to_server(NULL);

	/* enter main IO loop */
	read_io();

	return 0;
}
