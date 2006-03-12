/* s_userserv.c
 *   Contains code for user registration service.
 *
 * Copyright (C) 2004-2005 Lee Hardy
 * Copyright (C) 2004-2005 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef ENABLE_USERSERV
#include "rsdb.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "rserv.h"
#include "c_init.h"
#include "log.h"
#include "s_chanserv.h"
#include "s_userserv.h"
#include "s_nickserv.h"
#include "ucommand.h"
#include "balloc.h"
#include "conf.h"
#include "io.h"
#include "event.h"
#include "hook.h"
#include "email.h"

static void init_s_userserv(void);

static struct client *userserv_p;
static BlockHeap *user_reg_heap;

dlink_list user_reg_table[MAX_NAME_HASH];

static int o_user_userregister(struct client *, struct lconn *, const char **, int);
static int o_user_userdrop(struct client *, struct lconn *, const char **, int);
static int o_user_usersuspend(struct client *, struct lconn *, const char **, int);
static int o_user_userunsuspend(struct client *, struct lconn *, const char **, int);
static int o_user_userlist(struct client *, struct lconn *, const char **, int);
static int o_user_userinfo(struct client *, struct lconn *, const char **, int);
static int o_user_usersetpass(struct client *, struct lconn *, const char **, int);

static int s_user_register(struct client *, struct lconn *, const char **, int);
static int s_user_login(struct client *, struct lconn *, const char **, int);
static int s_user_logout(struct client *, struct lconn *, const char **, int);
static int s_user_resetpass(struct client *, struct lconn *, const char **, int);
static int s_user_set(struct client *, struct lconn *, const char **, int);
static int s_user_info(struct client *, struct lconn *, const char **, int);

static struct service_command userserv_command[] =
{
	{ "USERREGISTER",	&o_user_userregister,	2, NULL, 1, 0L, 0, 0, CONF_OPER_US_REGISTER, 0 },
	{ "USERDROP",		&o_user_userdrop,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_DROP, 0 },
	{ "USERSUSPEND",	&o_user_usersuspend,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_SUSPEND, 0 },
	{ "USERUNSUSPEND",	&o_user_userunsuspend,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_SUSPEND, 0 },
	{ "USERLIST",		&o_user_userlist,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_LIST, 0 },
	{ "USERINFO",		&o_user_userinfo,	0, NULL, 1, 0L, 0, 0, CONF_OPER_US_INFO, 0 },
	{ "USERSETPASS",	&o_user_usersetpass,	2, NULL, 1, 0L, 0, 0, CONF_OPER_US_SETPASS, 0 },
	{ "REGISTER",	&s_user_register,	2, NULL, 1, 0L, 0, 0, 0, 0 },
	{ "LOGIN",	&s_user_login,		2, NULL, 1, 0L, 0, 0, 0, 0 },
	{ "LOGOUT",	&s_user_logout,		0, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "RESETPASS",	&s_user_resetpass,	1, NULL, 1, 0L, 0, 0, 0, 0 },
	{ "SET",	&s_user_set,		1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "INFO",	&s_user_info,		1, NULL, 1, 0L, 1, 0, 0, 0 }
};

static struct ucommand_handler userserv_ucommand[] =
{
	{ "userregister",	o_user_userregister,	0, CONF_OPER_US_REGISTER,	2, 1, NULL },
	{ "userdrop",		o_user_userdrop,	0, CONF_OPER_US_DROP,	1, 1, NULL },
	{ "usersuspend",	o_user_usersuspend,	0, CONF_OPER_US_SUSPEND,	1, 1, NULL },
	{ "userunsuspend",	o_user_userunsuspend,	0, CONF_OPER_US_SUSPEND,	1, 1, NULL },
	{ "userlist",		o_user_userlist,	0, CONF_OPER_US_LIST,	0, 1, NULL },
	{ "userinfo",		o_user_userinfo,	0, CONF_OPER_US_INFO,	1, 1, NULL },
	{ "usersetpass",	o_user_usersetpass,	0, CONF_OPER_US_SETPASS,	2, 1, NULL },
	{ "\0",			NULL,			0, 0,			0, 0, NULL }
};

static struct service_handler userserv_service = {
	"USERSERV", "USERSERV", "userserv", "services.int", "User Auth Services",
	30, 50, userserv_command, sizeof(userserv_command), userserv_ucommand, init_s_userserv, NULL
};

static int user_db_callback(int argc, const char **argv);
static int h_user_burst_login(void *, void *);
static void e_user_expire(void *unused);
static void e_user_expire_resetpass(void *unused);

static void dump_user_info(struct client *, struct lconn *, struct user_reg *);

static int valid_email(const char *email);

void
preinit_s_userserv(void)
{
	userserv_p = add_service(&userserv_service);
}

static void
init_s_userserv(void)
{
	user_reg_heap = BlockHeapCreate(sizeof(struct user_reg), HEAP_USER_REG);

	rsdb_exec(user_db_callback, 
			"SELECT username, password, email, suspender, "
			"reg_time, last_time, flags FROM users");

	hook_add(h_user_burst_login, HOOK_BURST_LOGIN);

	eventAdd("userserv_expire", e_user_expire, NULL, 21600);
	eventAdd("userserv_expire_resetpass", e_user_expire_resetpass, NULL, 3600);
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

	rsdb_exec(NULL, "DELETE FROM users_resetpass WHERE username = '%Q'",
			ureg_p->name);

	rsdb_exec(NULL, "DELETE FROM members WHERE username = '%Q'",
			ureg_p->name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->channels.head)
	{
		free_member_reg(ptr->data, 1);
	}

#ifdef ENABLE_NICKSERV
	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->nicks.head)
	{
		free_nick_reg(ptr->data);
	}
#endif
			
	rsdb_exec(NULL, "DELETE FROM users WHERE username = '%Q'",
			ureg_p->name);

	my_free(ureg_p->password);
	BlockHeapFree(user_reg_heap, ureg_p);
}

static int
user_db_callback(int argc, const char **argv)
{
	struct user_reg *reg_p;

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

		sendto_server(":%s ENCAP * SU %s", MYNAME, target_p->name);

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

	/* nickname that isnt actually registered.. log them out */
	if((ureg_p = find_user_reg(NULL, username)) == NULL)
	{
		sendto_server(":%s ENCAP * SU %s", MYNAME, client_p->name);
		return 0;
	}

	/* already logged in.. hmm, this shouldnt really happen */
	if(client_p->user->user_reg)
		dlink_find_destroy(client_p, &client_p->user->user_reg->users);

	client_p->user->user_reg = ureg_p;
	dlink_add_alloc(client_p, &ureg_p->users);

	ureg_p->last_time = CURRENT_TIME;
	ureg_p->flags |= US_FLAGS_NEEDUPDATE;

	return 0;
}

