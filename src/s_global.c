/* src/s_global.c
 *   Contains the code for the netwide messaging service.
 *
 * Copyright (C) 2004 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef ENABLE_GLOBAL
#include "service.h"
#include "client.h"
#include "io.h"
#include "rserv.h"
#include "c_init.h"
#include "conf.h"
#include "ucommand.h"

static struct client *global_p;

static void u_global_netmsg(struct connection_entry *, char *parv[], int parc);
static int s_global_netmsg(struct client *, char *parv[], int parc);

static struct service_command global_command[] =
{
	{ "NETMSG",	&s_global_netmsg, 1, NULL, 1, 0L, 0, 0, CONF_OPER_GLOBAL, 0 },
	{ "\0",		NULL,		  0, NULL, 0, 0L, 0, 0, 0, 0 }
};

static struct ucommand_handler global_ucommand[] =
{
	{ "netmsg", u_global_netmsg, CONF_OPER_GLOBAL, 2, 1, NULL },
	{ "\0", NULL, 0, 0, 0, NULL }
};

static struct service_handler global_service = {
	"GLOBAL", "GLOBAL", "global", "services.int",
	"Network Message Service", 60, 80, 
	global_command, global_ucommand, NULL
};

void
init_s_global(void)
{
	global_p = add_service(&global_service);

	/* global service has to be opered otherwise it
	 * wont work. --anfl
	 */
	global_p->service->flags |= SERVICE_OPERED;
}

static void
u_global_netmsg(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct client *target_p;
	dlink_node *ptr;
	const char *data;

	data = rebuild_params((const char **) parv, parc, 1);

	DLINK_FOREACH(ptr, server_list.head)
	{
		target_p = ptr->data;

		sendto_server(":%s NOTICE $$%s :[NETWORK MESSAGE] %s",
				global_p->name, target_p->name, data);
	}
}

static int
s_global_netmsg(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;
	dlink_node *ptr;
	const char *data;

	data = rebuild_params((const char **) parv, parc, 0);

	DLINK_FOREACH(ptr, server_list.head)
	{
		target_p = ptr->data;

		sendto_server(":%s NOTICE $$%s :[NETWORK MESSAGE] %s",
				global_p->name, target_p->name, data);
	}

	return 0;
}

#endif