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
#include "fileio.h"
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

FBFILE *conf_fbfile_in;
extern char yytext[];

void
set_default_conf(void)
{
	config_file.dcc_low_port = 1025;
	config_file.dcc_high_port = 65000;

	config_file.ping_time = 300;
	config_file.reconnect_time = 300;
}

void
validate_conf(void)
{
	if(EmptyString(config_file.name))
		die("No servername specified");

	if(EmptyString(config_file.gecos))
		config_file.gecos = my_strdup("ratbox services");
}


void
conf_parse(void)
{
        if((conf_fbfile_in = fbopen(CONF_PATH, "r")) == NULL)
                die("Failed to open config file");

	set_default_conf();
        yyparse();
	validate_conf();

        fbclose(conf_fbfile_in);
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

                if(match(oper_p->username, conn_p->username) &&
                   match(oper_p->host, conn_p->host) &&
                   !strcasecmp(name, oper_p->name))
                        return oper_p;
        }

        return NULL;
}

struct conf_oper *
find_conf_oper(const char *username, const char *host)
{
        struct conf_oper *oper_p;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_oper_list.head)
        {
                oper_p = ptr->data;

                if(match(oper_p->username, username) &&
                   match(oper_p->host, host))
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

	slog("conf error: \"%s\", line %d: %s at '%s'", conffilebuf, lineno + 1, msg, newlinebuf);
}

int
conf_fbgets(char *lbuf, int max_size)
{
	char *buff;

	if((buff = fbgets(lbuf, max_size, conf_fbfile_in)) == NULL)
		return (0);

	return (strlen(lbuf));
}
