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
#include "event.h"

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

static void u_jupeserv_jupe(struct connection_entry *conn_p, char *parv[], int parc);
static void u_jupeserv_unjupe(struct connection_entry *conn_p, char *parv[], int parc);

static int s_jupeserv_jupe(struct client *, char *parv[], int parc);
static int s_jupeserv_unjupe(struct client *, char *parv[], int parc);
static int s_jupeserv_calljupe(struct client *, char *parv[], int parc);
static int s_jupeserv_callunjupe(struct client *, char *parv[], int parc);
static int s_jupeserv_pending(struct client *, char *parv[], int parc);

static struct service_command jupeserv_command[] =
{
	{ "JUPE",	&s_jupeserv_jupe,	2, NULL, 1, 0L, 0, 0, CONF_OPER_JUPE_ADMIN },
	{ "UNJUPE",	&s_jupeserv_unjupe,	1, NULL, 1, 0L, 0, 0, CONF_OPER_JUPE_ADMIN },
	{ "CALLJUPE",	&s_jupeserv_calljupe,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "CALLUNJUPE",	&s_jupeserv_callunjupe,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "PENDING",	&s_jupeserv_pending,	0, NULL, 1, 0L, 0, 1, 0 },
	{ "\0",		NULL,			0, NULL, 0, 0L, 0, 1, 0 }
};

static struct ucommand_handler jupeserv_ucommand[] =
{
	{ "jupe",	u_jupeserv_jupe,	CONF_OPER_JUPE_ADMIN,	3, NULL },
	{ "unjupe",	u_jupeserv_unjupe,	CONF_OPER_JUPE_ADMIN,	2, NULL },
	{ "\0",		NULL,			0,			0, NULL }
};

static struct service_handler jupe_service = {
	"JUPESERV", "jupeserv", "jupeserv", "services.jupeserv",
	"Jupe Services", 1, 60, 80,
	jupeserv_command, jupeserv_ucommand, NULL
};

static int jupe_db_callback(void *db, int argc, char **argv, char **colnames);
static int h_jupeserv_squit(void *name, void *unused);
static int h_jupeserv_finburst(void *unused, void *unused2);
static void e_jupeserv_expire(void *unused);

void
init_s_jupeserv(void)
{
	jupeserv_p = add_service(&jupe_service);

	hook_add(h_jupeserv_squit, HOOK_SQUIT_UNKNOWN);
	hook_add(h_jupeserv_finburst, HOOK_FINISHED_BURSTING);
	eventAdd("e_jupeserv_expire", e_jupeserv_expire, NULL, 60);

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

	if(finished_bursting)
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

static void
e_jupeserv_expire(void *unused)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, pending_jupes.head)
	{
		jupe_p = ptr->data;

		if(jupe_p->expire <= CURRENT_TIME)
		{
			dlink_delete(&jupe_p->node, &pending_jupes);
			free_jupe(jupe_p);
		}
	}
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
h_jupeserv_finburst(void *unused, void *unused2)
{
	struct client *target_p;
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, active_jupes.head)
	{
		jupe_p = ptr->data;

		if((target_p = find_server(jupe_p->name)))
		{
			sendto_server("SQUIT %s :%s", jupe_p->name, jupe_p->reason);
			exit_client(target_p);
		}

		sendto_server(":%s SERVER %s 2 :JUPED: %s",
				MYNAME, jupe_p->name, jupe_p->reason);
	}

	return 0;
}

static int
valid_jupe(const char *servername)
{
	if(!valid_servername(servername) || strchr(servername, '*') ||
	   !irccmp(servername, MYNAME))
		return 0;

	/* cant jupe our uplink */
	if(server_p && !irccmp(servername, server_p->name))
		return 0;

	return 1;
}

