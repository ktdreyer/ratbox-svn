/* s_userserv.c
 *   Contains code for user registration service.
 *
 * Copyright (C) 2004 Lee Hardy
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef ENABLE_USERSERV
#include <sqlite.h>
#include "service.h"
#include "client.h"
#include "rserv.h"
#include "c_init.h"
#include "log.h"
#include "s_chanserv.h"
#include "s_userserv.h"
#include "ucommand.h"
#include "balloc.h"
#include "conf.h"
#include "io.h"

static struct client *userserv_p;
static BlockHeap *user_reg_heap;

dlink_list user_reg_table[MAX_USER_REG_HASH];

static void u_userserv_udrop(struct connection_entry *, char *parv[], int parc);

static int s_userserv_udrop(struct client *, char *parv[], int parc);
static int s_userserv_register(struct client *, char *parv[], int parc);
static int s_userserv_login(struct client *, char *parv[], int parc);
static int s_userserv_logout(struct client *, char *parv[], int parc);

static struct service_command userserv_command[] =
{
	{ "UDROP",	&s_userserv_udrop,	2, NULL, 1, 0L, 0, 0, CONF_OPER_US_ADMIN },
	{ "REGISTER",	&s_userserv_register,	2, NULL, 1, 0L, 0, 0, 0 },
	{ "LOGIN",	&s_userserv_login,	2, NULL, 1, 0L, 0, 0, 0 },
	{ "LOGOUT",	&s_userserv_logout,	0, NULL, 1, 0L, 1, 0, 0 },
	{ "\0",		NULL,			0, NULL, 0, 0L, 0, 0, 0 }
};

static struct ucommand_handler userserv_ucommand[] =
{
	{ "udrop",	u_userserv_udrop,	CONF_OPER_US_ADMIN,	2, NULL },
	{ "\0",		NULL,			0,			0, NULL }
};

static struct service_handler userserv_service = {
	"USERSERV", "USERSERV", "userserv", "services.userserv", "User Auth Services", 0,
	30, 50, userserv_command, userserv_ucommand, NULL
};

static int user_db_callback(void *db, int argc, char **argv, char **colnames);

void
init_s_userserv(void)
{
	user_reg_heap = BlockHeapCreate(sizeof(struct user_reg), HEAP_USER_REG);

	userserv_p = add_service(&userserv_service);

	loc_sqlite_exec(user_db_callback, "SELECT * FROM users");
}

static void
add_user_reg(struct user_reg *reg_p)
{
	unsigned int hashv = hash_name(reg_p->name);
	dlink_add(reg_p, &reg_p->node, &user_reg_table[hashv]);
}

static void
free_user_reg(struct user_reg *ureg_p)
{
	unsigned int hashv = hash_name(ureg_p->name);
	dlink_delete(&ureg_p->node, &user_reg_table[hashv]);

	my_free(ureg_p->password);
	BlockHeapFree(user_reg_heap, ureg_p);
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

static void
u_userserv_udrop(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	dlink_node *ptr, *next_ptr;

	if((ureg_p = find_user_reg(NULL, parv[1])) == NULL)
	{
		if(*parv[1] == '=')
			sendto_one(conn_p, "Nickname %s is not logged in",
					parv[1]);
		else
			sendto_one(conn_p, "Username %s is not registered",
					parv[1]);
		return;
	}

	loc_sqlite_exec(NULL, "DELETE FROM members WHERE username = %Q",
			ureg_p->name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->channels.head)
	{
		mreg_p = ptr->data;

		/* only member of this channel, so drop the channel too */
		if(dlink_list_length(&mreg_p->channel_reg->users) == 1)
			free_channel_reg(mreg_p->channel_reg);
		else
			free_member_reg(mreg_p);
	}

	loc_sqlite_exec(NULL, "DELETE FROM users WHERE username = %Q",
			ureg_p->name);

	sendto_one(conn_p, "Username %s registration dropped",
			ureg_p->name);

	free_user_reg(ureg_p);
}

static int
s_userserv_udrop(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	dlink_node *ptr, *next_ptr;

	if((ureg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 1;

	loc_sqlite_exec(NULL, "DELETE FROM members WHERE username = %Q",
			ureg_p->name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->channels.head)
	{
		mreg_p = ptr->data;

		/* only member of this channel, so drop the channel too */
		if(dlink_list_length(&mreg_p->channel_reg->users) == 1)
			free_channel_reg(mreg_p->channel_reg);
		else
			free_member_reg(mreg_p);
	}

	loc_sqlite_exec(NULL, "DELETE FROM users WHERE username = %Q",
			ureg_p->name);

	service_error(userserv_p, client_p, "Username %s registration dropped",
			ureg_p->name);

	free_user_reg(ureg_p);
	return 1;
}

static int
s_userserv_register(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if(config_file.disable_uregister)
	{
		service_error(userserv_p, client_p, "%s::REGISTER is disabled", userserv_p->name);
		return 1;
	}

	if(client_p->user->user_reg != NULL)
	{
		service_error(userserv_p, client_p, "You are already logged in");
		return 1;
	}

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

	/* apply timed registration limits */
	if(config_file.uregister_time && config_file.uregister_amount)
	{
		static time_t last_time = 0;
		static int last_count = 0;

		if((last_time + config_file.uregister_time) < CURRENT_TIME)
		{
			last_time = CURRENT_TIME;
			last_count = 1;
		}
		else if(last_count >= config_file.uregister_amount)
		{
			service_error(userserv_p, client_p, 
				"%s::REGISTER rate-limited, try again shortly",
				userserv_p->name);
			return 1;
		}
		else
			last_count++;
	}


	reg_p = BlockHeapAlloc(user_reg_heap);
	strcpy(reg_p->name, parv[0]);

	password = get_crypt(parv[1], NULL);
	reg_p->password = my_strdup(password);

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	client_p->user->user_reg = reg_p;
	add_user_reg(reg_p);

	loc_sqlite_exec(NULL, "INSERT INTO users VALUES(%Q, %Q, %lu, %lu, %u)",
			reg_p->name, reg_p->password, reg_p->reg_time, reg_p->last_time,
			reg_p->flags);

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
