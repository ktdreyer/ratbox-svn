/* src/conf.c
 *  Contains code for parsing the config file
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "conf.h"
#include "fileio.h"
#include "tools.h"

struct _config_file config_file;
dlink_list conf_server_list;

static void parse_servinfo(char *line);
static void parse_admin(char *line);
static void parse_connect(char *line);

static void
conf_error_fatal(const char *format, ...)
{
	char buf[BUFSIZE];
	va_list args;

	if(EmptyString(format))
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	// log

	die("Problem parsing config file");
}


static void
conf_error(const char *format, ...)
{
	char buf[BUFSIZE];
	va_list args;

	if(EmptyString(format))
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	// log
}

void
conf_parse(void)
{
	FBFILE *conffile = fbopen(CONF_PATH, "r");
	char line[BUFSIZE];
	char *p;

	while(fbgets(line, sizeof(line), conffile))
	{
		if((p = strchr(line, '\n')) != NULL)
			*p = '\0';

		if((line[0] == '\0') || (line[0] == '#') || (line[1] != ':'))
			continue;

		switch(line[0])
		{
			case 'S':
				parse_servinfo(line+2);
				break;
			case 'A':
				parse_admin(line+2);
				break;
			case 'C':
				parse_connect(line+2);
				break;
			default:
				conf_error("%c: unknown configuration type", line[0]);
				break;
		}
	}

	fbclose(conffile);
}

static void
parse_servinfo(char *line)
{
	static char default_gecos[] = "services";
	const char *my_name = getfield(line);
	const char *my_gecos = getfield(NULL);
	const char *my_vhost = getfield(NULL);

	if(EmptyString(my_name))
		conf_error_fatal("S: Missing servername");

	if(EmptyString(my_gecos))
		my_gecos = default_gecos;

	if(strchr(my_name, ' ') != NULL)
		conf_error_fatal("S: servername cannot contain spaces");

	if(strchr(my_name, '.') == NULL)
		conf_error_fatal("S: servername must contain a '.'");

	config_file.my_name = my_strdup(my_name);
	config_file.my_gecos = my_strdup(my_gecos);

	if(my_vhost != NULL)
		config_file.vhost = my_strdup(my_vhost);
}

static void
parse_admin(char *line)
{
	const char *admin1 = getfield(line);
	const char *admin2 = getfield(NULL);
	const char *admin3 = getfield(NULL);

	if(!EmptyString(admin1))
		config_file.admin1 = my_strdup(admin1);
	if(!EmptyString(admin2))
		config_file.admin2 = my_strdup(admin2);
	if(!EmptyString(admin3))
		config_file.admin3 = my_strdup(admin3);
}

static void
parse_connect(char *line)
{
	struct conf_server *server;
	const char *server_name = getfield(line);
	const char *server_host = getfield(NULL);
	const char *server_port = getfield(NULL);
	const char *server_rpass = getfield(NULL);
	const char *server_spass = getfield(NULL);
	const char *server_vhost = getfield(NULL);
	int port;

	if(EmptyString(server_name) || EmptyString(server_name) ||
	   EmptyString(server_rpass) || EmptyString(server_spass))
	{
		conf_error("C: missing field");
		return;
	}

	server = my_calloc(1, sizeof(struct conf_server));
	server->name = my_strdup(server_name);
	server->host = my_strdup(server_host);
	server->rpass = my_strdup(server_rpass);
	server->spass = my_strdup(server_spass);

	if(server_vhost != NULL)
		server->vhost = my_strdup(server_vhost);

	if((port = atoi(server_port)) == 0)
		server->port = 6667;
	else
		server->port = port;

	dlink_add_alloc(server, &conf_server_list);
}