static void
e_user_expire(void *unused)
{
	struct user_reg *ureg_p;
	dlink_node *ptr, *next_ptr;
	int i;

	/* Start a transaction, we're going to make a lot of changes */
	rsdb_transaction(RSDB_TRANS_START);

	HASH_WALK_SAFE(i, MAX_NAME_HASH, ptr, next_ptr, user_reg_table)
	{
		ureg_p = ptr->data;

		/* nuke unverified accounts first */
		if(ureg_p->flags & US_FLAGS_NEVERLOGGEDIN &&
		   (ureg_p->reg_time + config_file.uexpire_unverified_time) <= CURRENT_TIME)
		{
			free_user_reg(ureg_p);
			continue;
		}
				
		/* if they're logged in, reset the expiry */
		if(dlink_list_length(&ureg_p->users))
		{
			ureg_p->last_time = CURRENT_TIME;
			ureg_p->flags |= US_FLAGS_NEEDUPDATE;
		}

		if(ureg_p->flags & US_FLAGS_NEEDUPDATE)
		{
			ureg_p->flags &= ~US_FLAGS_NEEDUPDATE;
			rsdb_exec(NULL, "UPDATE users SET last_time=%lu"
					" WHERE username='%Q'",
			ureg_p->last_time, ureg_p->name);
		}

		if((ureg_p->last_time + config_file.uexpire_time) > CURRENT_TIME)
			continue;

		free_user_reg(ureg_p);
	}
	HASH_WALK_END

	rsdb_transaction(RSDB_TRANS_END);
}

static void
e_user_expire_resetpass(void *unused)
{
	rsdb_exec(NULL, "DELETE FROM users_resetpass WHERE time <= '%lu'",
			CURRENT_TIME - config_file.uresetpass_duration);
}

