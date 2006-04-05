/* src/service.c
 *   Contains code for handling interaction with our services.
 *  
 * Copyright (C) 2003-2006 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2006 ircd-ratbox development team
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

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#include "service.h"
#include "client.h"
#include "scommand.h"
#include "rserv.h"
#include "conf.h"
#include "io.h"
#include "log.h"
#include "ucommand.h"
#include "cache.h"
#include "channel.h"
#include "s_userserv.h"

dlink_list service_list;

void
init_services(void)
{
	struct client *service_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		/* generate all our services a UID.. */
		strlcpy(service_p->uid, generate_uid(), sizeof(service_p->uid));

		if(service_p->service->init)
			(service_p->service->init)();
	}
}

typedef int (*bqcmp)(const void *, const void *);
static int
scmd_sort(struct service_command *one, struct service_command *two)
{
	return strcasecmp(one->cmd, two->cmd);
}

static int
scmd_compare(const char *name, struct service_command *cmd)
{
	return strcasecmp(name, cmd->cmd);
}

static void
load_service_help(struct client *service_p)
{
	struct service_command *scommand;
	struct ucommand_handler *ucommand;
	char filename[PATH_MAX];
	dlink_node *ptr;
	int maxlen = service_p->service->command_size / sizeof(struct service_command);
	unsigned long i;

        if(service_p->service->command == NULL)
		return;

	scommand = service_p->service->command;

	snprintf(filename, sizeof(filename), "%s/%s/index",
		HELP_PATH, lcase(service_p->service->id));

	service_p->service->help = cache_file(filename, "index");

	snprintf(filename, sizeof(filename), "%s/%s/index-admin",
		HELP_PATH, lcase(service_p->service->id));

	service_p->service->helpadmin = cache_file(filename, "index-admin");

	for(i = 0; i < maxlen; i++)
	{
		snprintf(filename, sizeof(filename), "%s/%s/",
			HELP_PATH, lcase(service_p->service->id));

		/* we cant lcase() twice in one function call */
		strlcat(filename, lcase(scommand[i].cmd),
			sizeof(filename));

		scommand[i].helpfile = cache_file(filename, scommand[i].cmd);
	}

	DLINK_FOREACH(ptr, service_p->service->ucommand_list.head)
	{
		ucommand = ptr->data;

	        /* now see if we can load a helpfile.. */
        	snprintf(filename, sizeof(filename), "%s/%s/u-",
                	 HELP_PATH, lcase(service_p->service->id));
	        strlcat(filename, lcase(ucommand->cmd), sizeof(filename));

        	ucommand->helpfile = cache_file(filename, ucommand->cmd);
	}
}

static void
clear_service_help(struct client *service_p)
{
	struct service_command *scommand;
	struct ucommand_handler *ucommand;
	dlink_node *ptr;
	int maxlen = service_p->service->command_size / sizeof(struct service_command);
	int i;
	
	if(service_p->service->command == NULL)
		return;

	scommand = service_p->service->command;

	free_cachefile(service_p->service->help);
	free_cachefile(service_p->service->helpadmin);
	service_p->service->help = NULL;
	service_p->service->helpadmin = NULL;

	for(i = 0; i < maxlen; i++)
	{
		free_cachefile(scommand[i].helpfile);
		scommand[i].helpfile = NULL;
	}

	DLINK_FOREACH(ptr, service_p->service->ucommand_list.head)
	{
		ucommand = ptr->data;

		free_cachefile(ucommand->helpfile);
		ucommand->helpfile = NULL;
	}
}

void
rehash_help(void)
{
	struct ucommand_handler *ucommand;
	char filename[PATH_MAX];
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		clear_service_help(ptr->data);
		load_service_help(ptr->data);
	}

	DLINK_FOREACH(ptr, ucommand_list.head)
	{
		ucommand = ptr->data;

		free_cachefile(ucommand->helpfile);

        	snprintf(filename, sizeof(filename), "%s/main/u-",
                	 HELP_PATH);
	        strlcat(filename, lcase(ucommand->cmd), sizeof(filename));

        	ucommand->helpfile = cache_file(filename, ucommand->cmd);
	}
}

