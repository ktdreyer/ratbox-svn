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
#include "client.h"
#include "service.h"
#include "io.h"

struct _config_file config_file;
dlink_list conf_server_list;
dlink_list conf_oper_list;

static void parse_servinfo(char *line);
static void parse_admin(char *line);
static void parse_connect(char *line);
static void parse_service(char *line);
static void parse_oper(char *line);

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
			case 'M':
				parse_servinfo(line+2);
				break;
			case 'A':
				parse_admin(line+2);
				break;
			case 'C':
				parse_connect(line+2);
				break;
			case 'S':
				parse_service(line+2);
				break;
                        case 'O':
                                parse_oper(line+2);
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
	const char *server_pass = getfield(NULL);
	const char *server_vhost = getfield(NULL);

	if(EmptyString(server_name) || EmptyString(server_host) ||
	   EmptyString(server_pass))
	{
		conf_error("C: missing field");
		return;
	}

	server = my_calloc(1, sizeof(struct conf_server));
	server->name = my_strdup(server_name);
	server->host = my_strdup(server_host);
	server->pass = my_strdup(server_pass);

	if(server_vhost != NULL)
		server->vhost = my_strdup(server_vhost);

        server->defport = atoi(server_port);

        /* if no port was specified, set it to 6667 with no a/c */
        if(!server->defport)
                server->defport = -6667;

	dlink_add_tail_alloc(server, &conf_server_list);
}

static void
parse_service(char *line)
{
	struct client *client_p;
	const char *s_id = getfield(line);
	const char *s_name = getfield(NULL);
	const char *s_username = getfield(NULL);
	const char *s_host = getfield(NULL);
	const char *s_info = getfield(NULL);
	const char *s_opered = getfield(NULL);
	int reintroduce = 0;

	if((client_p = find_service_id(s_id)) == NULL)
	{
		conf_error("Z: unknown service %s", s_id);
		return;
	}

	if(EmptyString(s_id) || EmptyString(s_name) ||
	   EmptyString(s_username) || EmptyString(s_host) ||
	   EmptyString(s_info) || EmptyString(s_opered))
	{
		conf_error("Z: missing fields for service %s", s_id);
		return;
	}

	if(strchr(s_name, '.') != NULL)
	{
		conf_error("Z: invalid service name %s", s_name);
		return;
	}

	/* need to reintroduce the service */
	if(irccmp(client_p->name, s_name) ||
	   irccmp(client_p->service->username, s_username) ||
	   irccmp(client_p->service->host, s_host) ||
	   irccmp(client_p->info, s_info))
	{
		reintroduce = 1;
		sendto_server(":%s QUIT :reintroducing service",
				client_p->name);
		del_client(client_p);
	}

	if(irccmp(client_p->name, s_name))
		strlcpy(client_p->name, s_name, sizeof(client_p->name));

	if(irccmp(client_p->service->username, s_username))
		strlcpy(client_p->service->username, s_username,
			sizeof(client_p->service->username));

	if(irccmp(client_p->service->host, s_host))
		strlcpy(client_p->service->host, s_host,
			sizeof(client_p->service->host));

	if(irccmp(client_p->info, s_info))
		strlcpy(client_p->info, s_info, sizeof(client_p->info));

	client_p->service->opered = atoi(s_opered);

	if(reintroduce)
	{
		add_client(client_p);
		introduce_service(client_p);
	}
}

static void
split_user_host(const char *userhost, const char **user, const char **host)
{
        static const char star[] = "*";
        static char uh[USERHOSTLEN+1];
        char *p;

        strlcpy(uh, userhost, sizeof(uh));

        if((p = strchr(uh, '@')) != NULL)
        {
                *p++ = '\0';
                *user = &uh[0];
                *host = p;
        }
        else
        {
                *user = star;
                *host = userhost;
        }
}


static void
parse_oper(char *line)
{
        struct conf_oper *oper_p;
        const char *o_host = getfield(line);
        const char *o_pass = getfield(NULL);
        const char *o_name = getfield(NULL);
        const char *os_username;
        const char *os_host;

        if(EmptyString(o_host) || EmptyString(o_pass) ||
           EmptyString(o_name))
        {
                conf_error("O: missing fields");
                return;
        }

        split_user_host(o_host, &os_username, &os_host);

        oper_p = my_malloc(sizeof(struct conf_oper));
        oper_p->username = my_strdup(os_username);
        oper_p->host = my_strdup(os_host);
        oper_p->name = my_strdup(o_name);
        oper_p->pass = my_strdup(o_pass);

	dlink_add_alloc(oper_p, &conf_oper_list);
}

struct conf_server *
find_conf_server(const char *name)
{
        struct conf_server *server;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_server_list.head)
        {
                server = ptr->data;

                if(!strcasecmp(name, server->name))
                        return server;
        }

        return NULL;
}

struct conf_oper *
find_oper(struct connection_entry *conn_p, const char *name)
{
        struct conf_oper *oper_p;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_oper_list.head)
        {
                oper_p = ptr->data;

                if(!strcasecmp(conn_p->username, oper_p->username) &&
                   !strcasecmp(conn_p->host, oper_p->host) &&
                   !strcasecmp(name, oper_p->name))
                        return oper_p;
        }

        return NULL;
}

int
is_conf_oper(const char *username, const char *host)
{
        struct conf_oper *oper_p;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_oper_list.head)
        {
                oper_p = ptr->data;

                if(!strcasecmp(username, oper_p->username) &&
                   !strcasecmp(host, oper_p->host))
                        return 1;
        }

        return 0;
}
