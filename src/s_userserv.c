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
#include "event.h"

static struct client *userserv_p;
static BlockHeap *user_reg_heap;

dlink_list user_reg_table[MAX_USER_REG_HASH];

static void u_user_userregister(struct connection_entry *, char *parv[], int parc);
static void u_user_userdrop(struct connection_entry *, char *parv[], int parc);
static void u_user_usersuspend(struct connection_entry *, char *parv[], int parc);
static void u_user_userunsuspend(struct connection_entry *, char *parv[], int parc);

static int s_user_userregister(struct client *, char *parv[], int parc);
static int s_user_userdrop(struct client *, char *parv[], int parc);
static int s_user_usersuspend(struct client *, char *parv[], int parc);
static int s_user_userunsuspend(struct client *, char *parv[], int parc);
static int s_user_register(struct client *, char *parv[], int parc);
static int s_user_login(struct client *, char *parv[], int parc);
static int s_user_logout(struct client *, char *parv[], int parc);
static int s_user_setpass(struct client *, char *parv[], int parc);
static int s_user_setemail(struct client *, char *parv[], int parc);

static struct service_command userserv_command[] =
{
	{ "USERREGISTER",	&s_user_userregister,	2, NULL, 1, 0L, 0, 0, CONF_OPER_US_REGISTER },
	{ "USERDROP",		&s_user_userdrop,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_ADMIN },
	{ "USERSUSPEND",	&s_user_usersuspend,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_ADMIN },
	{ "USERUNSUSPEND",	&s_user_userunsuspend,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_ADMIN },
	{ "REGISTER",	&s_user_register,	2, NULL, 1, 0L, 0, 0, 0 },
	{ "LOGIN",	&s_user_login,		2, NULL, 1, 0L, 0, 0, 0 },
	{ "LOGOUT",	&s_user_logout,		0, NULL, 1, 0L, 1, 0, 0 },
	{ "SETPASS",	&s_user_setpass,	2, NULL, 1, 0L, 1, 0, 0 },
	{ "SETEMAIL",	&s_user_setemail,	1, NULL, 1, 0L, 1, 0, 0 },
	{ "\0",		NULL,			0, NULL, 0, 0L, 0, 0, 0 }
};

static struct ucommand_handler userserv_ucommand[] =
{
	{ "userregister",	u_user_userregister,	CONF_OPER_US_REGISTER,	3, NULL },
	{ "userdrop",		u_user_userdrop,	CONF_OPER_US_ADMIN,	2, NULL },
	{ "usersuspend",	u_user_usersuspend,	CONF_OPER_US_ADMIN,	2, NULL },
	{ "userunsuspend",	u_user_userunsuspend,	CONF_OPER_US_ADMIN,	2, NULL },
	{ "\0",			NULL,			0,			0, NULL }
};

static struct service_handler userserv_service = {
	"USERSERV", "USERSERV", "userserv", "services.userserv", "User Auth Services",
	30, 50, userserv_command, userserv_ucommand, NULL
};

static int user_db_callback(void *db, int argc, char **argv, char **colnames);
static void e_user_expire(void *unused);

void
init_s_userserv(void)
{
	user_reg_heap = BlockHeapCreate(sizeof(struct user_reg), HEAP_USER_REG);

	userserv_p = add_service(&userserv_service);

	loc_sqlite_exec(user_db_callback, "SELECT * FROM users");

	eventAdd("userserv_expire", e_user_expire, NULL, 43200);
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
	dlink_node *ptr, *next_ptr;
	unsigned int hashv = hash_name(ureg_p->name);

	dlink_delete(&ureg_p->node, &user_reg_table[hashv]);

	loc_sqlite_exec(NULL, "DELETE FROM members WHERE username = %Q",
			ureg_p->name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->channels.head)
	{
		free_member_reg(ptr->data, 1);
	}

	loc_sqlite_exec(NULL, "DELETE FROM users WHERE username = %Q",
			ureg_p->name);

	my_free(ureg_p->password);
	BlockHeapFree(user_reg_heap, ureg_p);
}