static void
u_jupeserv_jupe(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct server_jupe *jupe_p;
	const char *reason;

	if(!valid_jupe(parv[1]))
	{
		sendto_one(conn_p, "Servername %s is invalid", parv[1]);
		return;
	}

	if((jupe_p = find_jupe(parv[1], &active_jupes)))
	{
		sendto_one(conn_p, "Server %s is already juped", jupe_p->name);
		return;
	}

	/* if theres a pending oper jupe, cancel it because we're gunna
	 * place a proper one.. --fl
	 */
	if((jupe_p = find_jupe(parv[1], &pending_jupes)))
	{
		dlink_delete(&jupe_p->node, &pending_jupes);
		free_jupe(jupe_p);
	}

	jupe_p = make_jupe(parv[1]);
	reason = rebuild_params((const char **) parv, parc, 2);

	if(EmptyString(reason))
		jupe_p->reason = my_strdup("No Reason");
	else
		jupe_p->reason = my_strdup(reason);

	loc_sqlite_exec(NULL, "INSERT INTO jupes VALUES(%Q, %Q)",
			jupe_p->name, jupe_p->reason);

	sendto_server(":%s WALLOPS :JUPE %s by %s [%s]",
			MYNAME, jupe_p->name, conn_p->name, jupe_p->reason);
	add_jupe(jupe_p);
}

static void
u_jupeserv_unjupe(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct server_jupe *ajupe_p, *jupe_p;

	if((jupe_p = find_jupe(parv[1], &active_jupes)) == NULL)
	{
		sendto_one(conn_p, "Server %s is not juped", parv[1]);
		return;
	}

	if((ajupe_p = find_jupe(parv[1], &pending_jupes)))
	{
		dlink_delete(&ajupe_p->node, &pending_jupes);
		free_jupe(ajupe_p);
	}

	loc_sqlite_exec(NULL, "DELETE FROM jupes WHERE servername = %Q",
			jupe_p->name);

	sendto_server(":%s WALLOPS :UNJUPE %s by %s",
			MYNAME, jupe_p->name, conn_p->name);

	sendto_server("SQUIT %s :Unjuped", jupe_p->name);
	dlink_delete(&jupe_p->node, &active_jupes);
	free_jupe(jupe_p);
}

static int
s_jupeserv_jupe(struct client *client_p, char *parv[], int parc)
{
	struct server_jupe *jupe_p;
	const char *reason;

	if(!valid_jupe(parv[1]))
	{
		service_error(jupeserv_p, client_p, "Servername %s is invalid",
				parv[0]);
		return 0;
	}

	if((jupe_p = find_jupe(parv[0], &active_jupes)))
	{
		service_error(jupeserv_p, client_p, "Server %s is already juped",
				jupe_p->name);
		return 0;
	}

	/* if theres a pending oper jupe, cancel it because we're gunna
	 * place a proper one.. --fl
	 */
	if((jupe_p = find_jupe(parv[0], &pending_jupes)))
	{
		dlink_delete(&jupe_p->node, &pending_jupes);
		free_jupe(jupe_p);
	}

	jupe_p = make_jupe(parv[0]);
	reason = rebuild_params((const char **) parv, parc, 1);

	if(EmptyString(reason))
		jupe_p->reason = my_strdup("No Reason");
	else
		jupe_p->reason = my_strdup(reason);

	loc_sqlite_exec(NULL, "INSERT INTO jupes VALUES(%Q, %Q)",
			jupe_p->name, jupe_p->reason);

	sendto_server(":%s WALLOPS :JUPE set on %s by %s!%s@%s on %s [%s]",
			MYNAME, jupe_p->name, client_p->name,
			client_p->user->username, client_p->user->servername,
			client_p->user->host, jupe_p->reason);

	add_jupe(jupe_p);

	return 0;
}

static int
s_jupeserv_unjupe(struct client *client_p, char *parv[], int parc)
{
	struct server_jupe *ajupe_p, *jupe_p;

	if((jupe_p = find_jupe(parv[0], &active_jupes)) == NULL)
	{
		service_error(jupeserv_p, client_p, "Server %s is not juped",
				parv[0]);
		return 0;
	}

	if((ajupe_p = find_jupe(parv[0], &pending_jupes)))
	{
		dlink_delete(&ajupe_p->node, &pending_jupes);
		free_jupe(ajupe_p);
	}

	loc_sqlite_exec(NULL, "DELETE FROM jupes WHERE servername = %Q",
			jupe_p->name);

	sendto_server(":%s WALLOPS :UNJUPE set on %s by %s!%s@%s",
			MYNAME, jupe_p->name, client_p->name,
			client_p->user->username, client_p->user->host);

	sendto_server("SQUIT %s :Unjuped", jupe_p->name);
	dlink_delete(&jupe_p->node, &active_jupes);
	free_jupe(jupe_p);

	return 0;
}

