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
#include "service.h"
#include "client.h"
#include "channel.h"
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
#include "hook.h"

static struct client *userserv_p;
static BlockHeap *user_reg_heap;

dlink_list user_reg_table[MAX_NAME_HASH];

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
static int s_user_set(struct client *, char **, int);
static int s_user_info(struct client *, char *parv[], int parc);

static struct service_command userserv_command[] =
{
	{ "USERREGISTER",	&s_user_userregister,	2, NULL, 1, 0L, 0, 0, CONF_OPER_UREGISTER, 0 },
	{ "USERDROP",		&s_user_userdrop,	1, NULL, 1, 0L, 0, 0, CONF_OPER_USERSERV, 0 },
	{ "USERSUSPEND",	&s_user_usersuspend,	1, NULL, 1, 0L, 0, 0, CONF_OPER_USERSERV, 0 },
	{ "USERUNSUSPEND",	&s_user_userunsuspend,	1, NULL, 1, 0L, 0, 0, CONF_OPER_USERSERV, 0 },
	{ "REGISTER",	&s_user_register,	2, NULL, 1, 0L, 0, 0, 0, 0 },
	{ "LOGIN",	&s_user_login,		2, NULL, 1, 0L, 0, 0, 0, 0 },
	{ "LOGOUT",	&s_user_logout,		0, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "SET",	&s_user_set,		1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "INFO",	&s_user_info,		1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "\0",		NULL,			0, NULL, 0, 0L, 0, 0, 0, 0 }
};

static struct ucommand_handler userserv_ucommand[] =
{
	{ "userregister",	u_user_userregister,	CONF_OPER_UREGISTER,	3, 1, NULL },
	{ "userdrop",		u_user_userdrop,	CONF_OPER_USERSERV,	2, 1, NULL },
	{ "usersuspend",	u_user_usersuspend,	CONF_OPER_USERSERV,	2, 1, NULL },
	{ "userunsuspend",	u_user_userunsuspend,	CONF_OPER_USERSERV,	2, 1, NULL },
	{ "\0",			NULL,			0,			0, 0, NULL }
};

static struct service_handler userserv_service = {
	"USERSERV", "USERSERV", "userserv", "services.int", "User Auth Services",
	30, 50, userserv_command, userserv_ucommand, NULL
};

static int user_db_callback(void *db, int argc, char **argv, char **colnames);
static int h_user_burst_login(void *, void *);
static void e_user_expire(void *unused);

void
init_s_userserv(void)
{
	user_reg_heap = BlockHeapCreate(sizeof(struct user_reg), HEAP_USER_REG);

	userserv_p = add_service(&userserv_service);

	loc_sqlite_exec(user_db_callback, "SELECT * FROM users");

	hook_add(h_user_burst_login, HOOK_BURST_LOGIN);

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

	if(argc < 7)
		return 0;

	if(EmptyString(argv[0]))
		return 0;

	reg_p = BlockHeapAlloc(user_reg_heap);
	strlcpy(reg_p->name, argv[0], sizeof(reg_p->name));
	reg_p->password = my_strdup(argv[1]);

	if(!EmptyString(argv[2]))
		reg_p->email = my_strdup(argv[2]);

	if(!EmptyString(argv[3]))
		reg_p->suspender = my_strdup(argv[3]);

	reg_p->reg_time = atol(argv[4]);
	reg_p->last_time = atol(argv[5]);
	reg_p->flags = atoi(argv[6]);

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

	if(IsDigit(*name) || *name == '-')
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
	dlink_node *ptr, *next_ptr;

	if(!dlink_list_length(&ureg_p->users))
		return;

	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->users.head)
	{
		target_p = ptr->data;

		target_p->user->user_reg = NULL;
		dlink_destroy(ptr, &ureg_p->users);
	}
}

static int
h_user_burst_login(void *v_client_p, void *v_username)
{
	struct client *client_p = v_client_p;
	struct user_reg *ureg_p;
	const char *username = v_username;

	/* only accepted during a burst.. */
	if(IsEOB(client_p->uplink))
		return 0;

	/* XXX - log them out? */
	if((ureg_p = find_user_reg(NULL, username)) == NULL)
		return 0;

	/* already logged in.. hmm, this shouldnt really happen */
	if(client_p->user->user_reg)
		dlink_find_destroy(client_p, &client_p->user->user_reg->users);

	client_p->user->user_reg = ureg_p;
	dlink_add_alloc(client_p, &ureg_p->users);

	return 0;
}