static int
user_db_callback(void *db, int argc, char **argv, char **colnames)
{
	struct user_reg *reg_p;

	if(argc < 6)
		return 0;

	if(EmptyString(argv[0]))
		return 0;

	reg_p = BlockHeapAlloc(user_reg_heap);
	strlcpy(reg_p->name, argv[0], sizeof(reg_p->name));
	reg_p->password = my_strdup(argv[1]);

	if(!EmptyString(argv[2]))
		reg_p->email = my_strdup(argv[2]);

	reg_p->reg_time = atol(argv[3]);
	reg_p->last_time = atol(argv[4]);
	reg_p->flags = atoi(argv[5]);

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
		service_error(userserv_p, client_p, "Username %s is not registered",
				username);

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
				service_error(userserv_p, client_p, "Nickname %s is not logged in",
						name+1);
			return NULL;
		}

		return target_p->user->user_reg;
	}
	else
		return find_user_reg(client_p, name);
}

static int
valid_username(const char *name)
{
	if(strlen(name) > USERREGNAME_LEN)
		return 0;

	for(; *name; name++)
	{
		if(!IsNickChar(*name))
			return 0;
	}

	return 1;
}

static void
logout_user_reg(struct user_reg *ureg_p)
{
	struct client *target_p;
	dlink_node *ptr;

	if(!ureg_p->refcount)
		return;

	/* log out anyone using this nickname */
	DLINK_FOREACH(ptr, user_list.head)
	{
		target_p = ptr->data;

		if(target_p->user->user_reg == ureg_p)
		{
			target_p->user->user_reg = NULL;
			ureg_p->refcount--;

			if(!ureg_p->refcount)
				return;
		}
	}
}

static void
e_user_expire(void *unused)
{
	struct user_reg *ureg_p;
	dlink_node *ptr, *next_ptr;
	int i;

	HASH_WALK_SAFE(i, MAX_USER_REG_HASH, ptr, next_ptr, user_reg_table)
	{
		ureg_p = ptr->data;

		if((ureg_p->last_time + config_file.uexpire_time) > CURRENT_TIME)
			continue;

		/* if theyre logged in, reset the expiry */
		if(ureg_p->refcount)
		{
			ureg_p->last_time = CURRENT_TIME;
			continue;
		}

		free_user_reg(ureg_p);
	}
	HASH_WALK_END
}

static void
u_user_userregister(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if((reg_p = find_user_reg(NULL, parv[1])) != NULL)
	{
		sendto_one(conn_p, "Username %s is already registered", parv[1]);
		return;
	}

	if(!valid_username(parv[1]))
	{
		sendto_one(conn_p, "Username %s invalid", parv[1]);
		return;
	}

	slog(userserv_p, 2, "%s - USERREGISTER %s %s",
		conn_p->name, parv[1], 
		EmptyString(parv[3]) ? "" : parv[3]);

	reg_p = BlockHeapAlloc(user_reg_heap);
	strcpy(reg_p->name, parv[1]);

	password = get_crypt(parv[2], NULL);
	reg_p->password = my_strdup(password);

	if(!EmptyString(parv[3]))
		reg_p->email = my_strdup(parv[3]);

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	add_user_reg(reg_p);

	loc_sqlite_exec(NULL, "INSERT INTO users VALUES(%Q, %Q, %Q, %lu, %lu, %u)",
			reg_p->name, reg_p->password, 
			EmptyString(reg_p->email) ? "" : reg_p->email, 
			reg_p->reg_time, reg_p->last_time, reg_p->flags);

	sendto_one(conn_p, "Username %s registered", parv[1]);

	return;
}

static void
u_user_userdrop(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg(NULL, parv[1])) == NULL)
	{
		sendto_one(conn_p, "Username %s is not registered",
				parv[1]);
		return;
	}

	slog(userserv_p, 1, "%s - USERDROP %s", conn_p->name, ureg_p->name);

	logout_user_reg(ureg_p);

	sendto_one(conn_p, "Username %s registration dropped",
			ureg_p->name);

	free_user_reg(ureg_p);
}

static void
u_user_usersuspend(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct user_reg *reg_p;

	if((reg_p = find_user_reg(NULL, parv[1])) == NULL)
	{
		sendto_one(conn_p, "Username %s is not registered",
				parv[1]);
		return;
	}

	if(reg_p->flags & US_FLAGS_SUSPENDED)
	{
		sendto_one(conn_p, "Username %s is already suspended", reg_p->name);
		return;
	}

	slog(userserv_p, 1, "%s - USERSUSPEND %s", conn_p->name, reg_p->name);

	logout_user_reg(reg_p);

	reg_p->flags |= US_FLAGS_SUSPENDED;

	loc_sqlite_exec(NULL, "UPDATE users SET flags = %d WHERE username = %Q",
			reg_p->flags, reg_p->name);

	sendto_one(conn_p, "Username %s suspended", reg_p->name);
}