struct client *
add_service(struct service_handler *service)
{
	struct client *client_p;
	int maxlen = service->command_size / sizeof(struct service_command);

	if(strchr(service->name, '.') != NULL)
	{
		mlog("ERR: Invalid service name %s", client_p->name);
		return NULL;
	}

	if((client_p = find_client(service->name)) != NULL)
	{
		if(IsService(client_p))
		{
			mlog("ERR: Tried to add duplicate service %s", service->name);
			return NULL;
		}
		else if(IsServer(client_p))
		{
			mlog("ERR: A server exists with service name %s?!", service->name);
			return NULL;
		}
		else if(IsUser(client_p))
		{
			if(client_p->user->tsinfo <= 1)
				die(1, "services conflict");

			/* we're about to collide it. */
			exit_client(client_p);
		}
	}

	/* now we need to sort the command array */
	if(service->command)
		qsort(service->command, maxlen,
			sizeof(struct service_command), (bqcmp) scmd_sort);

	client_p = my_malloc(sizeof(struct client));
	client_p->service = my_malloc(sizeof(struct service));

	strlcpy(client_p->name, service->name, sizeof(client_p->name));
	strlcpy(client_p->service->username, service->username,
		sizeof(client_p->service->username));
	strlcpy(client_p->service->host, service->host,
		sizeof(client_p->service->host));
	strlcpy(client_p->info, service->info, sizeof(client_p->info));
	strlcpy(client_p->service->id, service->id, sizeof(client_p->service->id));
	client_p->service->command = service->command;
	client_p->service->command_size = service->command_size;
        client_p->service->ucommand = service->ucommand;
	client_p->service->init = service->init;
        client_p->service->stats = service->stats;
	client_p->service->loglevel = 1;

        client_p->service->flood_max = service->flood_max;
        client_p->service->flood_max_ignore = service->flood_max_ignore;

	dlink_add_tail(client_p, &client_p->listnode, &service_list);

        if(service->ucommand != NULL)
                add_ucommands(client_p, service->ucommand);

	open_service_logfile(client_p);
	load_service_help(client_p);

	return client_p;
}

struct client *
find_service_id(const char *name)
{
	struct client *client_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		client_p = ptr->data;

		if(!strcasecmp(client_p->service->id, name))
			return client_p;
	}

	return NULL;
}

void
introduce_service(struct client *target_p)
{
	if(ConnTS6(server_p))
		sendto_server("UID %s 1 1 +iDS%s %s %s 0 %s :%s",
				target_p->name, ServiceOpered(target_p) ? "o" : "",
				target_p->service->username,
				target_p->service->host, target_p->uid,
				target_p->info);
	else
		sendto_server("NICK %s 1 1 +iDS%s %s %s %s :%s",
				target_p->name, ServiceOpered(target_p) ? "o" : "",
				target_p->service->username,
				target_p->service->host, MYNAME, target_p->info);

	SetServiceIntroduced(target_p);
	add_client(target_p);
}

void
introduce_service_channels(struct client *target_p)
{
	struct channel *chptr;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, target_p->service->channels.head)
	{
		chptr = ptr->data;

		sendto_server(":%s SJOIN %lu %s %s :@%s",
				MYNAME, (unsigned long) chptr->tsinfo, 
				chptr->name, chmode_to_string(&chptr->mode), 
				target_p->name);
	}
}

void
introduce_services()
{
	struct client *service_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		if(ServiceDisabled(service_p))
			continue;

		introduce_service(ptr->data);
	}
}

void
introduce_services_channels()
{
	struct client *service_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		if(!ServiceDisabled(service_p))
			introduce_service_channels(ptr->data);
	}
}

void
reintroduce_service(struct client *target_p)
{
	sendto_server(":%s QUIT :Updating information", target_p->name);
	del_client(target_p);
	introduce_service(target_p);
	introduce_service_channels(target_p);

	ClearServiceReintroduce(target_p);
}

void
deintroduce_service(struct client *target_p)
{
	sendto_server(":%s QUIT :Disabled", target_p->name);
	ClearServiceIntroduced(target_p);
	del_client(target_p);
}

void
update_service_floodcount(void *unused)
{
	struct client *client_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		client_p = ptr->data;

		client_p->service->flood -= 5;

		if(client_p->service->flood < 0)
			client_p->service->flood = 0;
	}
}

