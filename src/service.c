/* src/service.c
 *  Contains code for handling interaction with our services.
 *  
 *  Copyright (C) 2003 ircd-ratbox development team.
 *
 *  $Id$
 */
#include "stdinc.h"
#include "service.h"
#include "client.h"
#include "scommand.h"
#include "rserv.h"
#include "conf.h"
#include "io.h"
#include "log.h"

dlink_list service_list;

struct client *
add_service(struct service_handler *service)
{
	struct client *client_p;

	if(strchr(client_p->name, '.') != NULL)
	{
		slog("ERR: Invalid service name %s", client_p->name);
		return NULL;
	}

	if((client_p = find_client(service->name)) != NULL)
	{
		if(IsService(client_p))
		{
			slog("ERR: Tried to add duplicate service %s", service->name);
			return NULL;
		}
		else if(IsServer(client_p))
		{
			slog("ERR: A server exists with service name %s?!", service->name);
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
        client_p->service->error = service->error;
        client_p->service->stats = service->stats;

        client_p->service->flood_max = service->flood_max;
        client_p->service->flood_max_ignore = service->flood_max_ignore;

	dlink_add(client_p, &client_p->listnode, &service_list);
	add_client(client_p);

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
		      target_p->name, target_p->service->opered ? "o" : "",
		      target_p->service->username,
		      target_p->service->host, MYNAME, target_p->info);
}

void
introduce_services(void)
{
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		introduce_service(ptr->data);
	}
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
        char *p;
        int retval;
        int i;

        if(!IsUser(client_p))
                return;

        if(service_p->service->flood > service_p->service->flood_max_ignore)
                return;

        if(service_p->service->flood > service_p->service->flood_max)
        {
                sendto_server(":%s NOTICE %s :Temporarily unable to answer "
                              "query. Please try again shortly.",
                              MYNAME, client_p->name);
                service_p->service->flood++;
                return;
        }

        if((p = strchr(text, ' ')) != NULL)
                *p++ = '\0';

        cmd_table = service_p->service->command;

        if(!strcasecmp(text, "HELP"))
        {
                service_p->service->flood++;

                if(EmptyString(p))
                {
                        char buf[BUFSIZE];

                        buf[0] = '\0';

                        sendto_server(":%s NOTICE %s :%s Help Index.  Use "
                                      "HELP <command> for more information.",
                                      MYNAME, client_p->name,
                                      service_p->name);

                        for(i = 0; cmd_table[i].cmd[0] != '\0'; i++)
                        {
                                strlcat(buf, cmd_table[i].cmd, sizeof(buf));
                                strlcat(buf, " ", sizeof(buf));
                        }

                        sendto_server(":%s NOTICE %s :Topics: %s",
                                      MYNAME, client_p->name, buf);
                        return;
                }

                for(i = 0; cmd_table[i].cmd[0] != '\0'; i++)
                {
                        if(!strcasecmp(text, cmd_table[i].cmd))
                        {
                                int x = 0;

                                while(cmd_table[i].help[x] != NULL)
                                {
                                        sendto_server(":%s NOTICE %s :%s",
                                                      MYNAME, client_p->name,
                                                      cmd_table[i].help[x]);
                                }

                                service_p->service->flood += cmd_table[i].help_penalty;
                                cmd_table[i].help_use++;

                                return;
                        }
                }

                sendto_server(":%s NOTICE %s :Unknown topic '%s'",
                              MYNAME, client_p->name, text);
                return;
        }

        for(i = 0; cmd_table[i].cmd[0] != '\0'; i++)
        {
                if(!strcasecmp(text, cmd_table[i].cmd))
                {
                        retval = (cmd_table[i].func)(client_p, p);

                        service_p->service->flood += retval;
                        cmd_table[i].cmd_use++;

                        return;
                }
        }

        sendto_server(":%s NOTICE %s :Unknown command.",
                      MYNAME, client_p->name);
}

void
service_error(struct client *service_p, struct client *client_p, int error)
{
        struct service_error *error_table;
        int i;

        error_table = service_p->service->error;

        for(i = 0; error_table[i].error; i++)
        {
                if(error_table[i].error == error)
                {
                        sendto_server(":%s NOTICE %s :%s",
                                      MYNAME, client_p->name,
                                      error_table[i].text);
                        return;
                }
        }
}
