/* src/service.c
 *   Contains code for handling interaction with our services.
 *  
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
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

struct client *
add_service(struct service_handler *service)
{
	struct client *client_p;

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
				die("services conflict");

			/* we're about to collide it. */
			exit_client(client_p);
		}
	}

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
        client_p->service->ucommand = service->ucommand;
        client_p->service->stats = service->stats;

        client_p->service->flood_max = service->flood_max;
        client_p->service->flood_max_ignore = service->flood_max_ignore;

	dlink_add(client_p, &client_p->listnode, &service_list);

	open_service_logfile(client_p);

        /* try and cache any help stuff */
        if(service->command != NULL)
        {
                struct service_command *scommand = service->command;
                char filename[PATH_MAX];
                int i;

		snprintf(filename, sizeof(filename), "%s%s/index",
				HELP_PATH, lcase(service->id));

		client_p->service->help = cache_file(filename, "index");

		snprintf(filename, sizeof(filename), "%s%s/index-admin",
				HELP_PATH, lcase(service->id));

		client_p->service->helpadmin = cache_file(filename, "index-admin");

                for(i = 0; scommand[i].cmd[0] != '\0'; i++)
                {
                        snprintf(filename, sizeof(filename), "%s%s/",
                                 HELP_PATH, lcase(service->id));

                        /* we cant lcase() twice in one function call */
                        strlcat(filename, lcase(scommand[i].cmd),
                                sizeof(filename));

                        scommand[i].helpfile = cache_file(filename, scommand[i].cmd);
                }
        }

        if(service->ucommand != NULL)
                add_ucommands(client_p, service->ucommand, service->id);

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
	sendto_server("NICK %s 1 1 +i%s %s %s %s :%s",
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

void
handle_service(struct client *service_p, struct client *client_p, char *text)
{
        struct service_command *cmd_table;
        char *parv[MAXPARA+1];
        char *p;
        int parc;
        int retval;
        int i;

        if(!IsUser(client_p))
                return;

        cmd_table = service_p->service->command;

        /* this service doesnt handle commands via privmsg */
        if(cmd_table == NULL)
                return;

        if(service_p->service->flood > service_p->service->flood_max_ignore)
        {
                service_p->service->ignored_count++;
                return;
        }

        if(service_p->service->flood > service_p->service->flood_max)
        {
		service_error(service_p, client_p, 
			"Temporarily unable to answer query. Please try again shortly.");
                service_p->service->flood++;
                service_p->service->paced_count++;
                return;
        }

        if((p = strchr(text, ' ')) != NULL)
                *p++ = '\0';

        parc = string_to_array(p, parv);

        if(!strcasecmp(text, "HELP"))
        {
		if(ServiceStealth(service_p) && !client_p->user->oper)
			return;

                service_p->service->flood++;

                if(parc < 1 || EmptyString(parv[0]))
                {
			struct cachefile *fileptr;
			struct cacheline *lineptr;
			dlink_node *ptr;

			/* if this service has short help enabled, or there
			 * is no index file and theyre either unopered (so
			 * cant see admin file), or theres no admin file.
			 */
			if(ServiceShortHelp(service_p) ||
			   (!service_p->service->help &&
			    (!client_p->user->oper || !service_p->service->helpadmin)))
			{	
	                        char buf[BUFSIZE];

	                        buf[0] = '\0';

        	                for(i = 0; cmd_table[i].cmd[0] != '\0'; i++)
                	        {

	                        	if((cmd_table[i].operonly && !is_oper(client_p)) ||
					   (cmd_table[i].operflags && 
					    (!client_p->user->oper || 
					     (client_p->user->oper->flags & cmd_table[i].operflags) == 0)))
                                	        continue;

	                                strlcat(buf, cmd_table[i].cmd, sizeof(buf));
        	                        strlcat(buf, " ", sizeof(buf));
                	        }

				if(buf[0] != '\0')
				{
					service_error(service_p, client_p, "%s Help Index. "
							"Use HELP <command> for more information",
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
				service_error(service_p, client_p, "Available commands:");

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

			return;
                }

                for(i = 0; cmd_table[i].cmd[0] != '\0'; i++)
                {
                        if(!strcasecmp(parv[0], cmd_table[i].cmd))
                        {
                                struct cachefile *fileptr;
                                struct cacheline *lineptr;
                                dlink_node *ptr;

                                if(cmd_table[i].helpfile == NULL ||
                                   (cmd_table[i].operonly && !is_oper(client_p)))
                                {
                                        service_error(service_p, client_p,
						"No help available on %s", p);
                                        return;
                                }

                                fileptr = cmd_table[i].helpfile;

                                DLINK_FOREACH(ptr, fileptr->contents.head)
                                {
                                        lineptr = ptr->data;
					service_error(service_p, client_p, "%s",
							lineptr->data);
                                }

                                service_p->service->flood += cmd_table[i].help_penalty;
                                service_p->service->ehelp_count++;

                                return;
                        }
                }

		service_error(service_p, client_p, "Unknown topic '%s'", p);
                return;
        }
	else if(!strcasecmp(text, "OPERLOGIN") || !strcasecmp(text, "OLOGIN"))
	{
		struct conf_oper *oper_p;
		char *crpass;

		if(client_p->user->oper)
		{
			sendto_server(":%s NOTICE %s :You are already logged in as an oper",
					MYNAME, client_p->name);
			return;
		}

		if(parc < 2)
		{
			sendto_server(":%s NOTICE %s :Insufficient parameters to %s::OLOGIN",
					MYNAME, client_p->name, service_p->name);
			service_p->service->flood++;
			return;
		}

		if((oper_p = find_conf_oper(client_p->user->username, client_p->user->host,
						client_p->user->servername)) == NULL)
		{
			sendto_server(":%s NOTICE %s :No access to %s::OLOGIN",
					MYNAME, client_p->name, ucase(service_p->name));
			service_p->service->flood++;
			return;
		}

		if(ConfOperEncrypted(oper_p))
			crpass = crypt(parv[1], oper_p->pass);
		else
			crpass = parv[1];

		if(strcmp(crpass, oper_p->pass))
		{
			sendto_server(":%s NOTICE %s :Invalid password",
					MYNAME, client_p->name);
			return;
		}

		sendto_all(UMODE_AUTH, "%s:%s has logged in [IRC]",
				oper_p->name, client_p->user->mask);
		sendto_server(":%s NOTICE %s :Oper login successful",
				MYNAME, client_p->name);

		client_p->user->oper = oper_p;
		oper_p->refcount++;
		dlink_add_alloc(client_p, &oper_list);

		return;
	}
	else if(!strcasecmp(text, "OPERLOGOUT") || !strcasecmp(text, "OLOGOUT"))
	{
		if(client_p->user->oper == NULL)
		{
			sendto_server(":%s NOTICE %s :You are not logged in as an oper",
					MYNAME, client_p->name);
			service_p->service->flood++;
			return;
		}

		sendto_all(UMODE_AUTH, "%s:%s has logged out [IRC]",
				client_p->user->oper->name, client_p->user->mask);

		deallocate_conf_oper(client_p->user->oper);
		client_p->user->oper = NULL;
		dlink_find_destroy(client_p, &oper_list);

		sendto_server(":%s NOTICE %s :Oper logout successful",
				MYNAME, client_p->name);
		return;
	}

	if(ServiceStealth(service_p) && !client_p->user->oper)
		return;

        for(i = 0; cmd_table[i].cmd[0] != '\0'; i++)
        {
                if(!strcasecmp(text, cmd_table[i].cmd))
                {
                        if((cmd_table[i].operonly && !is_oper(client_p)) ||
			   (cmd_table[i].operflags && 
			    (!client_p->user->oper || 
			     (client_p->user->oper->flags & cmd_table[i].operflags) == 0)))
                        {
				service_error(service_p, client_p, "No access to %s::%s",
						service_p->name, cmd_table[i].cmd);
                                service_p->service->flood++;
                                return;
                        }

#ifdef ENABLE_USERSERV
			if(cmd_table[i].userreg)
			{
				if(client_p->user->user_reg == NULL)
				{
					service_error(service_p, client_p, 
						"You must be logged in for %s::%s",
						service_p->name, cmd_table[i].cmd);
					return;
				}
				else
					client_p->user->user_reg->last_time = CURRENT_TIME;
			}
#endif

			if(parc < cmd_table[i].minparc)
			{
				service_error(service_p, client_p, "Insufficient parameters to %s::%s",
						service_p->name, cmd_table[i].cmd);
				service_p->service->flood++;
				return;
			}

			if(cmd_table[i].operflags)
				sendto_all(UMODE_SPY, "#%s:%s# %s %s",
					client_p->user->oper->name, client_p->name,
					cmd_table[i].cmd, rebuild_params((const char **) parv, parc, 0));
			else if(cmd_table[i].spyflags)
				sendto_all(cmd_table[i].spyflags, "#%s:%s!%s@%s# %s %s",
					client_p->user->user_reg ? 
					 client_p->user->user_reg->name : "",
					client_p->name, client_p->user->username,
					client_p->user->host, cmd_table[i].cmd,
					rebuild_params((const char **) parv, parc, 0));

                        retval = (cmd_table[i].func)(client_p, parv, parc);

                        service_p->service->flood += retval;
                        cmd_table[i].cmd_use++;

                        return;
                }
        }

        service_error(service_p, client_p, "Unknown command.");
        service_p->service->flood++;
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
			client_p->name, buf);
}

void
service_stats(struct client *service_p, struct connection_entry *conn_p)
{
        struct service_command *cmd_table;
        char buf[BUFSIZE];
        char buf2[20];
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

        for(i = 0; cmd_table[i].cmd[0] != '\0'; i++)
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

        if(j)
                sendto_one(conn_p, "%s", buf);
}
