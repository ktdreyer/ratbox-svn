/* src/s_nickserv.c
 *   Contains the code for the nickname services.
 *
 * Copyright (C) 2005 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2005 ircd-ratbox development team
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

#ifdef ENABLE_NICKSERV
#include "service.h"
#include "client.h"
#include "io.h"
#include "rserv.h"
#include "c_init.h"
#include "conf.h"
#include "ucommand.h"
#include "log.h"
#include "s_userserv.h"
#include "s_nickserv.h"
#include "balloc.h"
#include "hook.h"

static struct client *nickserv_p;
static BlockHeap *nick_reg_heap;

static dlink_list nick_reg_table[MAX_NAME_HASH];

static int o_nick_nickdrop(struct client *, struct lconn *, const char **, int);

static int s_nick_register(struct client *, struct lconn *, const char **, int);
static int s_nick_drop(struct client *, struct lconn *, const char **, int);
static int s_nick_release(struct client *, struct lconn *, const char **, int);
static int s_nick_regain(struct client *, struct lconn *, const char **, int);
static int s_nick_set(struct client *, struct lconn *, const char **, int);
static int s_nick_info(struct client *, struct lconn *, const char **, int);

static int h_nick_warn_client(void *target_p, void *unused);
static int h_nick_server_eob(void *client_p, void *unused);

static struct service_command nickserv_command[] =
{
	{ "NICKDROP",	&o_nick_nickdrop, 1, NULL, 1, 0L, 0, 0, CONF_OPER_NS_DROP, 0 },
	{ "REGISTER",	&s_nick_register, 0, NULL, 1, 0L, 1, 0, 0, 0	},
	{ "DROP",	&s_nick_drop,     1, NULL, 1, 0L, 1, 0, 0, 0	},
	{ "RELEASE",	&s_nick_release,  1, NULL, 1, 0L, 1, 0, 0, 0	},
	{ "REGAIN",	&s_nick_regain,   1, NULL, 1, 0L, 1, 0, 0, 0	},
	{ "SET",	&s_nick_set,	  2, NULL, 1, 0L, 1, 0, 0, 0	},
	{ "INFO",	&s_nick_info,     1, NULL, 1, 0L, 1, 0, 0, 0	}
};

static struct ucommand_handler nickserv_ucommand[] =
{
	{ "nickdrop", o_nick_nickdrop, 0, CONF_OPER_NS_DROP, 1, 1, NULL },
	{ "\0", NULL, 0, 0, 0, 0, NULL }
};

static struct service_handler nick_service = {
	"NICKSERV", "NICKSERV", "nickserv", "services.int",
	"Nickname Registration Service", 60, 80, 
	nickserv_command, sizeof(nickserv_command), nickserv_ucommand, NULL
};

static int nick_db_callback(void *, int, char **, char **);

void
init_s_nickserv(void)
{
	nickserv_p = add_service(&nick_service);

	nick_reg_heap = BlockHeapCreate(sizeof(struct nick_reg), HEAP_NICK_REG);

	loc_sqlite_exec(nick_db_callback, "SELECT * FROM nicks");

	hook_add(h_nick_warn_client, HOOK_NEW_CLIENT);
	hook_add(h_nick_warn_client, HOOK_NICKCHANGE);
	hook_add(h_nick_server_eob, HOOK_SERVER_EOB);
}

static void
add_nick_reg(struct nick_reg *nreg_p)
{
	unsigned int hashv = hash_name(nreg_p->name);
	dlink_add(nreg_p, &nreg_p->node, &nick_reg_table[hashv]);
}

void
free_nick_reg(struct nick_reg *nreg_p)
{
	unsigned int hashv = hash_name(nreg_p->name);

	loc_sqlite_exec(NULL, "DELETE FROM nicks WHERE nickname = %Q",
			nreg_p->name);

	dlink_delete(&nreg_p->node, &nick_reg_table[hashv]);
	dlink_delete(&nreg_p->usernode, &nreg_p->user_reg->nicks);
	BlockHeapFree(nick_reg_heap, nreg_p);
}

static struct nick_reg *
find_nick_reg(struct client *client_p, const char *name)
{
	struct nick_reg *nreg_p;
	dlink_node *ptr;
	unsigned int hashv = hash_name(name);

	DLINK_FOREACH(ptr, nick_reg_table[hashv].head)
	{
		nreg_p = ptr->data;
		if(!irccmp(nreg_p->name, name))
			return nreg_p;
	}

	if(client_p)
		service_error(nickserv_p, client_p, "Nickname %s is not registered",
				name);

	return NULL;
}

static int
nick_db_callback(void *db, int argc, char **argv, char **colnames)
{
	struct nick_reg *nreg_p;
	struct user_reg *ureg_p;

	if(argc < 4)
		return 0;

	if(EmptyString(argv[0]) || EmptyString(argv[1]))
		return 0;

	if((ureg_p = find_user_reg(NULL, argv[1])) == NULL)
		return 0;

	nreg_p = BlockHeapAlloc(nick_reg_heap);
	strlcpy(nreg_p->name, argv[0], sizeof(nreg_p->name));
	nreg_p->reg_time = atol(argv[2]);
	nreg_p->last_time = atol(argv[3]);
	nreg_p->flags = atol(argv[4]);

	add_nick_reg(nreg_p);
	dlink_add(nreg_p, &nreg_p->usernode, &ureg_p->nicks);
	nreg_p->user_reg = ureg_p;

	return 0;
}

static int
h_nick_warn_client(void *vclient_p, void *unused)
{
	struct nick_reg *nreg_p;
	struct client *client_p = vclient_p;

	if(!config_file.nallow_set_warn || EmptyString(config_file.nwarn_string))
		return 0;

	if((nreg_p = find_nick_reg(NULL, client_p->name)) == NULL)
		return 0;

	if((nreg_p->flags & NS_FLAGS_WARN) == 0)
		return 0;

	/* here for nick change */
	if(nreg_p->user_reg == client_p->user->user_reg)
		return 0;

	service_error(nickserv_p, client_p, "%s", config_file.nwarn_string);
	return 0;
}