static void
handle_service_help_index(struct client *service_p, struct client *client_p)
{
	struct cachefile *fileptr;
	struct cacheline *lineptr;
	dlink_node *ptr;
	int i;

	/* if this service has short help enabled, or there is no index 
	 * file and theyre either unopered (so cant see admin file), 
	 * or theres no admin file.
	 */
	if(ServiceShortHelp(service_p) ||
	   (!service_p->service->help &&
	    (!client_p->user->oper || !service_p->service->helpadmin)))
	{
		char buf[BUFSIZE];
		struct service_command *cmd_table;

		buf[0] = '\0';
		cmd_table = service_p->service->command;

		SCMD_WALK(i, service_p)
		{
			if((cmd_table[i].operonly && !is_oper(client_p)) ||
			   (cmd_table[i].operflags && 
			    (!client_p->user->oper || 
			     (client_p->user->oper->flags & cmd_table[i].operflags) == 0)))
				continue;

			strlcat(buf, cmd_table[i].cmd, sizeof(buf));
			strlcat(buf, " ", sizeof(buf));
		}
		SCMD_END;

		if(buf[0] != '\0')
		{
			service_error(service_p, client_p,
				"%s Help Index. Use HELP <command> for more information",
				service_p->name);
			service_error(service_p, client_p, "Topics: %s", buf);
		}
		else
			service_error(service_p, client_p, "No help is available for this service.");

		service_p->service->help_count++;
		return;
	}

	service_p->service->flood++;
	fileptr = service_p->service->help;

	if(fileptr)
	{
		/* dump them the index file */
		/* this contains a short introduction and a list of commands */

		DLINK_FOREACH(ptr, fileptr->contents.head)
		{
			lineptr = ptr->data;
			service_error(service_p, client_p, "%s",
					lineptr->data);
		}
	}

	fileptr = service_p->service->helpadmin;

	if(client_p->user->oper && fileptr)
	{
		service_error(service_p, client_p, "Administrator commands:");

		DLINK_FOREACH(ptr, fileptr->contents.head)
		{
			lineptr = ptr->data;
			service_error(service_p, client_p, "%s",
					lineptr->data);
		}
	}
}

static void
handle_service_help(struct client *service_p, struct client *client_p, const char *arg)
{
	struct service_command *cmd_entry;

	if((cmd_entry = bsearch(arg, service_p->service->command,
				service_p->service->command_size / sizeof(struct service_command),
				sizeof(struct service_command), (bqcmp) scmd_compare)))
	{
		struct cachefile *fileptr;
		struct cacheline *lineptr;
		dlink_node *ptr;

		if(cmd_entry->helpfile == NULL ||
		   (cmd_entry->operonly && !is_oper(client_p)))
		{
			service_error(service_p, client_p,
					"No help available on %s", arg);
			return;
		}

		fileptr = cmd_entry->helpfile;

		DLINK_FOREACH(ptr, fileptr->contents.head)
		{
			lineptr = ptr->data;
			service_error(service_p, client_p, "%s", lineptr->data);
		}

		service_p->service->flood += cmd_entry->help_penalty;
		service_p->service->ehelp_count++;
	}
	else
		service_error(service_p, client_p, "Unknown topic '%s'", arg);
}

void
handle_service_msg(struct client *service_p, struct client *client_p, char *text)
{
        char *parv[MAXPARA+1];
        char *p;
        int parc = 0;

        if(!IsUser(client_p))
                return;

        if((p = strchr(text, ' ')) != NULL)
	{
                *p++ = '\0';
	        parc = string_to_array(p, parv);
	}

	handle_service(service_p, client_p, text, parc, (const char **) parv, 1);
}

