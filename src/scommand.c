/* src/command.c
 *  Contains code for handling of commands received.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "command.h"
#include "rserv.h"
#include "tools.h"
#include "io.h"
#include "conf.h"
#include "client.h"
#include "serno.h"

static dlink_list scommand_table[MAX_SCOMMAND_HASH];

static void c_admin(struct client *, char *parv[], int parc);
static void c_ping(struct client *, char *parv[], int parc);
static void c_stats(struct client *, char *parv[], int parc);
static void c_trace(struct client *, char *parv[], int parc);
static void c_version(struct client *, char *parv[], int parc);

static struct scommand_handler admin_command = { "ADMIN", c_admin, 0 };
static struct scommand_handler ping_command = { "PING", c_ping, 0 };
static struct scommand_handler stats_command = { "STATS", c_stats, 0 };
static struct scommand_handler trace_command = { "TRACE", c_trace, 0 };
static struct scommand_handler version_command = { "VERSION", c_version, 0 };

void
init_command(void)
{
	add_scommand_handler(&admin_command);
	add_scommand_handler(&ping_command);
	add_scommand_handler(&stats_command);
	add_scommand_handler(&trace_command);
	add_scommand_handler(&version_command);
}

static int
hash_command(const char *p)
{
	unsigned int hash_val = 0;

	while(*p)
	{
		hash_val += ((int) (*p) & 0xDF);
		p++;
	}

	return(hash_val % MAX_SCOMMAND_HASH);
}

static void
handle_command_unknown(const char *command, char *parv[], int parc)
{
	struct scommand_handler *handler;
	dlink_node *ptr;
	unsigned int hashv = hash_command(command);
	
	DLINK_FOREACH(ptr, scommand_table[hashv].head)
	{
		handler = ptr->data;
		if(!strcasecmp(command, handler->cmd))
		{
			if(handler->flags & FLAGS_UNKNOWN)
				handler->func(NULL, parv, parc);
			return;
		}
	}
}

static void
handle_command_client(struct client *client_p, const char *command, 
			char *parv[], int parc)
{
	struct scommand_handler *handler;
	dlink_node *ptr;
	unsigned int hashv = hash_command(command);
	
	DLINK_FOREACH(ptr, scommand_table[hashv].head)
	{
		handler = ptr->data;
		if(!strcasecmp(command, handler->cmd))
		{
			handler->func(client_p, parv, parc);
			break;
		}
	}
}

void
handle_command(const char *command, char *parv[], int parc)
{
	struct client *client_p;

	client_p = find_client(parv[0]);

	if(client_p != NULL)
		handle_command_client(client_p, command, parv, parc);
	else
		handle_command_unknown(command, parv, parc);
}

void
add_scommand_handler(struct scommand_handler *chandler)
{
	unsigned int hashv;

	if(chandler == NULL || EmptyString(chandler->cmd))
		return;

	hashv = hash_command(chandler->cmd);
	dlink_add_alloc(chandler, &scommand_table[hashv]);
}

static void
c_admin(struct client *client_p, char *parv[], int parc)
{
	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(!IsUser(client_p))
		return;

	sendto_server(":%s 256 %s :Administrative info about %s",
		      MYNAME, parv[0], MYNAME);

	if(!EmptyString(config_file.admin1))
		sendto_server(":%s 257 %s :%s", MYNAME, parv[0], config_file.admin1);
	if(!EmptyString(config_file.admin2))
		sendto_server(":%s 258 %s :%s", MYNAME, parv[0], config_file.admin2);
	if(!EmptyString(config_file.admin3))
		sendto_server(":%s 259 %s :%s", MYNAME, parv[0], config_file.admin3);
}

static void
c_ping(struct client *client_p, char *parv[], int parc)
{
	if(parc < 2 || EmptyString(parv[1]))
		return;

	sendto_server(":%s PONG %s :%s", MYNAME, MYNAME, parv[0]);
}

static void
c_trace(struct client *client_p, char *parv[], int parc)
{
	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(!IsUser(client_p))
		return;

	sendto_server(":%s 262 %s :End of TRACE", MYNAME, parv[0]);
}
	
static void
c_version(struct client *client_p, char *parv[], int parc)
{
	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(IsUser(client_p))
		sendto_server(":%s 351 %s rserv-0.1(%s). %s A TS",
			      MYNAME, parv[0], SERIALNUM, MYNAME);
}

static void
c_stats(struct client *client_p, char *parv[], int parc)
{
	char statchar;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(!IsUser(client_p))
		return;

	statchar = parv[1][0];
	switch(statchar)
	{
		case 'c': case 'C':
		case 'n': case 'N':
		{
			struct conf_server *conf_p;
			dlink_node *ptr;

			DLINK_FOREACH(ptr, conf_server_list.head)
			{
				conf_p = ptr->data;

				sendto_server(":%s 213 %s C *@%s A %s %d uplink",
					      MYNAME, parv[0], conf_p->name,
					      conf_p->name, conf_p->port);
			}
		}
			break;

		case 'h': case 'H':
		{
			struct conf_server *conf_p;
			dlink_node *ptr;

			DLINK_FOREACH(ptr, conf_server_list.head)
			{
				conf_p = ptr->data;

				sendto_server(":%s 244 %s H * * %s",
					      MYNAME, parv[0], conf_p->name);
			}
		}
			break;

		case 'u':
		{
			time_t seconds;
			int days, hours, minutes;

			seconds = CURRENT_TIME - config_file.first_time;

			days = (int) (seconds / 86400);
			seconds %= 86400;
			hours = (int) (seconds / 3600);
			hours %= 3600;
			minutes = (int) (seconds / 60);
			seconds %= 60;

			sendto_server(":%s 242 %s :Server Up %d day%s, %d:%02d:%02ld",
				      MYNAME, parv[0], days, (days == 1) ? "" : "s",
				      hours, minutes, seconds);
		}
			break;

		case 'v': case 'V':
		{
			time_t seconds;
			int days, hours, minutes;

			seconds = CURRENT_TIME - server_p->first_time;

			days = (int) (seconds / 86400);
			seconds %= 86400;
			hours = (int) (seconds / 3600);
			hours %= 3600;
			minutes = (int) (seconds / 60);
			seconds %= 60;

			sendto_server(":%s 249 %s V :%s (AutoConn.!*@*) Idle: %d "
				      "SendQ: %d Connected %d day%s, %d:%02d:%02ld",
				      MYNAME, parv[0], server_p->name, 
				      (CURRENT_TIME - server_p->last_time), 0,
				      days, (days == 1) ? "" : "s", hours,
				      minutes, seconds);
		}
			break;

		default:
			break;
	}

	sendto_server(":%s 219 %s %c :End of /STATS report",
		      MYNAME, parv[0], statchar);
}