static int
h_nick_server_eob(void *vclient_p, void *unused)
{
	struct nick_reg *nreg_p;
	struct client *client_p = vclient_p;
	struct client *target_p;
	dlink_node *ptr;

	if(!config_file.nallow_set_warn || EmptyString(config_file.nwarn_string))
		return 0;

	DLINK_FOREACH(ptr, client_p->server->users.head)
	{
		target_p = ptr->data;

		if((nreg_p = find_nick_reg(NULL, target_p->name)) == NULL)
			continue;

		if((nreg_p->flags & NS_FLAGS_WARN) == 0)
			continue;

		if(nreg_p->user_reg == target_p->user->user_reg)
			continue;

		service_error(nickserv_p, target_p, "%s", config_file.nwarn_string);
	}

	return 0;
}

static int
o_nick_nickdrop(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;

	if((nreg_p = find_nick_reg(NULL, parv[0])) == NULL)
	{
		service_send(nickserv_p, client_p, conn_p,
				"Nickname %s is not registered", parv[0]);
		return 0;
	}

	service_error(nickserv_p, client_p, "Nickname %s dropped", parv[0]);

	slog(nickserv_p, 1, "%s - NICKDROP %s",
		OPER_NAME(client_p, conn_p), nreg_p->name);

	free_nick_reg(nreg_p);
	return 0;
}

static int
s_nick_register(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;
	struct user_reg *ureg_p = client_p->user->user_reg;

	if(dlink_list_length(&ureg_p->nicks) >= config_file.nmax_nicks)
	{
		service_error(nickserv_p, client_p,
				"You have already registered %d nicknames",
				config_file.nmax_nicks);
		return 1;
	}

	if(find_nick_reg(NULL, client_p->name))
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is already registered",
				client_p->name);
		return 1;
	}

	slog(nickserv_p, 2, "%s %s REGISTER %s",
		client_p->user->mask, client_p->user->user_reg->name,
		client_p->name);

	nreg_p = BlockHeapAlloc(nick_reg_heap);

	strlcpy(nreg_p->name, client_p->name, sizeof(nreg_p->name));
	nreg_p->reg_time = nreg_p->last_time = CURRENT_TIME;

	if(config_file.nallow_set_warn)
		nreg_p->flags |= NS_FLAGS_WARN;

	add_nick_reg(nreg_p);
	dlink_add(nreg_p, &nreg_p->usernode, &ureg_p->nicks);
	nreg_p->user_reg = ureg_p;

	loc_sqlite_exec(NULL, 
			"INSERT INTO nicks (nickname, username, reg_time, last_time, flags) "
			"VALUES(%Q, %Q, %lu, %lu, %u)",
			nreg_p->name, ureg_p->name, nreg_p->reg_time, 
			nreg_p->last_time, nreg_p->flags);

	service_error(nickserv_p, client_p, "Nickname registered");
	return 1;
}

static int
s_nick_drop(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	if(nreg_p->user_reg != client_p->user->user_reg)
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is not registered to you",
				nreg_p->name);
		return 1;
	}

	service_error(nickserv_p, client_p, "Nickname %s dropped", parv[0]);

	slog(nickserv_p, 3, "%s %s DROP %s",
		client_p->user->mask, client_p->user->user_reg->name, parv[0]);

	free_nick_reg(nreg_p);
	return 1;
}