void
handle_service(struct client *service_p, struct client *client_p, 
		const char *command, int parc, const char *parv[], int msg)
{
	struct service_command *cmd_entry;
        int retval;

        /* this service doesnt handle commands via privmsg */
        if(service_p->service->command == NULL)
                return;

	/* do flood limiting */
	if(!client_p->user->oper)
	{
		if((client_p->user->flood_time + config_file.client_flood_time) < CURRENT_TIME)
		{
			client_p->user->flood_time = CURRENT_TIME;
			client_p->user->flood_count = 0;
		}

		if(service_p->service->flood > service_p->service->flood_max_ignore ||
		   client_p->user->flood_count > config_file.client_flood_max_ignore)
		{
			client_p->user->flood_count++;
			service_p->service->ignored_count++;
			return;
		}

		if(service_p->service->flood > service_p->service->flood_max ||
		   client_p->user->flood_count > config_file.client_flood_max)
		{
			service_error(service_p, client_p, 
					"Temporarily unable to answer query. Please try again shortly.");
			client_p->user->flood_count++;
			service_p->service->flood++;
			service_p->service->paced_count++;
			return;
		}
	}

	if(msg && ServiceShortcut(service_p))
	{
		if(ServiceStealth(service_p) && !client_p->user->oper && !is_oper(client_p))
			return;

		client_p->user->flood_count += 1;
                service_p->service->flood += 1;

		service_error(service_p, client_p,
				"Commands to this service must be issued via /%s instead of by name.",
				service_p->name);
		return;
	}

        if(!strcasecmp(command, "HELP"))
        {
		if(ServiceStealth(service_p) && !client_p->user->oper && !is_oper(client_p))
			return;

#ifdef ENABLE_USERSERV
		if(ServiceLoginHelp(service_p) && !client_p->user->user_reg &&
		   !client_p->user->oper && !is_oper(client_p))
		{
			service_error(service_p, client_p, 
					"You must be logged in for %s::HELP",
					service_p->name);
			return;
		}
#endif

		client_p->user->flood_count += 2;
                service_p->service->flood += 2;

                if(parc < 1 || EmptyString(parv[0]))
			handle_service_help_index(service_p, client_p);
		else
			handle_service_help(service_p, client_p, parv[0]);

		return;
        }
	else if(!strcasecmp(command, "OPERLOGIN") || !strcasecmp(command, "OLOGIN"))
	{
		struct conf_oper *oper_p;
		const char *crpass;

		if(client_p->user->oper)
		{
			sendto_server(":%s NOTICE %s :You are already logged in as an oper",
					MYNAME, UID(client_p));
			return;
		}

		if(parc < 2)
		{
			sendto_server(":%s NOTICE %s :Insufficient parameters to %s::OLOGIN",
					MYNAME, UID(client_p), service_p->name);
			client_p->user->flood_count++;
			return;
		}

		if((oper_p = find_conf_oper(client_p->user->username, client_p->user->host,
						client_p->user->servername)) == NULL)
		{
			sendto_server(":%s NOTICE %s :No access to %s::OLOGIN",
					MYNAME, UID(client_p), ucase(service_p->name));
			client_p->user->flood_count++;
			return;
		}

		if(ConfOperEncrypted(oper_p))
			crpass = crypt(parv[1], oper_p->pass);
		else
			crpass = parv[1];

		if(strcmp(crpass, oper_p->pass))
		{
			sendto_server(":%s NOTICE %s :Invalid password",
					MYNAME, UID(client_p));
			return;
		}

		sendto_all(UMODE_AUTH, "%s:%s has logged in [IRC]",
				oper_p->name, client_p->user->mask);
		sendto_server(":%s NOTICE %s :Oper login successful",
				MYNAME, UID(client_p));

		client_p->user->oper = oper_p;
		oper_p->refcount++;
		dlink_add_alloc(client_p, &oper_list);

		return;
	}
	else if(!strcasecmp(command, "OPERLOGOUT") || !strcasecmp(command, "OLOGOUT"))
	{
		if(client_p->user->oper == NULL)
		{
			sendto_server(":%s NOTICE %s :You are not logged in as an oper",
					MYNAME, UID(client_p));
			client_p->user->flood_count++;
			return;
		}

		sendto_all(UMODE_AUTH, "%s:%s has logged out [IRC]",
				client_p->user->oper->name, client_p->user->mask);

		deallocate_conf_oper(client_p->user->oper);
		client_p->user->oper = NULL;
		dlink_find_destroy(client_p, &oper_list);

		sendto_server(":%s NOTICE %s :Oper logout successful",
				MYNAME, UID(client_p));
		return;
	}

	if(ServiceStealth(service_p) && !client_p->user->oper && !is_oper(client_p))
		return;

	if((cmd_entry = bsearch(command, service_p->service->command, 
				service_p->service->command_size / sizeof(struct service_command),
				sizeof(struct service_command), (bqcmp) scmd_compare)))
	{
		if((cmd_entry->operonly && !is_oper(client_p)) ||
		   (cmd_entry->operflags && 
		    (!client_p->user->oper || 
		     (client_p->user->oper->sflags & cmd_entry->operflags) == 0)))
		{
			service_error(service_p, client_p, "No access to %s::%s",
					service_p->name, cmd_entry->cmd);
			client_p->user->flood_count++;
			service_p->service->flood++;
			return;
		}

#ifdef ENABLE_USERSERV
		if(cmd_entry->userreg)
		{
			if(client_p->user->user_reg == NULL)
			{
				service_error(service_p, client_p, 
						"You must be logged in for %s::%s",
						service_p->name, cmd_entry->cmd);
				client_p->user->flood_count++;
				service_p->service->flood++;
				return;
			}
			else
			{
				client_p->user->user_reg->last_time = CURRENT_TIME;
				client_p->user->user_reg->flags |= US_FLAGS_NEEDUPDATE;
			}
		}
#endif

		if(parc < cmd_entry->minparc)
		{
			service_error(service_p, client_p, "Insufficient parameters to %s::%s",
					service_p->name, cmd_entry->cmd);
			client_p->user->flood_count++;
			service_p->service->flood++;
			return;
		}

		if(cmd_entry->operflags)
			sendto_all(UMODE_SPY, "#%s:%s# %s %s",
					client_p->user->oper->name, client_p->name,
					cmd_entry->cmd, rebuild_params((const char **) parv, parc, 0));
		else if(cmd_entry->spyflags)
			sendto_all(cmd_entry->spyflags, "#%s:%s!%s@%s# %s %s",
					client_p->user->user_reg ? 
					client_p->user->user_reg->name : "",
					client_p->name, client_p->user->username,
					client_p->user->host, cmd_entry->cmd,
					rebuild_params((const char **) parv, parc, 0));

		retval = (cmd_entry->func)(client_p, NULL, (const char **) parv, parc);

		client_p->user->flood_count += retval;
		service_p->service->flood += retval;
		cmd_entry->cmd_use++;
		return;
        }

        service_error(service_p, client_p, "Unknown command.");
        service_p->service->flood++;
	client_p->user->flood_count++;
}

