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
	client_p->service->func = service->func;
        client_p->service->stats = service->stats;

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

		client_p->service->floodcount -= 5;

		if(client_p->service->floodcount < 0)
			client_p->service->floodcount = 0;
	}
}