static int
s_nick_release(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;
	struct client *target_p;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	if(nreg_p->user_reg != client_p->user->user_reg)
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is not registered to you",
				nreg_p->name);
		return 1;
	}

	if((target_p = find_user(parv[0])) == NULL)
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is not online", nreg_p->name);
		return 1;
	}

	if(target_p == client_p)
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is already in use by you",
				nreg_p->name);
		return 1;
	}

	sendto_server("KILL %s :%s (%s: RELEASE by %s)",
			target_p->name, MYNAME, 
			nickserv_p->name, client_p->name);
	exit_client(target_p);

	slog(nickserv_p, 4, "%s %s RELEASE %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	return 1;
}

static int
s_nick_regain(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;
	struct client *target_p;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	if(nreg_p->user_reg != client_p->user->user_reg)
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is not registered to you",
				nreg_p->name);
		return 1;
	}

	if((target_p = find_user(parv[0])) == NULL)
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is not online", nreg_p->name);
		return 1;
	}

	if(target_p == client_p)
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is already in use by you",
				nreg_p->name);
		return 1;
	}

	if((client_p->uplink->flags & FLAGS_RSFNC) == 0)
	{
		service_error(nickserv_p, client_p,
				"%s::REGAIN is not supported by your server",
				nickserv_p->name);
		return 1;
	}

	sendto_server("KILL %s :%s (%s: REGAIN by %s)",
			target_p->name, MYNAME, 
			nickserv_p->name, client_p->name);

	/* send out a forced nick change for the client to their new
	 * nickname, at a TS of 60 seconds ago to prevent collisions.
	 */
	sendto_server("ENCAP %s RSFNC %s %s %lu %lu",
			client_p->user->servername, client_p->name,
			nreg_p->name, CURRENT_TIME - 60,
			client_p->user->tsinfo);

	exit_client(target_p);

	slog(nickserv_p, 4, "%s %s REGAIN %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	return 1;
}


static int
s_nick_set_flag(struct client *client_p, struct nick_reg *nreg_p,
		const char *name, const char *arg, int flag)
{
	if(!strcasecmp(arg, "ON"))
	{
		service_error(nickserv_p, client_p,
			"Nickname %s %s set ON", nreg_p->name, name);

		if(nreg_p->flags & flag)
			return 0;

		nreg_p->flags |= flag;

		loc_sqlite_exec(NULL, "UPDATE nicks SET flags=%d "
				"WHERE nickname=%Q",
				nreg_p->flags, nreg_p->name);

		return 1;
	}
	else if(!strcasecmp(arg, "OFF"))
	{
		service_error(nickserv_p, client_p,
			"Nickname %s %s set OFF", nreg_p->name, name);

		if((nreg_p->flags & flag) == 0)
			return 0;

		nreg_p->flags &= ~flag;

		loc_sqlite_exec(NULL, "UPDATE nicks SET flags=%d "
				"WHERE nickname=%Q",
				nreg_p->flags, nreg_p->name);

		return -1;
	}

	service_error(nickserv_p, client_p,
			"Nickname %s %s is %s",
			nreg_p->name, name, (nreg_p->flags & flag) ? "ON" : "OFF");
	return 0;
}

static int
s_nick_set(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static const char dummy[] = "\0";
	struct nick_reg *nreg_p;
	const char *arg;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	if(nreg_p->user_reg != client_p->user->user_reg)
	{
		service_error(nickserv_p, client_p,
				"Nickname %s is not registered to you",
				nreg_p->name);
		return 1;
	}

	arg = EmptyString(parv[2]) ? dummy : parv[2];

	if(!strcasecmp(parv[1], "WARN"))
	{
		if(!config_file.nallow_set_warn)
		{
			service_error(nickserv_p, client_p,
					"%s::SET::WARN is disabled",
					nickserv_p->name);
			return 1;
		}

		s_nick_set_flag(client_p, nreg_p, parv[1], arg, NS_FLAGS_WARN);
		return 1;
	}

	service_error(nickserv_p, client_p, "Set option invalid");
	return 1;
}	

static int
s_nick_info(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	service_error(nickserv_p, client_p,
			"[%s] Registered to %s",
			nreg_p->name, nreg_p->user_reg->name);

	service_error(nickserv_p, client_p,
			"[%s] Registered for %s",
			nreg_p->name, 
			get_duration((time_t) (CURRENT_TIME - nreg_p->reg_time)));

	slog(nickserv_p, 5, "%s %s INFO %s",
		client_p->user->mask, client_p->user->user_reg->name, parv[0]);

	return 1;
}

#endif
