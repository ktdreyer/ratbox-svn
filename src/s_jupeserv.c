/* src/s_operbot.c
 *   Contains the code for the operbot service.
 *
 * Copyright (C) 2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef ENABLE_JUPESERV
#include "service.h"
#include "client.h"
#include "channel.h"
#include "rserv.h"
#include "io.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "ucommand.h"
#include "newconf.h"
#include "hook.h"

struct server_jupe
{
	char *name;
	char *reason;
	int points;
	int add;
	time_t expire;
	dlink_node node;
	dlink_list servers;
};

static dlink_list pending_jupes;
static dlink_list active_jupes;

static struct client *jupeserv_p;

static int s_jupeserv_calljupe(struct client *, char *parv[], int parc);
static int s_jupeserv_callunjupe(struct client *, char *parv[], int parc);

static struct service_command jupeserv_command[] =
{
	{ "CALLJUPE",	&s_jupeserv_calljupe,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "CALLUNJUPE",	&s_jupeserv_callunjupe,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "\0",		NULL,			0, NULL, 0, 0L, 0, 1, 0 }
};

static struct service_handler jupe_service = {
	"JUPES", "jupes", "jupes", "services.jupes",
	"Jupe Services", 1, 60, 80,
	jupeserv_command, NULL, NULL
};

static int jupe_db_callback(void *db, int argc, char **argv, char **colnames);
static int h_jupeserv_squit(void *name, void *unused);

void
init_s_jupeserv(void)
{
	jupeserv_p = add_service(&jupe_service);
	hook_add(h_jupeserv_squit, HOOK_SQUIT_UNKNOWN);
	loc_sqlite_exec(jupe_db_callback, "SELECT * FROM jupes");
}

static struct server_jupe *
make_jupe(const char *name)
{
	struct server_jupe *jupe_p = my_malloc(sizeof(struct server_jupe));
	jupe_p->name = my_strdup(name);
	dlink_add(jupe_p, &jupe_p->node, &pending_jupes);
	return jupe_p;
}

static void
add_jupe(struct server_jupe *jupe_p)
{
	struct client *target_p;

	if((target_p = find_server(jupe_p->name)))
	{
		sendto_server("SQUIT %s :%s", jupe_p->name, jupe_p->reason);
		exit_client(target_p);
	}

	sendto_server(":%s SERVER %s 1 :JUPED: %s",
			MYNAME, jupe_p->name, jupe_p->reason);
	dlink_move_node(&jupe_p->node, &pending_jupes, &active_jupes);
}

static void
free_jupe(struct server_jupe *jupe_p)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, jupe_p->servers.head)
	{
		my_free(ptr->data);
		dlink_destroy(ptr, &jupe_p->servers);
	}

	my_free(jupe_p->name);
	my_free(jupe_p->reason);
	my_free(jupe_p);
}

static struct server_jupe *
find_jupe(const char *name, dlink_list *list)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		jupe_p = ptr->data;

		if(!irccmp(jupe_p->name, name))
			return jupe_p;
	}

	return NULL;
}

static int
jupe_db_callback(void *db, int argc, char **argv, char **colnames)
{
	struct server_jupe *jupe_p;

	jupe_p = make_jupe(argv[0]);
	jupe_p->reason = my_strdup(argv[1]);

	add_jupe(jupe_p);
	return 0;
}

static int
h_jupeserv_squit(void *name, void *unused)
{
	struct server_jupe *jupe_p;

	if((jupe_p = find_jupe(name, &active_jupes)))
	{
		sendto_server(":%s SERVER %s 2 :JUPED: %s",
				MYNAME, jupe_p->name, jupe_p->reason);
		return -1;
	}

	return 0;
}

static int
s_jupeserv_calljupe(struct client *client_p, char *parv[], int parc)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	if((jupe_p = find_jupe(parv[0], &active_jupes)))
	{
		service_error(jupeserv_p, client_p, "Server %s is already juped",
				jupe_p->name);
		return 0;
	}

	if((jupe_p = find_jupe(parv[0], &pending_jupes)) == NULL)
	{
		const char *reason;

		jupe_p = make_jupe(parv[0]);
		jupe_p->add = 1;

		reason = rebuild_params((const char **) parv, parc, 1);

		if(EmptyString(reason))
			jupe_p->reason = my_strdup("No Reason");
		else
			jupe_p->reason = my_strdup(reason);
	}

	DLINK_FOREACH(ptr, jupe_p->servers.head)
	{
		if(!irccmp((const char *) ptr->data, client_p->user->servername))
		{
			service_error(jupeserv_p, client_p, "Server %s jupe already requested by your server",
					jupe_p->name);
			return 0;
		}
	}

	jupe_p->points += config_file.oper_score;
	dlink_add_alloc(my_strdup(client_p->user->servername), &jupe_p->servers);

	if(jupe_p->points >= config_file.jupe_score)
	{
		loc_sqlite_exec(NULL, "INSERT INTO jupes VALUES(%Q, %Q)",
				jupe_p->name, jupe_p->reason);

		add_jupe(jupe_p);
	}

	return 0;
}

static int
s_jupeserv_callunjupe(struct client *client_p, char *parv[], int parc)
{
	struct server_jupe *ajupe_p, *jupe_p;

	if((ajupe_p = find_jupe(parv[0], &active_jupes)) == NULL)
	{
		service_error(jupeserv_p, client_p, "Server %s is not juped",
				parv[0]);
		return 0;
	}

	if((jupe_p = find_jupe(parv[0], &pending_jupes)) == NULL)
	{
		jupe_p = make_jupe(ajupe_p->name);
		jupe_p->points = config_file.unjupe_score;
	}

	jupe_p->points -= config_file.oper_score;

	if(jupe_p->points <= 0)
	{
		loc_sqlite_exec(NULL, "DELETE FROM jupes WHERE servername = %Q",
				jupe_p->name);

		sendto_server("SQUIT %s :Unjuped", jupe_p->name);
		dlink_delete(&jupe_p->node, &pending_jupes);
		dlink_delete(&ajupe_p->node, &active_jupes);
		free_jupe(jupe_p);
		free_jupe(ajupe_p);
	}

	return 0;
}

#endif