static int
o_user_userregister(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if((reg_p = find_user_reg(NULL, parv[0])) != NULL)
	{
		service_send(userserv_p, client_p, conn_p,
				"Username %s is already registered", reg_p->name);
		return 0;
	}

	if(!valid_username(parv[0]))
	{
		service_send(userserv_p, client_p, conn_p,
				"Username %s invalid", parv[0]);
		return 0;
	}

	if(strlen(parv[1]) > PASSWDLEN)
	{
		service_send(userserv_p, client_p, conn_p,
				"Password too long");
		return 0;
	}

	slog(userserv_p, 2, "%s - USERREGISTER %s %s",
		OPER_NAME(client_p, conn_p), parv[0], 
		EmptyString(parv[2]) ? "-" : parv[2]);

	reg_p = BlockHeapAlloc(user_reg_heap);
	strcpy(reg_p->name, parv[0]);

	password = get_crypt(parv[1], NULL);
	reg_p->password = my_strdup(password);

	if(!EmptyString(parv[2]))
	{
		if(valid_email(parv[2]))
			reg_p->email = my_strdup(parv[2]);
		else
			service_send(userserv_p, client_p, conn_p, "Email %s invalid, ignoring", parv[2]);
	}

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	add_user_reg(reg_p);

	rsdb_exec(NULL, "INSERT INTO users (username, password, email, reg_time, last_time, flags) "
			"VALUES('%Q', '%Q', '%Q', %lu, %lu, %u)",
			reg_p->name, reg_p->password, 
			EmptyString(reg_p->email) ? "" : reg_p->email, 
			reg_p->reg_time, reg_p->last_time, reg_p->flags);

	service_send(userserv_p, client_p, conn_p,
			"Username %s registered", parv[0]);
	return 0;
}

static int
o_user_userdrop(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg(NULL, parv[0])) == NULL)
	{
		service_send(userserv_p, client_p, conn_p,
				"Username %s is not registered", parv[0]);
		return 0;
	}

	slog(userserv_p, 1, "%s - USERDROP %s", 
		OPER_NAME(client_p, conn_p), ureg_p->name);

	logout_user_reg(ureg_p);

	service_send(userserv_p, client_p, conn_p,
			"Username %s registration dropped", ureg_p->name);

	free_user_reg(ureg_p);
	return 0;
}

static int
o_user_usersuspend(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;

	if((reg_p = find_user_reg(NULL, parv[0])) == NULL)
	{
		service_send(userserv_p, client_p, conn_p,
				"Username %s is not registered", parv[0]);
		return 0;
	}

	if(reg_p->flags & US_FLAGS_SUSPENDED)
	{
		service_send(userserv_p, client_p, conn_p,
				"Username %s is already suspended", reg_p->name);
		return 0;
	}

	slog(userserv_p, 1, "%s - USERSUSPEND %s",
		OPER_NAME(client_p, conn_p), reg_p->name);

	logout_user_reg(reg_p);

	reg_p->flags |= US_FLAGS_SUSPENDED;
	reg_p->suspender = my_strdup(OPER_NAME(client_p, conn_p));

	rsdb_exec(NULL, "UPDATE users SET flags=%d, suspender='%Q' WHERE username='%Q'",
			reg_p->flags, reg_p->suspender, reg_p->name);

	service_send(userserv_p, client_p, conn_p,
			"Username %s suspended", reg_p->name);
	return 0;
}

static int
o_user_userunsuspend(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;

	if((reg_p = find_user_reg(NULL, parv[0])) == NULL)
	{
		service_send(userserv_p, client_p, conn_p,
				"Username %s is not registered", parv[0]);
		return 0;
	}

	if((reg_p->flags & US_FLAGS_SUSPENDED) == 0)
	{
		service_send(userserv_p, client_p, conn_p,
				"Username %s is not suspended", reg_p->name);
		return 0;
	}

	slog(userserv_p, 1, "%s - USERUNSUSPEND %s",
		OPER_NAME(client_p, conn_p), reg_p->name);

	reg_p->flags &= ~US_FLAGS_SUSPENDED;
	my_free(reg_p->suspender);
	reg_p->suspender = NULL;

	rsdb_exec(NULL, "UPDATE users SET flags=%d, suspender=NULL WHERE username='%Q'",
			reg_p->flags, reg_p->name);

	service_send(userserv_p, client_p, conn_p,
			"Username %s unsuspended", reg_p->name);
	return 0;
}

