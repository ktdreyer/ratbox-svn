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

#include "stdinc.h"
#include "rserv.h"
#include "conf.h"
#include "io.h"
#include "event.h"
#include "command.h"
#include "client.h"
#include "channel.h"
#include "log.h"
#include "c_init.h"
#include "service.h"

struct timeval system_time;

int die(const char *s)
{
    exit(0);
}

void
set_time(void)
{
	struct timeval newtime;

	newtime.tv_sec = newtime.tv_usec = 0;

	if(gettimeofday(&newtime, NULL) == -1)
	{
		die("Clock failure.");
	}

	system_time.tv_sec = newtime.tv_sec;
	system_time.tv_usec = newtime.tv_usec;
}

/* string_to_array()
 *   Changes a given buffer into an array of parameters.
 *   Taken from ircd-ratbox.
 *
 * inputs	- string to parse, array to put in
 * outputs	- number of parameters
 */
static inline int
string_to_array(char *string, char *parv[MAXPARA])
{
	char *p, *buf = string;
	int x = 1;

	parv[x] = NULL;
	while (*buf == ' ')	/* skip leading spaces */
		buf++;
	if(*buf == '\0')	/* ignore all-space args */
		return x;

	do
	{
		if(*buf == ':')	/* Last parameter */
		{
			buf++;
			parv[x++] = buf;
			parv[x] = NULL;
			return x;
		}
		else
		{
			parv[x++] = buf;
			parv[x] = NULL;
			if((p = strchr(buf, ' ')) != NULL)
			{
				*p++ = '\0';
				buf = p;
			}
			else
				return x;
		}
		while (*buf == ' ')
			buf++;
		if(*buf == '\0')
			return x;
	}
	while (x < MAXPARA - 1);

	if(*p == ':')
		p++;

	parv[x++] = p;
	parv[x] = NULL;
	return x;
}

void
parse_server(char *buf, int len)
{
	static char *parv[MAXPARA + 1];
	const char *command;
	char *s;
	char *ch;
	int parc;

	if(len > BUFSIZE)
		buf[BUFSIZE-1] = '\0';

	if((s = strchr(buf, '\n')) != NULL)
		*s = '\0';

	if((s = strchr(buf, '\r')) != NULL)
		*s = '\0';

	/* skip leading spaces.. */
	for(ch = buf; *ch == ' '; ch++)
		;

	parv[0] = server_p->name;

	if(*ch == ':')
	{
		ch++;

		parv[0] = ch;

		if((s = strchr(ch, ' ')) != NULL)
		{
			*s++ = '\0';
			ch = s;
		}

		while(*ch == ' ')
			ch++;
	}

	if(EmptyString(ch))
		return;

	command = ch;

	if((s = strchr(ch, ' ')) != NULL)
	{
		*s++ = '\0';
		ch = s;
	}

	while(*ch == ' ')
		ch++;

	if(EmptyString(ch))
		return;

	parc = string_to_array(ch, parv);

	handle_command(command, parv, parc);
}


int main(int argc, char *argv[])
{
	switch (fork())
	{
		case -1:
			perror("fork()");
			exit(3);
		case 0:
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
			if (setsid() == -1)
				exit(4);
			break;
		default:
			printf("daemonized.\n");
			return 0;
	}

	init_events();
	init_command();
	init_client();
	init_channel();
	init_log();

	/* load specific commands */
	add_scommand_handler(&mode_command);
/*	add_scommand_handler(&notice_command); */
	add_scommand_handler(&privmsg_command);

	/* load our services.. */
	add_service(&alis_service);

	set_time();
	config_file.first_time = CURRENT_TIME;

	conf_parse();

	connect_to_server(NULL);

	/* enter main IO loop */
	read_io();

	return 0;
}
