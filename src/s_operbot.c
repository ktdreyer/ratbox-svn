/* src/s_operbot.c
 *   Contains the code for the operbot service.
 *
 * Copyright (C) 2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef ENABLE_OPERBOT
#include "service.h"
#include "client.h"
#include "channel.h"
#include "io.h"
#include "rserv.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "ucommand.h"
#include "newconf.h"

static struct client *operbot_p;

static void u_operbot_objoin(struct client *, struct lconn *, const char **, int);
static void u_operbot_obpart(struct client *, struct lconn *, const char **, int);

static int s_operbot_objoin(struct client *, const char **, int);
static int s_operbot_obpart(struct client *, const char **, int);
static int s_operbot_invite(struct client *, const char **, int);
static int s_operbot_op(struct client *, const char **, int);

static struct service_command operbot_command[] =
{
	{ "OBJOIN",	&s_operbot_objoin,	1, NULL, 1, 0L, 0, 0, CONF_OPER_OPERBOT, 0 },
	{ "OBPART",	&s_operbot_obpart,	1, NULL, 1, 0L, 0, 0, CONF_OPER_OPERBOT, 0 },
	{ "INVITE",	&s_operbot_invite,	1, NULL, 1, 0L, 0, 1, 0, 0 },
	{ "OP",		&s_operbot_op,		0, NULL, 1, 0L, 0, 1, 0, 0 }
};

static struct ucommand_handler operbot_ucommand[] =
{
	{ "objoin",	u_operbot_objoin,	CONF_OPER_OPERBOT, 1, 1, NULL },
	{ "obpart",	u_operbot_obpart,	CONF_OPER_OPERBOT, 1, 1, NULL },
	{ "\0",		NULL,			0, 0, 0, NULL }
};

static struct service_handler operbot_service = {
	"OPERBOT", "operbot", "operbot", "services.int",
	"Oper invitation/op services", 60, 80, 
	operbot_command, sizeof(operbot_command), operbot_ucommand, NULL
};

static int operbot_db_callback(void *db, int, char **, char **);

void
init_s_operbot(void)
{
	operbot_p = add_service(&operbot_service);

	loc_sqlite_exec(operbot_db_callback, "SELECT * FROM operbot");
}

static int
operbot_db_callback(void *db, int argc, char **argv, char **colnames)
{
	join_service(operbot_p, argv[0], NULL);
	return 0;
}

static void
u_operbot_objoin(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) && 
	   dlink_find(operbot_p, &chptr->services))
	{
		sendto_one(conn_p, "%s already in %s", operbot_p->name, parv[0]);
		return;
	}

	slog(operbot_p, 1, "%s - OBJOIN %s", conn_p->name, parv[0]);

	loc_sqlite_exec(NULL, "INSERT INTO operbot VALUES(%Q, %Q)",
			parv[0], conn_p->name);

	join_service(operbot_p, parv[0], NULL);
	sendto_one(conn_p, "%s joined to %s", operbot_p->name, parv[0]);
}

static void
u_operbot_obpart(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	if(part_service(operbot_p, parv[0]))
	{
		slog(operbot_p, 1, "%s - OBPART %s", conn_p->name, parv[0]);

		loc_sqlite_exec(NULL, "DELETE FROM operbot WHERE chname = %Q",
				parv[0]);
		sendto_one(conn_p, "%s removed from %s", operbot_p->name, parv[0]);
	}
	else
		sendto_one(conn_p, "%s not in channel %s", operbot_p->name, parv[0]);
}

static int
s_operbot_objoin(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) && 
	   dlink_find(operbot_p, &chptr->services))
	{
		service_error(operbot_p, client_p, 
				"%s already in %s", operbot_p->name, parv[0]);
		return 1;
	}

	slog(operbot_p, 1, "%s - OBJOIN %s",
		client_p->user->oper->name, parv[0]);

	loc_sqlite_exec(NULL, "INSERT INTO operbot VALUES(%Q, %Q)",
			parv[0], client_p->user->oper->name);
			
	join_service(operbot_p, parv[0], NULL);
	service_error(operbot_p, client_p, 
			"%s joined to %s", operbot_p->name, parv[0]);
	return 1;
}

static int
s_operbot_obpart(struct client *client_p, const char *parv[], int parc)
{
	if(part_service(operbot_p, parv[0]))
	{
		slog(operbot_p, 1, "%s - OBPART %s",
			client_p->user->oper->name, parv[0]);

		loc_sqlite_exec(NULL, "DELETE FROM operbot WHERE chname = %Q",
				parv[0]);
		service_error(operbot_p, client_p, 
				"%s removed from %s", operbot_p->name, parv[0]);
	}
	else
		service_error(operbot_p, client_p, 
				"%s not in channel %s", operbot_p->name, parv[0]);
	return 1;
}


static int
s_operbot_invite(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_error(operbot_p, client_p, "Invalid channel");
		return 1;
	}

	if(dlink_find(chptr, &operbot_p->service->channels) == NULL)
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
s_operbot_op(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct chmember *mptr;

	/* op in all common channels */
	if(!parc)
	{
		dlink_node *ptr;

		DLINK_FOREACH(ptr, operbot_p->service->channels.head)
		{
			chptr = ptr->data;

			if((mptr = find_chmember(chptr, client_p)) == NULL)
				continue;

			if(is_opped(mptr))
				continue;

			mptr->flags |= MODE_OPPED;
			sendto_server(":%s MODE %s +o %s",
					operbot_p->name, chptr->name, client_p->name);
		}

		return 1;
	}

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_error(operbot_p, client_p, "Invalid channel");
		return 1;
	}

	if(dlink_find(chptr, &operbot_p->service->channels) == NULL)
	{
		service_error(operbot_p, client_p, "Invalid channel");
		return 1;
	}

	if((mptr = find_chmember(chptr, client_p)) == NULL)
		return 1;

	if(is_opped(mptr))
		return 1;

	mptr->flags |= MODE_OPPED;
	sendto_server(":%s MODE %s +o %s",
			operbot_p->name, chptr->name, client_p->name);
	return 1;
}

#endif
