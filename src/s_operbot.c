/* src/s_operbot.c
 *   Contains the code for the host statistics service.
 *
 * Copyright (C) 2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "io.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "ucommand.h"

#define OPERBOT_ERR_PARAM	1
#define OPERBOT_ERR_ACCESS	2
#define OPERBOT_ERR_CHANNEL	3

static struct client *operbot_p;

dlink_list operbot_channels;

static int s_operbot_invite(struct client *, char *parv[], int parc);
static int s_operbot_op(struct client *, char *parv[], int parc);

static void s_operbot_objoin(struct connection_entry *conn_p, char *parv[], int parc);
static void s_operbot_obpart(struct connection_entry *conn_p, char *parv[], int parc);

static struct service_command operbot_command[] =
{
	{ "INVITE",	&s_operbot_invite,	NULL, 0, 1, 0L },
	{ "OP",		&s_operbot_op,		NULL, 0, 1, 0L },
	{ "\0",		NULL,			NULL, 0, 0, 0L }
};

static struct ucommand_handler operbot_ucommand[] =
{
	{ ".objoin",	&s_operbot_objoin,	0, NULL },
	{ ".obpart",	&s_operbot_obpart,	0, NULL },
	{ "\0",		NULL,			0, NULL }
};

static struct service_error operbot_message[] =
{
	{ OPERBOT_ERR_PARAM,	"Missing parameters."		},
	{ OPERBOT_ERR_ACCESS,	"No access."			},
	{ OPERBOT_ERR_CHANNEL,	"Invalid channel."		},
	{ 0,			"\0"				}
};

static struct service_handler operbot_service = {
	"OPERBOT", "operbot", "operbot", "services.operbot",
	"Oper invitation/op services", 1, 60, 80, 
	operbot_command, operbot_message, operbot_ucommand, NULL
};

void
init_s_operbot(void)
{
	operbot_p = add_service(&operbot_service);
	join_service(operbot_p, "#ratbox");
}

static int
s_operbot_invite(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;

	if(parc < 1 || EmptyString(parv[0]))
	{
		service_error(operbot_p, client_p, OPERBOT_ERR_PARAM);
		return 1;
	}

	if(!is_oper(client_p))
	{
		service_error(operbot_p, client_p, OPERBOT_ERR_ACCESS);
		return 1;
	}

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_error(operbot_p, client_p, OPERBOT_ERR_CHANNEL);
		return 1;
	}

	if(dlink_find(&operbot_p->service->channels, chptr) == NULL)
	{
		service_error(operbot_p, client_p, OPERBOT_ERR_CHANNEL);
		return 1;
	}

	if(find_chmember(chptr, client_p) != NULL)
		return 1;

	sendto_server(":%s INVITE %s %s", 
			operbot_p->name, client_p->name, chptr->name);
	return 1;
}

static int
s_operbot_op(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;

	if(parc < 1 || EmptyString(parv[0]))
	{
		service_error(operbot_p, client_p, OPERBOT_ERR_PARAM);
		return 1;
	}

	if(!is_oper(client_p))
	{
		service_error(operbot_p, client_p, OPERBOT_ERR_ACCESS);
		return 1;
	}

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_error(operbot_p, client_p, OPERBOT_ERR_CHANNEL);
		return 1;
	}

	if(dlink_find(&operbot_p->service->channels, chptr) == NULL)
	{
		service_error(operbot_p, client_p, OPERBOT_ERR_CHANNEL);
		return 1;
	}

	if(find_chmember(chptr, client_p) == NULL)
		return 1;

	sendto_server(":%s MODE %s +o %s",
			operbot_p->name, chptr->name, client_p->name);
	return 1;

	return 1;
}

static void
s_operbot_objoin(struct connection_entry *conn_p, char *parv[], int parc)
{
}

static void
s_operbot_obpart(struct connection_entry *conn_p, char *parv[], int parc)
{
}
