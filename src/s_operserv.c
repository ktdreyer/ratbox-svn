/* src/s_operserv.c
 *   Contains the code for oper services.
 *
 * Copyright (C) 2004 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef ENABLE_OPERSERV
#include "client.h"
#include "service.h"
#include "channel.h"
#include "io.h"
#include "rserv.h"
#include "c_init.h"
#include "conf.h"
#include "ucommand.h"
#include "modebuild.h"
#include "log.h"

static struct client *operserv_p;

static void u_oper_takeover(struct connection_entry *, char **, int);
static void u_oper_osjoin(struct connection_entry *, char **, int);
static void u_oper_ospart(struct connection_entry *, char **, int);
static void u_oper_omode(struct connection_entry *, char **, int);

static int s_oper_takeover(struct client *, char *parv[], int parc);
static int s_oper_osjoin(struct client *, char *parv[], int parc);
static int s_oper_ospart(struct client *, char *parv[], int parc);
static int s_oper_omode(struct client *, char *parv[], int parc);

static struct service_command operserv_command[] =
{
	{ "OSJOIN",	&s_oper_osjoin,		1, NULL, 1, 0L, 0, 0, CONF_OPER_OPERSERV, 0 },
	{ "OSPART",	&s_oper_ospart,		1, NULL, 1, 0L, 0, 0, CONF_OPER_OPERSERV, 0 },
	{ "TAKEOVER",	&s_oper_takeover,	1, NULL, 1, 0L, 0, 0, CONF_OPER_OPERSERV, 0 },
	{ "OMODE",	&s_oper_omode,		2, NULL, 1, 0L, 0, 0, CONF_OPER_OPERSERV, 0 }
};

static struct ucommand_handler operserv_ucommand[] =
{
	{ "osjoin",	u_oper_osjoin,	CONF_OPER_OPERSERV, 2, 1, NULL },
	{ "ospart",	u_oper_ospart,	CONF_OPER_OPERSERV, 2, 1, NULL },
	{ "takeover",	u_oper_takeover,CONF_OPER_OPERSERV, 2, 1, NULL },
	{ "omode",	u_oper_omode,	CONF_OPER_OPERSERV, 3, 1, NULL },
	{ "\0", NULL, 0, 0, 0, NULL }
};

static struct service_handler operserv_service = {
	"OPERSERV", "OPERSERV", "operserv", "services.int", "Oper Services",
	60, 80, operserv_command, sizeof(operserv_command), operserv_ucommand, NULL
};

static int operserv_db_callback(void *db, int, char **, char **);

void
init_s_operserv(void)
{
	operserv_p = add_service(&operserv_service);

	loc_sqlite_exec(operserv_db_callback, "SELECT * FROM operserv");
}

static int
operserv_db_callback(void *db, int argc, char **argv, char **colnames)
{
	join_service(operserv_p, argv[0], NULL);
	return 0;
}

static void
otakeover(struct channel *chptr, int invite)
{
	part_service(operserv_p, chptr->name);

	remove_our_modes(chptr);

	if(invite)
		chptr->mode.mode = MODE_TOPIC|MODE_NOEXTERNAL|MODE_INVITEONLY;
	else
		chptr->mode.mode = MODE_TOPIC|MODE_NOEXTERNAL;

	chptr->tsinfo--;

	join_service(operserv_p, chptr->name, NULL);
}

static void
otakeover_full(struct channel *chptr)
{
	dlink_node *ptr, *next_ptr;

	modebuild_start(operserv_p, chptr);

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->bans.head)
	{
		modebuild_add(DIR_DEL, "b", ptr->data);
		my_free(ptr->data);
		dlink_destroy(ptr, &chptr->bans);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->excepts.head)
	{
		modebuild_add(DIR_DEL, "e", ptr->data);
		my_free(ptr->data);
		dlink_destroy(ptr, &chptr->excepts);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->invites.head)
	{
		modebuild_add(DIR_DEL, "I", ptr->data);
		my_free(ptr->data);
		dlink_destroy(ptr, &chptr->invites);
	}

	modebuild_finish();
}

static void
otakeover_clear(struct channel *chptr, struct client *source_p)
{
	struct chmember *msptr;
	dlink_node *ptr, *next_ptr;

	kickbuild_start();

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->users.head)
	{
		msptr = ptr->data;

		if(source_p)
		{
			if(source_p == msptr->client_p)
				continue;
		}
		else if(is_oper(msptr->client_p) || msptr->client_p->user->oper)
			continue;

		kickbuild_add(msptr->client_p->name, "Takeover Requested");
		del_chmember(msptr);
	}

	kickbuild_finish(operserv_p, chptr);
}

static void
u_oper_takeover(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[1])) == NULL)
	{
		sendto_one(conn_p, "Channel %s does not exist",
				parv[1]);
		return;
	}

	if(chptr->tsinfo < 2)
	{
		sendto_one(conn_p, "Channel %s TS too low for takeover",
				parv[1]);
		return;
	}

	if(parc > 2 && !EmptyString(parv[2]))
	{
		if(!irccmp(parv[2], "-clearall"))
		{
			otakeover(chptr, 1);
			otakeover_full(chptr);

			/* we have no associated client pointer here, so
			 * pass operserv_p as a dummy that wont get matched
			 */
			otakeover_clear(chptr, operserv_p);
		}
		else if(!irccmp(parv[2], "-clear"))
		{
			otakeover(chptr, 1);
			otakeover_full(chptr);
			otakeover_clear(chptr, NULL);
		}
		else if(!irccmp(parv[2], "-full"))
		{
			otakeover(chptr, 0);
			otakeover_full(chptr);
		}
	}
	else
		otakeover(chptr, 0);

	sendto_one(conn_p, "Channel %s has been taken over", chptr->name);
}