static void
u_user_userunsuspend(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct user_reg *reg_p;

	if((reg_p = find_user_reg(NULL, parv[1])) == NULL)
	{
		sendto_one(conn_p, "Username %s is not registered",
				parv[1]);
		return;
	}

	if((reg_p->flags & US_FLAGS_SUSPENDED) == 0)
	{
		sendto_one(conn_p, "Username %s is not suspended", reg_p->name);
		return;
	}

	slog(userserv_p, 1, "%s - USERUNSUSPEND %s", conn_p->name, reg_p->name);

	reg_p->flags &= ~US_FLAGS_SUSPENDED;

	loc_sqlite_exec(NULL, "UPDATE users SET flags = %d WHERE username = %Q",
			reg_p->flags, reg_p->name);

	sendto_one(conn_p, "Username %s unsuspended", reg_p->name);
}

static int
s_user_userregister(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if((reg_p = find_user_reg(NULL, parv[0])) != NULL)
	{
		service_error(userserv_p, client_p, 
			"Username %s is already registered", parv[0]);
		return 0;
	}

	if(!valid_username(parv[0]))
	{
		service_error(userserv_p, client_p, "Username %s invalid", parv[0]);
		return 0;
	}

	slog(userserv_p, 2, "%s - USERREGISTER %s %s",
		client_p->user->oper->name, parv[0], 
		EmptyString(parv[2]) ? "" : parv[2]);

	reg_p = BlockHeapAlloc(user_reg_heap);
	strcpy(reg_p->name, parv[0]);

	password = get_crypt(parv[1], NULL);
	reg_p->password = my_strdup(password);

	if(!EmptyString(parv[2]))
		reg_p->email = my_strdup(parv[2]);

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	add_user_reg(reg_p);

	loc_sqlite_exec(NULL, "INSERT INTO users VALUES(%Q, %Q, %Q, %lu, %lu, %u)",
			reg_p->name, reg_p->password, 
			EmptyString(reg_p->email) ? "" : reg_p->email, 
			reg_p->reg_time, reg_p->last_time, reg_p->flags);

	service_error(userserv_p, client_p, "Username %s registered", parv[0]);

	return 0;
}

static int
s_user_userdrop(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 1;

	slog(userserv_p, 1, "%s - USERDROP %s", 
		client_p->user->oper->name, ureg_p->name);

	logout_user_reg(ureg_p);

	service_error(userserv_p, client_p, "Username %s registration dropped",
			ureg_p->name);

	free_user_reg(ureg_p);
	return 1;
}

