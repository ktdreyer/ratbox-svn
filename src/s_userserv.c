/* s_userserv.c
 *   Contains code for user registration service.
 *
 * Copyright (C) 2004 Lee Hardy
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef USER_SERVICE
#include <sqlite.h>
#include "service.h"
#include "client.h"
#include "rserv.h"
#include "c_init.h"
#include "log.h"
#include "s_userserv.h"
#include "balloc.h"
#include "conf.h"
#include "io.h"

static struct client *userserv_p;
static BlockHeap *user_reg_heap;

dlink_list user_reg_table[MAX_USER_REG_HASH];

static void load_user_db(void);

static int s_userserv_register(struct client *, char *parv[], int parc);
static int s_userserv_login(struct client *, char *parv[], int parc);
static int s_userserv_logout(struct client *, char *parv[], int parc);

static struct service_command userserv_command[] =
{
	{ "REGISTER",	&s_userserv_register,	2, NULL, 0, 0, 1, 0L },
	{ "LOGIN",	&s_userserv_login,	2, NULL, 0, 0, 1, 0L },
	{ "LOGOUT",	&s_userserv_logout,	0, NULL, 0, 1, 1, 0L },
	{ "\0",		NULL,			0, NULL, 0, 0, 0, 0L }
};

static struct service_handler userserv_service = {
	"USERSERV", "USERSERV", "userserv", "services.userserv", "User Auth Services", 0,
	30, 50, userserv_command, NULL, NULL
};

void
init_s_userserv(void)
{
	user_reg_heap = BlockHeapCreate(sizeof(struct user_reg), HEAP_USER_REG);

	userserv_p = add_service(&userserv_service);
	load_user_db();
}

static void
add_user_reg(struct user_reg *reg_p)
{
	unsigned int hashv = hash_name(reg_p->name);
	dlink_add(reg_p, &reg_p->node, &user_reg_table[hashv]);
}


static int
user_db_callback(void *db, int argc, char **argv, char **colnames)
{
	struct user_reg *reg_p;

	if(argc < 5)
		return 0;

	if(EmptyString(argv[0]))
		return 0;

	reg_p = BlockHeapAlloc(user_reg_heap);
	strlcpy(reg_p->name, argv[0], sizeof(reg_p->name));
	reg_p->password = my_strdup(argv[1]);

	reg_p->reg_time = atol(argv[2]);
	reg_p->last_time = atol(argv[3]);
	reg_p->flags = atoi(argv[4]);

	add_user_reg(reg_p);

	return 0;
}

static void
load_user_db(void)
{
	char **errmsg;
	int i;

	if((i = sqlite_exec(rserv_db, "SELECT * FROM users", user_db_callback, NULL, errmsg)))
	{
		slog("ERR: Problem parsing db file: %s", *errmsg);
		die("Problem parsing db file");
	}
}

static void
write_user_db_entry(struct user_reg *reg_p)
{
	int i;

	if((i = sqlite_exec_printf(rserv_db, "INSERT INTO users VALUES(%Q, %Q, %lu, %lu, %u)", NULL, NULL, NULL,
				reg_p->name, reg_p->password, reg_p->reg_time, reg_p->last_time,
				reg_p->flags)))
	{
		slog("ERR: Problem writing to db file");
		die("Problem writing to db file");
	}
}

struct user_reg *
find_user_reg(struct client *client_p, const char *username)
{
	struct user_reg *reg_p;
	unsigned int hashv = hash_name(username);
	dlink_node *ptr;

	DLINK_FOREACH(ptr, user_reg_table[hashv].head)
	{
		reg_p = ptr->data;

		if(!strcasecmp(reg_p->name, username))
			return reg_p;
	}

	if(client_p != NULL)
		sendto_server(":%s NOTICE %s :Username %s is not registered",
				MYNAME, client_p->name, username);

	return NULL;
}

struct user_reg *
find_user_reg_nick(struct client *client_p, const char *name)
{
	if(*name == '=')
	{
		struct client *target_p;
		
		if((target_p = find_user(name+1)) == NULL ||
		   target_p->user->user_reg == NULL)
		{
			if(client_p != NULL)
				sendto_server(":%s NOTICE %s :Nickname %s is not logged in",
						MYNAME, client_p->name, name+1);
			return NULL;
		}

		return target_p->user->user_reg;
	}
	else
		return find_user_reg(client_p, name);
}

static int
s_userserv_register(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if((reg_p = find_user_reg(NULL, parv[0])) != NULL)
	{
		service_error(userserv_p, client_p, "That username is already registered");
		return 1;
	}

	if(strlen(parv[0]) > USERREGNAME_LEN)
	{
		service_error(userserv_p, client_p, "Invalid username");
		return 1;
	}

	reg_p = BlockHeapAlloc(user_reg_heap);
	strcpy(reg_p->name, parv[0]);

	password = get_crypt(parv[1], NULL);
	reg_p->password = my_strdup(password);

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	client_p->user->user_reg = reg_p;
	add_user_reg(reg_p);

	write_user_db_entry(reg_p);

	service_error(userserv_p, client_p, "Registration successful");

	return 5;
}

static int
s_userserv_login(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if(client_p->user->user_reg != NULL)
	{
		service_error(userserv_p, client_p, "You are already logged in");
		return 1;
	}

	if((reg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 1;

	password = get_crypt(parv[1], reg_p->password);

	if(strcmp(password, reg_p->password))
	{
		service_error(userserv_p, client_p, "Invalid password");
		return 1;
	}

	client_p->user->user_reg = reg_p;
	reg_p->last_time = CURRENT_TIME;
	service_error(userserv_p, client_p, "Login successful");

	return 1;
}

static int
s_userserv_logout(struct client *client_p, char *parv[], int parc)
{
	client_p->user->user_reg = NULL;
	service_error(userserv_p, client_p, "Logout successful");

	return 1;
}
#endif
