/* src/s_operbot.c
 *   Contains the code for the operbot service.
 *
 * Copyright (C) 2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef OPERBOT_SERVICE
#include "service.h"
#include "client.h"
#include "channel.h"
#include "io.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "ucommand.h"
#include "newconf.h"

#define OPERBOT_ERR_CHANNEL	2

static struct client *operbot_p;

static int s_operbot_invite(struct client *, char *parv[], int parc);
static int s_operbot_op(struct client *, char *parv[], int parc);

static struct service_command operbot_command[] =
{
	{ "INVITE",	&s_operbot_invite,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "OP",		&s_operbot_op,		1, NULL, 1, 0L, 0, 1, 0 },
	{ "\0",		NULL,			0, NULL, 0, 0L, 0, 0, 0 }
};

static struct service_handler operbot_service = {
	"OPERBOT", "operbot", "operbot", "services.operbot",
	"Oper invitation/op services", 1, 60, 80, 
	operbot_command, NULL, NULL
};

void
init_s_operbot(void)
{
	operbot_p = add_service(&operbot_service);
}

static int
s_operbot_invite(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_error(operbot_p, client_p, "Invalid channel");
		return 1;
	}

	if(dlink_find(&operbot_p->service->channels, chptr) == NULL)
	{
		service_error(operbot_p, client_p, "Invalid channel");
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
	struct chmember *mptr;

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_error(operbot_p, client_p, "Invalid channel");
		return 1;
	}

	if(dlink_find(&operbot_p->service->channels, chptr) == NULL)
	{
		service_error(operbot_p, client_p, "Invalid channel");
		return 1;
	}

	if((mptr = find_chmember(chptr, client_p)) == NULL)
		return 1;

	mptr->flags |= MODE_OPPED;
	sendto_server(":%s MODE %s +o %s",
			operbot_p->name, chptr->name, client_p->name);
	return 1;
}

#endif