static void
e_user_expire(void *unused)
{
	struct user_reg *ureg_p;
	dlink_node *ptr, *next_ptr;
	int i;

	HASH_WALK_SAFE(i, MAX_NAME_HASH, ptr, next_ptr, user_reg_table)
	{
		ureg_p = ptr->data;

		if((ureg_p->last_time + config_file.uexpire_time) > CURRENT_TIME)
			continue;

		/* if theyre logged in, reset the expiry */
		if(dlink_list_length(&ureg_p->users))
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

	loc_sqlite_exec(NULL, "INSERT INTO users (username, password, email, reg_time, last_time, flags) "
			"VALUES(%Q, %Q, %Q, %lu, %lu, %u)",
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
	reg_p->suspender = my_strdup(conn_p->name);

	loc_sqlite_exec(NULL, "UPDATE users SET flags=%d, suspender=%Q WHERE username=%Q",
			reg_p->flags, conn_p->name, reg_p->name);

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
	my_free(reg_p->suspender);
	reg_p->suspender = NULL;

	loc_sqlite_exec(NULL, "UPDATE users SET flags=%d, suspender=NULL WHERE username=%Q",
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

	loc_sqlite_exec(NULL, "INSERT INTO users (username, password, email, reg_time, last_time, flags) "
			"VALUES(%Q, %Q, %Q, %lu, %lu, %u)",
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
	reg_p->suspender = my_strdup(client_p->user->oper->name);

	loc_sqlite_exec(NULL, "UPDATE users SET flags=%d, suspender=%Q WHERE username=%Q",
			reg_p->flags, reg_p->suspender, reg_p->name);

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
	my_free(reg_p->suspender);
	reg_p->suspender = NULL;

	loc_sqlite_exec(NULL, "UPDATE users SET flags=%d, suspender=NULL WHERE username=%Q",
			reg_p->flags, reg_p->name);

	service_error(userserv_p, client_p,
			"Username %s unsuspended", reg_p->name);

	return 0;
}

static int
valid_email(const char *email)
{
	char *p;

	/* no username, or no '@' */
	if(*email == '@' || (p = strchr(email, '@')) == NULL)
		return 0;

	p++;

	/* no host, or no '.' in host */
	if(EmptyString(p) || (p = strrchr(p, '.')) == NULL)
		return 0;

	p++;

	/* it ends in a '.' */
	if(EmptyString(p))
		return 0;

	return 1;
}

static int
s_user_register(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if(config_file.disable_uregister)
	{
		if(config_file.uregister_url)
			service_error(userserv_p, client_p,
				"%s::REGISTER is disabled, see %s",
				userserv_p->name, config_file.uregister_url);
		else
			service_error(userserv_p, client_p, 
					"%s::REGISTER is disabled",
					userserv_p->name);

		return 1;
	}

	if(client_p->user->user_reg != NULL)
	{
		service_error(userserv_p, client_p, "You are already logged in");
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

	if(parc < 3 || EmptyString(parv[2]))
	{
		if(config_file.uregister_email)
		{
			service_error(userserv_p, client_p, 
					"Insufficient parameters to %s::REGISTER",
					userserv_p->name);
			return 1;
		}
	}
	else if(!valid_email(parv[2]))
	{
		service_error(userserv_p, client_p, "Email %s invalid",
				parv[2]);
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

	/* check per host registration limits */
	if(config_file.uhregister_time && config_file.uhregister_amount)
	{
		struct host_entry *hent = find_host(client_p->user->host);

		/* this host has gone over the limits.. */
		if(hent->uregister >= config_file.uhregister_amount &&
		   hent->uregister_expire > CURRENT_TIME)
		{
			service_error(userserv_p, client_p,
				"%s::REGISTER rate-limited for your host, try again later",
				userserv_p->name);
			return 1;
		}

		/* its expired.. reset limits */
		if(hent->uregister_expire <= CURRENT_TIME)
		{
			hent->uregister_expire = CURRENT_TIME + config_file.uhregister_time;
			hent->uregister = 0;
		}

		hent->uregister++;
	}

	/* we need to mask the password */
	sendto_all(UMODE_REGISTER, "#:%s!%s@%s# REGISTER %s %s",
			client_p->name, client_p->user->username,
			client_p->user->host, parv[0],
			EmptyString(parv[2]) ? "" : parv[2]);
			
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

	dlink_add_alloc(client_p, &reg_p->users);
	client_p->user->user_reg = reg_p;
	add_user_reg(reg_p);

	loc_sqlite_exec(NULL, "INSERT INTO users (username, password, email, reg_time, last_time, flags) "
			"VALUES(%Q, %Q, %Q, %lu, %lu, %u)",
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
	dlink_node *ptr;

	if(client_p->user->user_reg != NULL)
	{
		service_error(userserv_p, client_p, "You are already logged in");
		return 1;
	}

	if((reg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 1;

	if(config_file.umax_logins && 
	   dlink_list_length(&reg_p->users) >= config_file.umax_logins)
	{
		service_error(userserv_p, client_p,
			"Login failed, username has %d logged in users",
			config_file.umax_logins);
		return 1;
	}

	password = get_crypt(parv[1], reg_p->password);

	if(strcmp(password, reg_p->password))
	{
		service_error(userserv_p, client_p, "Invalid password");
		return 1;
	}

	slog(userserv_p, 5, "%s - LOGIN %s", client_p->user->mask, parv[0]);

	DLINK_FOREACH(ptr, reg_p->users.head)
	{
		service_error(userserv_p, ptr->data,
				"%s has just authenticated as you (%s)",
				client_p->user->mask, reg_p->name);
	}

	if(ConnCapService(server_p))
		sendto_server(":%s ENCAP * SU %s %s",
				MYNAME, client_p->name, reg_p->name);

	client_p->user->user_reg = reg_p;
	reg_p->last_time = CURRENT_TIME;
	dlink_add_alloc(client_p, &reg_p->users);
	service_error(userserv_p, client_p, "Login successful");

	return 1;
}

static int
s_user_logout(struct client *client_p, char *parv[], int parc)
{
	dlink_find_destroy(client_p, &client_p->user->user_reg->users);
	client_p->user->user_reg = NULL;
	service_error(userserv_p, client_p, "Logout successful");

	if(ConnCapService(server_p))
		sendto_server(":%s ENCAP * SU %s", MYNAME, client_p->name);

	return 1;
}

static int
s_user_set(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *ureg_p;
	const char *arg;

	ureg_p = client_p->user->user_reg;

	arg = EmptyString(parv[1]) ? "" : parv[1];

	if(!strcasecmp(parv[0], "PASSWORD"))
	{
		const char *password;

		if(!config_file.allow_set_password)
		{
			service_error(userserv_p, client_p,
				"%s::SET::PASS is disabled", userserv_p->name);
			return 1;
		}

		if(EmptyString(parv[1]) || EmptyString(parv[2]))
		{
			service_error(userserv_p, client_p,
				"Insufficient parameters to %s::SET::PASSWORD",
				userserv_p->name);
			return 1;
		}

		password = get_crypt(parv[1], ureg_p->password);

		if(strcmp(password, ureg_p->password))
		{
			service_error(userserv_p, client_p, "Invalid password");
			return 1;
		}

		slog(userserv_p, 3, "%s %s SET PASS",
			client_p->user->mask, ureg_p->name);

		password = get_crypt(parv[2], NULL);
		my_free(ureg_p->password);
		ureg_p->password = my_strdup(password);

		loc_sqlite_exec(NULL, "UPDATE users SET password=%Q "
				"WHERE username=%Q", password, ureg_p->name);

		service_error(userserv_p, client_p,
				"Username %s PASSWORD set", ureg_p->name);
		return 1;
	}
	else if(!strcasecmp(parv[0], "EMAIL"))
	{
		if(!config_file.allow_set_email)
		{
			service_error(userserv_p, client_p,
				"%s::SET::EMAIL is disabled", userserv_p->name);
			return 1;
		}

		if(EmptyString(arg))
		{
			service_error(userserv_p, client_p,
				"Insufficient parameters to %s::SET::EMAIL",
				userserv_p->name);
			return 1;
		}

		if(!valid_email(arg))
		{
			service_error(userserv_p, client_p, "Email %s invalid",
					arg);
			return 1;
		}

		slog(userserv_p, 3, "%s %s SET EMAIL %s",
			client_p->user->mask, ureg_p->name, arg);

		my_free(ureg_p->email);
		ureg_p->email = my_strdup(arg);

		loc_sqlite_exec(NULL, "UPDATE users SET email=%Q "
				"WHERE username=%Q", arg, ureg_p->name);

		service_error(userserv_p, client_p,
				"Username %s EMAIL set %s",
				ureg_p->name, arg);
		return 1;
	}
	else if(!strcasecmp(parv[0], "PRIVATE"))
	{
		if(!strcasecmp(arg, "ON"))
			ureg_p->flags |= US_FLAGS_PRIVATE;
		else if(!strcasecmp(arg, "OFF"))
			ureg_p->flags &= ~US_FLAGS_PRIVATE;
		else
		{
			service_error(userserv_p, client_p,
				"Username %s PRIVATE is %s",
				ureg_p->name,
				(ureg_p->flags & US_FLAGS_PRIVATE) ?
				 "ON" : "OFF");
			return 1;
		}

		service_error(userserv_p, client_p,
			"Username %s PRIVATE set %s",
			ureg_p->name,
			(ureg_p->flags & US_FLAGS_PRIVATE) ? "ON" : "OFF");

		loc_sqlite_exec(NULL, "UPDATE users SET flags=%d "
				"WHERE username=%Q",
				ureg_p->flags, ureg_p->name);
		return 1;
	}

	service_error(userserv_p, client_p, "Set option invalid");
	return 1;
}

static int
s_user_info(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *ureg_p;
	time_t seconds;
	int minutes, hours, days, weeks;

	if((ureg_p = find_user_reg_nick(client_p, parv[0])) == NULL)
		return 1;

	seconds = (CURRENT_TIME - ureg_p->reg_time);
	minutes = (seconds / 60);
	weeks = (int) (seconds / 604800);
	seconds %= 604800;
	days = (int) (seconds / 86400);
	seconds %= 86400;
	hours = (int) (seconds / 3600);
	seconds %= 3600;
	minutes = (int) (seconds / 60);
	
	service_error(userserv_p, client_p, 
			"[%s] Username %s registered for %dw %dd %d%dm",
			parv[0], ureg_p->name, weeks, days, hours, minutes);

	if(ureg_p == client_p->user->user_reg)
	{
		char buf[BUFSIZE];
		struct member_reg *mreg_p;
		dlink_node *ptr;
		char *p;
		int buflen = 0;
		int mlen;

		p = buf;

		DLINK_FOREACH(ptr, ureg_p->channels.head)
		{
			mreg_p = ptr->data;

			/* "Access to: " + ":200 " */
			if((buflen + strlen(mreg_p->channel_reg->name) + 16) >=
				(BUFSIZE - 3))
			{
				service_error(userserv_p, client_p,
						"[%s] Access to: %s", 
						parv[0], buf);
				p = buf;
				buflen = 0;
			}

			mlen = sprintf(p, "%s:%d ",
					mreg_p->channel_reg->name,
					mreg_p->level);

			buflen += mlen;
			p += mlen;
		}

		/* could have access to no channels.. */
		if(buflen)
			service_error(userserv_p, client_p,
					"[%s] Access to: %s", parv[0], buf);
	}

	if(ureg_p == client_p->user->user_reg || CliOperUSAdmin(client_p))
	{
		struct client *target_p;
		dlink_node *ptr;

		service_error(userserv_p, client_p,
				"Currently logged on via:");

		DLINK_FOREACH(ptr, ureg_p->users.head)
		{
			target_p = ptr->data;

			service_error(userserv_p, client_p,
					"  %s", target_p->user->mask);
		}
	}

	return 1;
}

#endif
