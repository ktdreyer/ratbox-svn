/* src/conf.c
 *   Contains code for parsing the config file
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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

	config_file.client_flood_max = 20;
	config_file.client_flood_max_ignore = 30;
	config_file.client_flood_ignore_time = 300;
	config_file.client_flood_time = 60;
	
	config_file.disable_uregister = 0;
	config_file.uregister_time = 60;
	config_file.uregister_amount = 5;
	config_file.uhregister_time = 86400;	/* 1 day */
	config_file.uhregister_amount = 2;
	config_file.uregister_email = 0;
	config_file.uexpire_time = 2419200;	/* 4 weeks */
	config_file.allow_set_password = 1;
	config_file.allow_resetpass = 0;
	config_file.allow_set_email = 1;
	config_file.umax_logins = 5;

	config_file.disable_cregister = 0;
	config_file.cregister_time = 60;
	config_file.cregister_amount = 5;
	config_file.chregister_time = 86400;	/* 1 day */
	config_file.chregister_amount = 4;
	config_file.cexpire_time = 2419200; 	/* 4 weeks */
	config_file.cmax_bans = 50;
	config_file.cexpireban_frequency = DEFAULT_EXPIREBAN_FREQUENCY;
	config_file.cenforcetopic_frequency = DEFAULT_ENFORCETOPIC_FREQUENCY;

	config_file.nmax_nicks = 2;
	config_file.nallow_set_warn = 1;

	config_file.bs_unban_time = 1209600;	/* 2 weeks */
	config_file.bs_temp_workaround = 0;
	config_file.bs_autosync_frequency = DEFAULT_AUTOSYNC_FREQUENCY;

	my_free(config_file.nwarn_string);
	config_file.nwarn_string = my_strdup("This nickname is registered, you may "
				"be disconnected if a user regains this nickname.");

	config_file.oper_score = 3;
	config_file.jupe_score = 15;
	config_file.unjupe_score = 15;
	config_file.pending_time = 1800;

	config_file.max_matches = 60;
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

	if(config_file.max_matches >= 250)
		config_file.max_matches = 250;
	else if(config_file.max_matches <= 0)
		config_file.max_matches = 250;

	if(config_file.umax_logins < 0)
		config_file.umax_logins = 0;
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

	/* if we havent sent our burst, the following will just break */
	if(!testing_conf && sent_burst)
	{
		DLINK_FOREACH(ptr, service_list.head)
		{
			target_p = ptr->data;

			if(ServiceIntroduced(target_p))
			{
				if(ServiceDisabled(target_p))
				{
					deintroduce_service(target_p);
					continue;
				}
				else if(ServiceReintroduce(target_p))
				{
					reintroduce_service(target_p);
					continue;
				}
			}
			else if(!ServiceDisabled(target_p))
				introduce_service(target_p);

			ClearServiceReintroduce(target_p);
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

struct flag_table
{
	char mode;
	int flag;
};

static struct flag_table oper_flags[] =
{
	{ 'D', CONF_OPER_DCC		},
	{ 'A', CONF_OPER_ADMIN		},
#if 0
	{ 'C', CONF_OPER_CHANSERV	},
	{ 'c', CONF_OPER_CREGISTER	},
	{ 'U', CONF_OPER_USERSERV	},
	{ 'u', CONF_OPER_UREGISTER	},
	{ 'B', CONF_OPER_OPERBOT	},
	{ 'J', CONF_OPER_JUPESERV	},
	{ 'G', CONF_OPER_GLOBAL		},
	{ 'O', CONF_OPER_OPERSERV	},
#endif
	{ '\0',0 }
};

const char *
conf_oper_flags(int flags)
{
	static char buf[20];
	char *p = buf;
	int i;

	for(i = 0; oper_flags[i].mode; i++)
	{
		if(flags & oper_flags[i].flag)
			*p++ = oper_flags[i].mode;
	}

	*p = '\0';
	return buf;
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