void
service_send(struct client *service_p, struct client *client_p,
		struct lconn *conn_p, const char *format, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(client_p)
		sendto_server(":%s NOTICE %s :%s",
				ServiceMsgSelf(service_p) ? service_p->name : MYNAME, 
				UID(client_p), buf);
	else
		sendto_one(conn_p, "%s", buf);
}

void
service_error(struct client *service_p, struct client *client_p,
		const char *format, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	sendto_server(":%s NOTICE %s :%s",
			ServiceMsgSelf(service_p) ? service_p->name : MYNAME, 
			UID(client_p), buf);
}

void
service_stats(struct client *service_p, struct lconn *conn_p)
{
        struct service_command *cmd_table;
        char buf[BUFSIZE];
        char buf2[40];
        int i;
        int j = 0;

        sendto_one(conn_p, "%s Service:", service_p->service->id);

	if(ServiceDisabled(service_p))
	{
		sendto_one(conn_p, " Disabled");
		return;
	}

        sendto_one(conn_p, " Online as %s!%s@%s [%s]",
                   service_p->name, service_p->service->username,
                   service_p->service->host, service_p->info);

        if(service_p->service->command == NULL)
                return;

        sendto_one(conn_p, " Current load: %d/%d [%d] Paced: %lu [%lu]",
                   service_p->service->flood, service_p->service->flood_max,
                   service_p->service->flood_max_ignore,
                   service_p->service->paced_count,
                   service_p->service->ignored_count);

        sendto_one(conn_p, " Help usage: %lu Extended: %lu",
                   service_p->service->help_count,
                   service_p->service->ehelp_count);

        cmd_table = service_p->service->command;

        sprintf(buf, " Command usage: ");

	SCMD_WALK(i, service_p)
        {
                snprintf(buf2, sizeof(buf2), "%s:%lu ",
                         cmd_table[i].cmd, cmd_table[i].cmd_use);
                strlcat(buf, buf2, sizeof(buf));

                j++;

                if(j > 6)
                {
                        sendto_one(conn_p, "%s", buf);
                        sprintf(buf, "                ");
                        j = 0;
                }
        }
	SCMD_END;

        if(j)
                sendto_one(conn_p, "%s", buf);
}
