/* src/conf.c
 *   Contains code for parsing the config file
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "conf.h"
#include "tools.h"
#include "client.h"
#include "service.h"
#include "io.h"
#include "log.h"

struct _config_file config_file;
dlink_list conf_server_list;
dlink_list conf_oper_list;

time_t first_time;

extern int yyparse();           /* defined in y.tab.c */
extern char linebuf[];
extern char conffilebuf[BUFSIZE + 1];
int scount = 0;                 /* used by yyparse(), etc */

FILE *conf_fbfile_in;
extern char yytext[];

static void
set_default_conf(void)
{
	config_file.dcc_low_port = 1025;
	config_file.dcc_high_port = 65000;

	config_file.ping_time = 300;
	config_file.reconnect_time = 300;

	config_file.ratbox = 1;

	config_file.disable_uregister = 0;
	config_file.uregister_time = 60;
	config_file.uregister_amount = 10;

	config_file.disable_cregister = 0;
	config_file.cregister_time = 60;
	config_file.cregister_amount = 5;

	config_file.oper_score = 3;
	config_file.jupe_score = 15;
	config_file.unjupe_score = 15;
	config_file.pending_time = 1800;
}

static void
validate_conf(void)
{
	if(EmptyString(config_file.name))
		die("No servername specified");

	if(EmptyString(config_file.gecos))
		config_file.gecos = my_strdup("ratbox services");

	if(config_file.dcc_low_port <= 1024)
		config_file.dcc_low_port = 1025;

	if(config_file.dcc_high_port < config_file.dcc_low_port)
		config_file.dcc_high_port = 65000;

	if(config_file.ping_time <= 0)
		config_file.ping_time = 300;

	if(config_file.reconnect_time <= 0)
		config_file.reconnect_time = 300;

	if(config_file.pending_time <= 0)
		config_file.pending_time = 1800;
}

static void
clear_old_conf(void)
{
	struct conf_oper *oper_p;
	dlink_node *ptr;
	dlink_node *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, conf_oper_list.head)
	{
		oper_p = ptr->data;

		/* still in use */
		if(oper_p->refcount)
			SetConfDead(oper_p);
		else
			free_conf_oper(oper_p);

		dlink_destroy(ptr, &conf_oper_list);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, conf_server_list.head)
	{
		free_conf_server(ptr->data);
		dlink_destroy(ptr, &conf_server_list);
	}	
}

void
conf_parse(int cold)
{
	struct client *target_p;
	dlink_node *ptr;

        if((conf_fbfile_in = fopen(CONF_PATH, "r")) == NULL)
	{
		if(!cold)
		{
			mlog("Failed to open config file");
			sendto_all(0, "Failed to open config file");
			return;
		}
		else
	                die("Failed to open config file");
	}

	if(!cold)
		clear_old_conf();
	else
		set_default_conf();

        yyparse();
	validate_conf();

	DLINK_FOREACH(ptr, service_list.head)
	{
		target_p = ptr->data;

		if(target_p->service->reintroduce)
		{
			/* not linked anywhere.. so dont have to! */
			if(finished_bursting)
				reintroduce_service(ptr->data);

			target_p->service->reintroduce = 0;
		}
	}
			
        fclose(conf_fbfile_in);
}

void
rehash(int sig)
{
	if(sig)
	{
		mlog("services rehashing: got SIGHUP");
		sendto_all(0, "services rehashing: got SIGHUP");
	}

	reopen_logfiles();

	conf_parse(0);
}

void
free_conf_oper(struct conf_oper *conf_p)
{
	my_free(conf_p->name);
	my_free(conf_p->pass);
	my_free(conf_p->username);
	my_free(conf_p->host);
	my_free(conf_p->server);
	my_free(conf_p);
}

void
free_conf_server(struct conf_server *conf_p)
{
	my_free(conf_p->name);
	my_free(conf_p->host);
	my_free(conf_p->pass);
	my_free(conf_p->vhost);
}

void
deallocate_conf_oper(struct conf_oper *conf_p)
{
	conf_p->refcount--;

	/* marked as dead, now unused, free. */
	if(ConfDead(conf_p) && !conf_p->refcount)
		free_conf_oper(conf_p);
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
find_conf_oper(const char *username, const char *host, const char *server)
{
        struct conf_oper *oper_p;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_oper_list.head)
        {
                oper_p = ptr->data;

                if(match(oper_p->username, username) &&
                   match(oper_p->host, host) &&
		   (EmptyString(oper_p->server) || match(oper_p->server, server)))
                        return oper_p;
        }

        return NULL;
}

/*
 * yyerror
 *
 * inputs	- message from parser
 * output	- none
 * side effects	- message to opers and log file entry is made
 */
void
yyerror(const char *msg)
{
	char newlinebuf[BUFSIZE];

	strip_tabs(newlinebuf, (const unsigned char *) linebuf, strlen(linebuf));

	sendto_all(0, "\"%s\", line %d: %s at '%s'",
                   conffilebuf, lineno + 1, msg, newlinebuf);

	mlog("conf error: \"%s\", line %d: %s at '%s'", conffilebuf, lineno + 1, msg, newlinebuf);
}

int
conf_fbgets(char *lbuf, int max_size)
{
	char *buff;

	if((buff = fgets(lbuf, max_size, conf_fbfile_in)) == NULL)
		return (0);

	return (strlen(lbuf));
}