static int
s_user_usersuspend(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *reg_p;

	if((reg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 0;

	if(reg_p->flags & US_FLAGS_SUSPENDED)
	{
		service_error(userserv_p, client_p, 
				"Username %s is already suspended", reg_p->name);
		return 0;
	}

	slog(userserv_p, 1, "%s - USERSUSPEND %s",
		client_p->user->oper->name, reg_p->name);

	logout_user_reg(reg_p);

	reg_p->flags |= US_FLAGS_SUSPENDED;

	loc_sqlite_exec(NULL, "UPDATE users SET flags = %d WHERE username = %Q",
			reg_p->flags, reg_p->name);

	service_error(userserv_p, client_p,
			"Username %s suspended", reg_p->name);

	return 0;
}

static int
s_user_userunsuspend(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *reg_p;

	if((reg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 0;

	if((reg_p->flags & US_FLAGS_SUSPENDED) == 0)
	{
		service_error(userserv_p, client_p,
				"Username %s is not suspended", reg_p->name);
		return 0;
	}

	slog(userserv_p, 1, "%s - USERUNSUSPEND %s", 
		client_p->user->oper->name, reg_p->name);

	reg_p->flags &= ~US_FLAGS_SUSPENDED;

	loc_sqlite_exec(NULL, "UPDATE users SET flags = %d WHERE username = %Q",
			reg_p->flags, reg_p->name);

	service_error(userserv_p, client_p,
			"Username %s unsuspended", reg_p->name);

	return 0;
}

static int
s_user_register(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if(config_file.disable_uregister)
	{
		service_error(userserv_p, client_p, "%s::REGISTER is disabled", userserv_p->name);
		return 1;
	}

	if(config_file.uregister_email && (parc < 3 || EmptyString(parv[2])))
	{
		service_error(userserv_p, client_p, "Insufficient parameters to %s::REGISTER",
				userserv_p->name);
		return 1;
	}

	if(client_p->user->user_reg != NULL)
	{
		service_error(userserv_p, client_p, "You are already logged in");
		return 1;
	}

	if(ClientRegister(client_p))
	{
		service_error(userserv_p, client_p, "You have already registered a username");
		return 1;
	}

	if((reg_p = find_user_reg(NULL, parv[0])) != NULL)
	{
		service_error(userserv_p, client_p, "Username %s is already registered",
				parv[0]);
		return 1;
	}

	if(!valid_username(parv[0]))
	{
		service_error(userserv_p, client_p, "Username %s invalid", parv[0]);
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

	slog(userserv_p, 2, "%s - REGISTER %s %s",
		client_p->user->mask, parv[0], 
		EmptyString(parv[2]) ? "" : parv[2]);

	reg_p = BlockHeapAlloc(user_reg_heap);
	strcpy(reg_p->name, parv[0]);

	password = get_crypt(parv[1], NULL);
	reg_p->password = my_strdup(password);

	if(!EmptyString(parv[2]))
		reg_p->email = my_strdup(parv[2]);

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;
	reg_p->refcount++;

	client_p->user->user_reg = reg_p;
	add_user_reg(reg_p);

	SetClientRegister(client_p);

	loc_sqlite_exec(NULL, "INSERT INTO users VALUES(%Q, %Q, %Q, %lu, %lu, %u)",
			reg_p->name, reg_p->password, 
			EmptyString(reg_p->email) ? "" : reg_p->email, 
			reg_p->reg_time, reg_p->last_time, reg_p->flags);

	service_error(userserv_p, client_p, "Username %s registered", parv[0]);

	return 5;
}

static int
s_user_login(struct client *client_p, char *parv[], int parc)
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

	slog(userserv_p, 5, "%s - LOGIN %s", client_p->user->mask, parv[0]);

	client_p->user->user_reg = reg_p;
	reg_p->last_time = CURRENT_TIME;
	reg_p->refcount++;
	service_error(userserv_p, client_p, "Login successful");

	return 1;
}

static int
s_user_logout(struct client *client_p, char *parv[], int parc)
{
	client_p->user->user_reg->refcount--;
	client_p->user->user_reg = NULL;
	service_error(userserv_p, client_p, "Logout successful");

	return 1;
}

static int
s_user_setpass(struct client *client_p, char *parv[], int parc)
{
	const char *password;

	if(!config_file.allow_setpass)
	{
		service_error(userserv_p, client_p, "%s::SETPASS is disabled", userserv_p->name);
		return 1;
	}

	password = get_crypt(parv[0], client_p->user->user_reg->password);

	if(strcmp(password, client_p->user->user_reg->password))
	{
		service_error(userserv_p, client_p, "Invalid password");
		return 1;
	}

	slog(userserv_p, 3, "%s %s SETPASS",
		client_p->user->mask, client_p->user->user_reg->name);

	password = get_crypt(parv[1], NULL);
	my_free(client_p->user->user_reg->password);
	client_p->user->user_reg->password = my_strdup(password);

	loc_sqlite_exec(NULL, "UPDATE users SET password = %Q WHERE username = %Q",
			password, client_p->user->user_reg->name);

	service_error(userserv_p, client_p, "Password change successful");
	return 1;
}

static int
s_user_setemail(struct client *client_p, char *parv[], int parc)
{
	if(!config_file.allow_setemail)
	{
		service_error(userserv_p, client_p, "%s::SETEMAIL is disabled", userserv_p->name);
		return 1;
	}

	slog(userserv_p, 3, "%s %s SETEMAIL %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	my_free(client_p->user->user_reg->email);
	client_p->user->user_reg->email = my_strdup(parv[0]);

	loc_sqlite_exec(NULL, "UPDATE users SET email = %Q WHERE username = %Q",
			parv[0], client_p->user->user_reg->name);

	service_error(userserv_p, client_p, "Email change successful");
	return 1;
}

#endif
