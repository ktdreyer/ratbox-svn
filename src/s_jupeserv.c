/* src/s_operbot.c
 *   Contains the code for the operbot service.
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
#include "rserv.h"
#include "io.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "ucommand.h"
#include "newconf.h"

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

void
init_s_jupeserv(void)
{
	jupeserv_p = add_service(&jupe_service);
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

		jupe_p = my_malloc(sizeof(struct server_jupe));
		jupe_p->name = my_strdup(parv[0]);
		jupe_p->add = 1;

		reason = rebuild_params((const char **) parv, parc, 1);

		if(EmptyString(reason))
			jupe_p->reason = my_strdup("No Reason");
		else
			jupe_p->reason = my_strdup(reason);

		dlink_add(jupe_p, &jupe_p->node, &pending_jupes);
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
		jupe_p = my_malloc(sizeof(struct server_jupe));
		jupe_p->name = my_strdup(ajupe_p->name);
		jupe_p->points = config_file.unjupe_score;
	}

	jupe_p->points -= config_file.oper_score;

	if(jupe_p->points <= 0)
	{
		sendto_server("SQUIT %s :Unjuped", jupe_p->name);
		dlink_delete(&jupe_p->node, &pending_jupes);
		dlink_delete(&ajupe_p->node, &active_jupes);
		free_jupe(jupe_p);
		free_jupe(ajupe_p);
	}

	return 0;
}
