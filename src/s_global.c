/* src/s_global.c
 *   Contains the code for the netwide messaging service.
 *
 * Copyright (C) 2004-2005 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004-2005 ircd-ratbox development team
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

#ifdef ENABLE_GLOBAL
#include "rsdb.h"
#include "service.h"
#include "client.h"
#include "io.h"
#include "rserv.h"
#include "c_init.h"
#include "conf.h"
#include "ucommand.h"
#include "log.h"
#include "hook.h"

/* maximum length of a welcome message */
#define WELCOME_MAGIC	400

static struct client *global_p;

static void init_s_global(void);

static dlink_list global_welcome_list;

static int o_global_netmsg(struct client *, struct lconn *, const char **, int);
static int o_global_addwelcome(struct client *, struct lconn *, const char **, int);
static int o_global_delwelcome(struct client *, struct lconn *, const char **, int);

static int h_global_send_welcome(void *target_p, void *unused);

static struct service_command global_command[] =
{
	{ "NETMSG",	&o_global_netmsg,	1, NULL, 1, 0L, 0, 0, CONF_OPER_GLOB_NETMSG, 0 },
	{ "ADDWELCOME",	&o_global_addwelcome,	2, NULL, 1, 0L, 0, 0, CONF_OPER_GLOB_WELCOME, 0 },
	{ "DELWELCOME", &o_global_delwelcome,	1, NULL, 1, 0L, 0, 0, CONF_OPER_GLOB_WELCOME, 0 }
};

static struct ucommand_handler global_ucommand[] =
{
	{ "netmsg",	o_global_netmsg,	0, CONF_OPER_GLOB_NETMSG, 1, 1, NULL },
	{ "addwelcome",	o_global_addwelcome,	0, CONF_OPER_GLOB_WELCOME, 2, 1, NULL },
	{ "delwelcome",	o_global_delwelcome,	0, CONF_OPER_GLOB_WELCOME, 1, 1, NULL },
	{ "\0", NULL, 0, 0, 0, 0, NULL }
};

static struct service_handler global_service = {
	"GLOBAL", "GLOBAL", "global", "services.int",
	"Network Message Service", 60, 80, 
	global_command, sizeof(global_command), global_ucommand, init_s_global, NULL
};

struct global_welcome_msg
{
	unsigned int id;
	unsigned int priority;
	char *text;
	dlink_node ptr;
};

void
preinit_s_global(void)
{
	global_p = add_service(&global_service);

	/* global service has to be opered otherwise it
	 * wont work. --anfl
	 */
	global_p->service->flags |= SERVICE_OPERED;
}

static void
init_s_global(void)
{
	struct global_welcome_msg *welcome;
	const char **coldata;
	const char **colnames;
	int ncol;

	rsdb_step_init("SELECT id, priority, text FROM global_welcome WHERE 1 ORDER BY priority ASC");

	while(rsdb_step(&ncol, &coldata, &colnames))
	{
		if(ncol < 3)
			continue;

		welcome = my_malloc(sizeof(struct global_welcome_msg));
		welcome->id = (unsigned int) atoi(coldata[0]);
		welcome->priority = (unsigned int) atoi(coldata[1]);
		welcome->text = my_strdup(coldata[2]);

		dlink_add_tail(welcome, &welcome->ptr, &global_welcome_list);
	}

	hook_add(h_global_send_welcome, HOOK_NEW_CLIENT);
}

static int
o_global_netmsg(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct client *target_p;
	dlink_node *ptr;
	const char *data;

	data = rebuild_params(parv, parc, 0);

	DLINK_FOREACH(ptr, server_list.head)
	{
		target_p = ptr->data;

		sendto_server(":%s NOTICE $$%s :[NETWORK MESSAGE] %s",
				global_p->name, target_p->name, data);
	}

	slog(global_p, 1, "%s - NETMSG %s", 
		OPER_NAME(client_p, conn_p), data);

	return 0;
}

static struct global_welcome_msg *
find_welcome(unsigned int id)
{
	struct global_welcome_msg *welcome;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, global_welcome_list.head)
	{
		welcome = ptr->data;

		if(welcome->id == id)
			return welcome;
	}

	return NULL;
}

static int
h_global_send_welcome(void *target_p, void *unused)
{
	struct global_welcome_msg *welcome;
	dlink_node *ptr;

	if(!dlink_list_length(&global_welcome_list))
		return 0;

	DLINK_FOREACH(ptr, global_welcome_list.head)
	{
		welcome = ptr->data;

		service_error(global_p, target_p, "%s", welcome->text);
	}

	return 0;
}

static int
o_global_addwelcome(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static int i=0;		/* XXX */
	struct global_welcome_msg *welcome;
	const char *data;
	unsigned int priority;
	dlink_node *listpos;

	priority = atoi(parv[0]);
	data = rebuild_params(parv, parc, 1);

	if(strlen(data) > WELCOME_MAGIC)
	{
		service_send(global_p, client_p, conn_p,
				"Welcome message too long (%u > %u)",
				strlen(data), WELCOME_MAGIC);
		return 0;
	}

	/* find where in the list we want to add this.. */
	DLINK_FOREACH(listpos, global_welcome_list.head)
	{
		welcome = listpos->data;

		if(welcome->priority > priority)
			break;
	}

	welcome = my_malloc(sizeof(struct global_welcome_msg));
	welcome->id = i++; /* XXX */
	welcome->priority = priority;
	welcome->text = my_strdup(data);

	dlink_add_before(welcome, &welcome->ptr, listpos, &global_welcome_list);

	service_send(global_p, client_p, conn_p,
			"Welcome message %u added with priority %u",
			welcome->id, welcome->priority);
	slog(global_p, 1, "%s - ADDWELCOME %u %u %s",
		OPER_NAME(client_p, conn_p), welcome->id, welcome->priority, welcome->text);

	return 0;
}

static int
o_global_delwelcome(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct global_welcome_msg *welcome;
	unsigned int id;

	id = atoi(parv[0]);

	if((welcome = find_welcome(id)) == NULL)
	{
		service_send(global_p, client_p, conn_p,
				"Welcome message %u not found", id);
		return 0;
	}

	rsdb_exec(NULL, "DELETE FROM global_welcome WHERE id='%u'", id);

	dlink_delete(&welcome->ptr, &global_welcome_list);
	my_free(welcome->text);
	my_free(welcome);

	service_send(global_p, client_p, conn_p,
			"Welcome message %u deleted", id);
	slog(global_p, 1, "%s - DELWELCOME %u",
		OPER_NAME(client_p, conn_p), id);

	return 0;
}

#endif