static int
s_jupeserv_calljupe(struct client *client_p, char *parv[], int parc)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	if(!valid_jupe(parv[1]))
	{
		service_error(jupeserv_p, client_p, "Servername %s is invalid",
				parv[0]);
		return 0;
	}

	if(!config_file.jupe_score || !config_file.oper_score)
	{
		service_error(jupeserv_p, client_p, "Oper jupes are disabled");
		return 0;
	}

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

	jupe_p->expire = CURRENT_TIME + config_file.pending_time;
	jupe_p->points += config_file.oper_score;
	dlink_add_alloc(my_strdup(client_p->user->servername), &jupe_p->servers);

	if(jupe_p->points >= config_file.jupe_score)
	{
		sendto_server(":%s WALLOPS :JUPE triggered on %s by %s!%s@%s on %s [%s]",
				MYNAME, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host, 
				client_p->user->servername, jupe_p->reason);

		loc_sqlite_exec(NULL, "INSERT INTO jupes VALUES(%Q, %Q)",
				jupe_p->name, jupe_p->reason);

		add_jupe(jupe_p);
	}
	else
		sendto_server(":%s WALLOPS :JUPE requested on %s by %s!%s@%s on %s [%s]",
				MYNAME, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host, 
				client_p->user->servername, jupe_p->reason);

	return 0;
}

static int
s_jupeserv_callunjupe(struct client *client_p, char *parv[], int parc)
{
	struct server_jupe *ajupe_p, *jupe_p;

	if(!config_file.unjupe_score || !config_file.oper_score)
	{
		service_error(jupeserv_p, client_p, "Oper jupes are disabled");
		return 0;
	}

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

	jupe_p->expire = CURRENT_TIME + config_file.pending_time;
	jupe_p->points -= config_file.oper_score;

	if(jupe_p->points <= 0)
	{
		sendto_server(":%s WALLOPS :UNJUPE triggered on %s by %s!%s@%s on %s",
				MYNAME, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host, 
				client_p->user->servername);

		loc_sqlite_exec(NULL, "DELETE FROM jupes WHERE servername = %Q",
				jupe_p->name);

		sendto_server("SQUIT %s :Unjuped", jupe_p->name);
		dlink_delete(&jupe_p->node, &pending_jupes);
		dlink_delete(&ajupe_p->node, &active_jupes);
		free_jupe(jupe_p);
		free_jupe(ajupe_p);
	}
	else
		sendto_server(":%s WALLOPS :UNJUPE requested on %s by %s!%s@%s on %s",
				MYNAME, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host, 
				client_p->user->servername);

	return 0;
}

static int
s_jupeserv_pending(struct client *client_p, char *parv[], int parc)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	if(!config_file.oper_score)
	{
		service_error(jupeserv_p, client_p, "Oper jupes are disabled");
		return 0;
	}

	if(!dlink_list_length(&pending_jupes))
	{
		service_error(jupeserv_p, client_p, "No pending jupes");
		return 0;
	}

	service_error(jupeserv_p, client_p, "Pending jupes:");

	DLINK_FOREACH(ptr, pending_jupes.head)
	{
		jupe_p = ptr->data;

		service_error(jupeserv_p, client_p, "  %s %s %d/%d points (%s)",
				jupe_p->add ? "JUPE" : "UNJUPE",
				jupe_p->name, jupe_p->points,
				jupe_p->add ? config_file.jupe_score : config_file.unjupe_score,
				jupe_p->reason);
	}

	service_error(jupeserv_p, client_p, "End of pending jupes");

	return 0;
}

#endif