#define USERLIST_LEN	350	/* should be long enough */

static int
o_user_userlist(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static char def_mask[] = "*";
	static char buf[BUFSIZE];
	struct user_reg *ureg_p;
	const char *mask = def_mask;
	dlink_node *ptr;
	unsigned int limit = 100;
	int para = 0;
	int longlist = 0, suspended = 0;
	int i;
	int buflen = 0;
	int arglen;

	buf[0] = '\0';

	if(parc > para && !strcmp(parv[para], "-long"))
	{
		longlist++;
		para++;
	}

	if(parc > para && !strcmp(parv[para], "-suspended"))
	{
		suspended++;
		para++;
	}

	if(parc > para)
	{
		mask = parv[para];
		para++;
	
		if(parc > para)
			limit = atoi(parv[para]);
	}

	service_send(userserv_p, client_p, conn_p,
			"Username list matching %s, limit %u%s",
			mask, limit,
			suspended ? ", suspended" : "");

	HASH_WALK(i, MAX_NAME_HASH, ptr, user_reg_table)
	{
		ureg_p = ptr->data;

		if(!match(mask, ureg_p->name))
			continue;

		if(suspended)
		{
			if((ureg_p->flags & US_FLAGS_SUSPENDED) == 0)
				continue;
		}
		else if(ureg_p->flags & US_FLAGS_SUSPENDED)
			continue;

		if(!longlist)
		{
			arglen = strlen(ureg_p->name);

			if(buflen + arglen >= USERLIST_LEN)
			{
				service_send(userserv_p, client_p, conn_p,
						"  %s", buf);
				buf[0] = '\0';
				buflen = 0;
			}

			strcat(buf, ureg_p->name);
			strcat(buf, " ");
			buflen += arglen+1;
		}
		else
		{
			static char last_active[] = "Active";
			char timebuf[BUFSIZE];
			const char *p = last_active;

			if(suspended || !dlink_list_length(&ureg_p->users))
			{
				snprintf(timebuf, sizeof(timebuf), "Last %s",
					get_short_duration(CURRENT_TIME - ureg_p->last_time));
				p = timebuf;
			}

			service_send(userserv_p, client_p, conn_p,
				"  %s - Email %s For %s %s",
				ureg_p->name,
				EmptyString(ureg_p->email) ? "<>" : ureg_p->email,
				get_short_duration(CURRENT_TIME - ureg_p->reg_time),
				p);
		}

		if(limit == 1)
		{
			/* two loops to exit here, kludge it */
			i = MAX_NAME_HASH;
			break;
		}

		limit--;
	}
	HASH_WALK_END

	if(!longlist)
		service_send(userserv_p, client_p, conn_p, "  %s", buf);

	service_send(userserv_p, client_p, conn_p,
			"End of username list%s",
			(limit == 1) ? ", limit reached" : "");

	slog(userserv_p, 1, "%s - USERLIST %s",
		OPER_NAME(client_p, conn_p), mask);

	return 0;
}

static int
o_user_userinfo(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg_nick(NULL, parv[0])) == NULL)
	{
		if(parv[0][0] == '=')
			service_send(userserv_p, client_p, conn_p,
					"Nickname %s is not logged in",
					parv[0]);
		else
			service_send(userserv_p, client_p, conn_p,
					"Username %s is not registered", parv[0]);

		return 0;
	}

	slog(userserv_p, 1, "%s - USERINFO %s",
		OPER_NAME(client_p, conn_p), ureg_p->name);

	service_send(userserv_p, client_p, conn_p,
			"[%s] Username registered for %s",
			ureg_p->name,
			get_duration((time_t) (CURRENT_TIME - ureg_p->reg_time)));

	dump_user_info(client_p, conn_p, ureg_p);
	return 0;
}