static int
s_oper_takeover(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_error(operserv_p, client_p,
				"Channel %s does not exist", parv[0]);
		return 0;
	}

	if(chptr->tsinfo < 2)
	{
		service_error(operserv_p, client_p,
				"Channel %s TS too low for takeover",
				chptr->name);
		return 0;
	}
	if(parc > 1 && !EmptyString(parv[1]))
	{
		if(!irccmp(parv[1], "-clearall"))
		{
			otakeover(chptr, 1);
			otakeover_full(chptr);
			otakeover_clear(chptr, client_p);
		}
		else if(!irccmp(parv[1], "-clear"))
		{
			otakeover(chptr, 1);
			otakeover_full(chptr);
			otakeover_clear(chptr, NULL);
		}
		else if(!irccmp(parv[1], "-full"))
		{
			otakeover(chptr, 0);
			otakeover_full(chptr);
		}
	}
	else
		otakeover(chptr, 0);


	service_error(operserv_p, client_p,
			"Channel %s has been taken over", chptr->name);
	return 0;
}

static void
u_oper_osjoin(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[1])) &&
	   dlink_find(operserv_p, &chptr->services))
	{
		sendto_one(conn_p, "%s already in %s",
			operserv_p->name, parv[1]);
		return;
	}

	slog(operserv_p, 1, "%s - OSJOIN %s", conn_p->name, parv[1]);

	loc_sqlite_exec(NULL, "INSERT INTO operserv VALUES(%Q, %Q)",
			parv[1], conn_p->name);

	join_service(operserv_p, parv[1], NULL);
	sendto_one(conn_p, "%s joined to %s", operserv_p->name, parv[1]);
}

static void
u_oper_ospart(struct connection_entry *conn_p, char *parv[], int parc)
{
	if(part_service(operserv_p, parv[1]))
	{
		slog(operserv_p, 1, "%s - OSPART %s", conn_p->name, parv[1]);

		loc_sqlite_exec(NULL, "DELETE FROM operserv WHERE "
				"chname = %Q", parv[1]);
		sendto_one(conn_p, "%s removed from %s",
				operserv_p->name, parv[1]);
	}
	else
		sendto_one(conn_p, "%s not in channel %s", 
				operserv_p->name, parv[1]);
}

static int
s_oper_osjoin(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) &&
	   dlink_find(operserv_p, &chptr->services))
	{
		service_error(operserv_p, client_p, "%s already in %s",
			operserv_p->name, parv[0]);
		return 0;
	}

	slog(operserv_p, 1, "%s - OSJOIN %s", 
		client_p->user->oper->name, parv[0]);

	loc_sqlite_exec(NULL, "INSERT INTO operserv VALUES(%Q, %Q)",
			parv[0], client_p->user->oper->name);

	join_service(operserv_p, parv[0], NULL);
	service_error(operserv_p, client_p,
			"%s joined to %s", operserv_p->name, parv[0]);

	return 0;
}

static int
s_oper_ospart(struct client *client_p, char *parv[], int parc)
{
	if(part_service(operserv_p, parv[0]))
	{
		slog(operserv_p, 1, "%s - OSPART %s", 
			client_p->user->oper->name, parv[0]);

		loc_sqlite_exec(NULL, "DELETE FROM operserv WHERE "
				"chname = %Q", parv[0]);
		service_error(operserv_p, client_p, "%s removed from %s",
				operserv_p->name, parv[0]);
	}
	else
		service_error(operserv_p, client_p, "%s not in channel %s", 
				operserv_p, parv[0]);

	return 0;
}

static void
u_oper_omode(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[1])) == NULL)
	{
		sendto_one(conn_p, "Channel %s does not exist",	parv[1]);
		return;
	}

	parse_full_mode(chptr, operserv_p, (const char **) parv, parc, 2);

	sendto_one(conn_p, "OMODE issued");
}

static int
s_oper_omode(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_error(operserv_p, client_p,
				"Channel %s does not exist", parv[1]);
		return 0;
	}

	parse_full_mode(chptr, operserv_p, (const char **) parv, parc, 1);

	service_error(operserv_p, client_p, "OMODE issued");
	return 0;
}

#endif