static int
o_user_usersetpass(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	const char *password;

	if((ureg_p = find_user_reg(NULL, parv[0])) == NULL)
	{
		service_send(userserv_p, client_p, conn_p,
				"Username %s is not registered", parv[0]);
		return 0;
	}

	if(strlen(parv[0]) > PASSWDLEN)
	{
		service_send(userserv_p, client_p, conn_p,
				"Password too long");
		return 0;
	}

	slog(userserv_p, 1, "%s - USERSETPASS %s",
		OPER_NAME(client_p, conn_p), ureg_p->name);

	password = get_crypt(parv[1], NULL);
	my_free(ureg_p->password);
	ureg_p->password = my_strdup(password);

	rsdb_exec(NULL, "UPDATE users SET password='%Q' WHERE username='%Q'", 
			password, ureg_p->name);

	service_send(userserv_p, client_p, conn_p,
			"Username %s password changed", ureg_p->name);
	return 0;
}

static int
valid_email(const char *email)
{
	char *p;

	if(strlen(email) > EMAILLEN)
		return 0;

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
s_user_register(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;
	struct host_entry *hent = NULL;
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

	if(!config_file.uregister_verify && strlen(parv[1]) > PASSWDLEN)
	{
		service_send(userserv_p, client_p, conn_p,
				"Password too long");
		return 0;
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
		hent = find_host(client_p->user->host);

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

		/* dont penalise the individual user just because we cant send emails, so
		 * raise hent lower down..
		 */
	}

	if(config_file.uregister_verify)
	{
		if(config_file.disable_email)
		{
			service_error(userserv_p, client_p,
				"%s::REGISTER is disabled as it cannot send emails",
				userserv_p->name);
			return 1;
		}

		if(!can_send_email())
		{
			service_error(userserv_p, client_p,
				"Temporarily unable to send email, please try later");
			return 1;
		}

		password = get_password();

		if(!send_email(parv[2], "Username registration verification",
				"The username %s has been registered to this email address "
				"by %s!%s@%s\n\n"
				"Your automatically generated password is: %s\n\n"
				"To activate this account you must login with the given "
				"username and password within %s.\n",
				parv[0], client_p->name, client_p->user->username,
				client_p->user->host, password,
				get_short_duration(config_file.uexpire_unverified_time)))
		{
			service_error(userserv_p, client_p,
				"Unable to register username due to problems sending email");
			return 1;
		}

		/* need the crypted version for the database */
		password = get_crypt(password, NULL);
	}
	else
		password = get_crypt(parv[1], NULL);

	if(hent)
		hent->uregister++;

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
	reg_p->password = my_strdup(password);

	if(!EmptyString(parv[2]))
		reg_p->email = my_strdup(parv[2]);

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	if(config_file.uregister_verify)
		reg_p->flags |= US_FLAGS_NEVERLOGGEDIN;

	add_user_reg(reg_p);

	rsdb_exec(NULL, "INSERT INTO users (username, password, email, reg_time, last_time, flags) "
			"VALUES('%Q', '%Q', '%Q', %lu, %lu, %u)",
			reg_p->name, reg_p->password, 
			EmptyString(reg_p->email) ? "" : reg_p->email, 
			reg_p->reg_time, reg_p->last_time, reg_p->flags);

	if(!config_file.uregister_verify)
	{
		dlink_add_alloc(client_p, &reg_p->users);
		client_p->user->user_reg = reg_p;

		sendto_server(":%s ENCAP * SU %s %s", 
				MYNAME, client_p->name, reg_p->name);

		service_error(userserv_p, client_p, "Username %s registered, you are now logged in", parv[0]);

		hook_call(HOOK_USER_LOGIN, client_p, NULL);
	}
	else
		service_error(userserv_p, client_p, 
				"Username %s registered, your password has been emailed",
				parv[0]);

	return 5;
}

static int
s_user_login(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
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

	if(reg_p->flags & US_FLAGS_SUSPENDED)
	{
		service_error(userserv_p, client_p,
			"Login failed, username has been suspended");
		return 1;
	}

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

	if(reg_p->flags & US_FLAGS_NEVERLOGGEDIN)
		reg_p->flags &= ~US_FLAGS_NEVERLOGGEDIN;

	DLINK_FOREACH(ptr, reg_p->users.head)
	{
		service_error(userserv_p, ptr->data,
				"%s has just authenticated as you (%s)",
				client_p->user->mask, reg_p->name);
	}

	sendto_server(":%s ENCAP * SU %s %s",
			MYNAME, client_p->name, reg_p->name);

	client_p->user->user_reg = reg_p;
	reg_p->last_time = CURRENT_TIME;
	reg_p->flags |= US_FLAGS_NEEDUPDATE;
	dlink_add_alloc(client_p, &reg_p->users);
	service_error(userserv_p, client_p, "Login successful");

	hook_call(HOOK_USER_LOGIN, client_p, NULL);

	return 1;
}

static int
s_user_logout(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	dlink_find_destroy(client_p, &client_p->user->user_reg->users);
	client_p->user->user_reg = NULL;
	service_error(userserv_p, client_p, "Logout successful");

	sendto_server(":%s ENCAP * SU %s", MYNAME, client_p->name);

	return 1;
}

static int
s_user_resetpass(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	struct user_reg *reg_p;

	if(config_file.disable_email || !config_file.allow_resetpass)
	{
		service_error(userserv_p, client_p,
			"%s::RESETPASS is disabled", userserv_p->name);
		return 1;
	}

	if(client_p->user->user_reg != NULL)
	{
		service_error(userserv_p, client_p, "You cannot request a password reset whilst logged in");
		return 1;
	}

	if((reg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 1;

	/* initial password reset */
	if(EmptyString(parv[1]))
	{
		const char *token;

		if(EmptyString(reg_p->email))
		{
			service_error(userserv_p, client_p,
					"Username %s does not have an email address set",
					reg_p->name);
			return 1;
		}

		/* XXX - another var? */
		rsdb_exec_fetch(&data, "SELECT COUNT(username) FROM users_resetpass WHERE username='%Q' AND time > '%lu'",
				reg_p->name, CURRENT_TIME - config_file.uresetpass_duration);

		/* already issued one within the past day.. */
		if(atoi(data.row[0][0]))
		{
			service_error(userserv_p, client_p,
					"Username %s already has a pending password reset",
					reg_p->name);
			rsdb_exec_fetch_end(&data);
			return 1;
		}

		rsdb_exec_fetch_end(&data);

		if(!can_send_email())
		{
			service_error(userserv_p, client_p,
				"Temporarily unable to send email, please try later");
			return 1;
		}

		slog(userserv_p, 3, "%s - RESETPASS %s",
			client_p->user->mask, reg_p->name);

		token = get_password();
		rsdb_exec(NULL, "INSERT INTO users_resetpass (username, token, time) VALUES('%Q', '%Q', '%lu')",
				reg_p->name, token, CURRENT_TIME);

		if(!send_email(reg_p->email, "Password reset",
				"%s!%s@%s has requested a password reset for username %s which "
				"is registered to this email address.\n\n"
				"To authenticate this request, send %s RESETPASS %s %s <new_password>\n\n"
				"If you did not request this, simply ignore this message, no "
				"action will be taken on your account and your password will "
				"NOT be reset.\n",
				client_p->name, client_p->user->username, client_p->user->host,
				reg_p->name, userserv_p->name, reg_p->name, token))
		{
			service_error(userserv_p, client_p,
					"Unable to issue password reset due to problems sending email");
		}
		else
		{
			service_error(userserv_p, client_p,
					"Username %s has been sent an email to confirm the password reset",
					reg_p->name);
		}

		
			
		return 2;
	}

	if(EmptyString(parv[2]))
	{
		service_error(userserv_p, client_p,
				"Insufficient parameters to %s::RESETPASS, new password not specified",
				userserv_p->name);
		return 1;
	}

	slog(userserv_p, 3, "%s - RESETPASS %s (auth)",
		client_p->user->mask, reg_p->name);

	/* authenticating a password reset */
	rsdb_exec_fetch(&data, "SELECT token FROM users_resetpass WHERE username='%Q' AND time > '%lu'",
			reg_p->name, CURRENT_TIME - config_file.uresetpass_duration);

	/* ok, found the entry.. */
	if(data.row_count)
	{
		if(strcmp(data.row[0][0], parv[1]) == 0)
		{
			const char *password = get_crypt(parv[2], NULL);

			/* need to execute another query.. */
			rsdb_exec_fetch_end(&data);

			rsdb_exec(NULL, "DELETE FROM users_resetpass WHERE username='%Q'",
					reg_p->name);
			rsdb_exec(NULL, "UPDATE users SET password='%Q' WHERE username='%Q'",
					password, reg_p->name);

			my_free(reg_p->password);
			reg_p->password = strdup(password);

			service_error(userserv_p, client_p,
					"Username %s password reset", reg_p->name);

			return 1;
		}
		else
			service_error(userserv_p, client_p,
					"Username %s password reset tokens do not match",
					reg_p->name);
	}
	else
		service_error(userserv_p, client_p,
				"Username %s does not have a pending password reset",
				reg_p->name);

	rsdb_exec_fetch_end(&data);
	return 1;
}

static int
s_user_set(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
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
				"%s::SET::PASSWORD is disabled", 
				userserv_p->name);
			return 1;
		}

		if(EmptyString(parv[1]) || EmptyString(parv[2]))
		{
			service_error(userserv_p, client_p,
				"Insufficient parameters to %s::SET::PASSWORD",
				userserv_p->name);
			return 1;
		}

		if(strlen(parv[2]) > PASSWDLEN)
		{
			service_send(userserv_p, client_p, conn_p,
					"Password too long");
			return 0;
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

		rsdb_exec(NULL, "UPDATE users SET password='%Q' "
				"WHERE username='%Q'", password, ureg_p->name);

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

		rsdb_exec(NULL, "UPDATE users SET email='%Q' "
				"WHERE username='%Q'", arg, ureg_p->name);

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

		rsdb_exec(NULL, "UPDATE users SET flags=%d "
				"WHERE username='%Q'",
				ureg_p->flags, ureg_p->name);
		return 1;
	}

	service_error(userserv_p, client_p, "Set option invalid");
	return 1;
}

static void
dump_user_info(struct client *client_p, struct lconn *conn_p, struct user_reg *ureg_p)
{
	char buf[BUFSIZE];
	struct member_reg *mreg_p;
#ifdef ENABLE_NICKSERV
	struct nick_reg *nreg_p;
#endif
	struct client *target_p;
	dlink_node *ptr;
	char *p;
	int buflen = 0;
	int mlen;

	p = buf;

	DLINK_FOREACH(ptr, ureg_p->channels.head)
	{
		mreg_p = ptr->data;

		/* "Access to: " + ":200 " */
		if((buflen + strlen(mreg_p->channel_reg->name) + 16) >= (BUFSIZE - 3))
		{
			service_send(userserv_p, client_p, conn_p,
					"[%s] Access to: %s", ureg_p->name, buf);
			p = buf;
			buflen = 0;
		}

		mlen = sprintf(p, "%s:%d ",
				mreg_p->channel_reg->name, mreg_p->level);

		buflen += mlen;
		p += mlen;
	}

	/* could have access to no channels.. */
	if(buflen)
		service_send(userserv_p, client_p, conn_p,
				"[%s] Access to: %s", ureg_p->name, buf);

#ifdef ENABLE_NICKSERV
	p = buf;
	buflen = 0;

	DLINK_FOREACH(ptr, ureg_p->nicks.head)
	{
		nreg_p = ptr->data;

		/* "Registered nicknames: " + " " */
		if((buflen + strlen(nreg_p->name) + 25) >= (BUFSIZE - 3))
		{
			service_send(userserv_p, client_p, conn_p,
					"[%s] Registered nicknames: %s",
					ureg_p->name, buf);
			p = buf;
			buflen = 0;
		}

		mlen = sprintf(p, "%s ", nreg_p->name);
		buflen += mlen;
		p += mlen;
	}

	if(buflen)
		service_send(userserv_p, client_p, conn_p,
				"[%s] Registered nicknames: %s",
				ureg_p->name, buf);
#endif

	if(!EmptyString(ureg_p->email))
		service_send(userserv_p, client_p, conn_p,
				"[%s] Email: %s", 
				ureg_p->name, ureg_p->email);

	service_send(userserv_p, client_p, conn_p,
			"[%s] Currently logged on via:", ureg_p->name);

	DLINK_FOREACH(ptr, ureg_p->users.head)
	{
		target_p = ptr->data;

		service_send(userserv_p, client_p, conn_p,
				"[%s]  %s", ureg_p->name, target_p->user->mask);
	}
}

static int
s_user_info(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg_nick(client_p, parv[0])) == NULL)
		return 1;

	service_error(userserv_p, client_p, 
			"[%s] Username registered for %s",
			ureg_p->name,
			get_duration((time_t) (CURRENT_TIME - ureg_p->reg_time)));

	if(ureg_p == client_p->user->user_reg)
	{
		dump_user_info(client_p, NULL, ureg_p);
		return 3;
	}

	return 1;
}

#endif
