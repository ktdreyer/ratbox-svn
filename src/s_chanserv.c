/* src/s_chanserv.c
 *   Contains code for channel registration service.
 *
 * Copyright (C) 2004-2006 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004-2006 ircd-ratbox development team
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

#include "rsdb.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "rserv.h"
#include "ucommand.h"
#include "log.h"
#include "io.h"
#include "s_userserv.h"
#include "s_chanserv.h"
#include "c_init.h"
#include "balloc.h"
#include "conf.h"
#include "modebuild.h"
#include "hook.h"
#include "event.h"
#include "watch.h"

#define S_C_OWNER	200
#define S_C_MANAGER	190
#define S_C_USERLIST	150
#define S_C_CLEAR	140
#define S_C_SUSPEND	100
#define S_C_OP		50
#define S_C_REGULAR	10
#define S_C_USER	1

/* Note: There is NO automatic changing of DB structures to deal with the
 * size of this value changing.
 *
 * It is modified at your own risk.
 */
#define REASON_MAGIC	50

static void init_s_chanserv(void);

static struct client *chanserv_p;
static BlockHeap *channel_reg_heap;
static BlockHeap *member_reg_heap;
static BlockHeap *ban_reg_heap;

static dlink_list chan_reg_table[MAX_CHANNEL_TABLE];

static int o_chan_chanregister(struct client *, struct lconn *, const char **, int);
static int o_chan_chandrop(struct client *, struct lconn *, const char **, int);
static int o_chan_chansuspend(struct client *, struct lconn *, const char **, int);
static int o_chan_chanunsuspend(struct client *, struct lconn *, const char **, int);
static int o_chan_chanlist(struct client *, struct lconn *, const char **, int);
static int o_chan_chaninfo(struct client *, struct lconn *, const char **, int);

static int s_chan_register(struct client *, struct lconn *, const char **, int);
static int s_chan_set(struct client *, struct lconn *, const char **, int);
static int s_chan_adduser(struct client *, struct lconn *, const char **, int);
static int s_chan_deluser(struct client *, struct lconn *, const char **, int);
static int s_chan_moduser(struct client *, struct lconn *, const char **, int);
static int s_chan_modauto(struct client *, struct lconn *, const char **, int);
static int s_chan_listusers(struct client *, struct lconn *, const char **, int);
static int s_chan_suspend(struct client *, struct lconn *, const char **, int);
static int s_chan_unsuspend(struct client *, struct lconn *, const char **, int);
static int s_chan_clearmodes(struct client *, struct lconn *, const char **, int);
static int s_chan_clearops(struct client *, struct lconn *, const char **, int);
static int s_chan_clearallops(struct client *, struct lconn *, const char **, int);
static int s_chan_clearbans(struct client *, struct lconn *, const char **, int);
static int s_chan_invite(struct client *, struct lconn *, const char **, int);
static int s_chan_getkey(struct client *, struct lconn *, const char **, int);
static int s_chan_op(struct client *, struct lconn *, const char **, int);
static int s_chan_voice(struct client *, struct lconn *, const char **, int);
static int s_chan_addban(struct client *, struct lconn *, const char **, int);
static int s_chan_delban(struct client *, struct lconn *, const char **, int);
static int s_chan_modban(struct client *, struct lconn *, const char **, int);
static int s_chan_listbans(struct client *, struct lconn *, const char **, int);
static int s_chan_unban(struct client *, struct lconn *, const char **, int);
static int s_chan_info(struct client *, struct lconn *, const char **, int);

static struct service_command chanserv_command[] =
{
	{ "CHANREGISTER",	&o_chan_chanregister,	2, NULL, 1, 0L, 0, 0, CONF_OPER_CS_REGISTER, 0 },
	{ "CHANDROP",		&o_chan_chandrop,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CS_DROP, 0 },
	{ "CHANSUSPEND",	&o_chan_chansuspend,	2, NULL, 1, 0L, 0, 0, CONF_OPER_CS_SUSPEND, 0 },
	{ "CHANUNSUSPEND",	&o_chan_chanunsuspend,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CS_SUSPEND, 0 },
	{ "CHANLIST",		&o_chan_chanlist,	0, NULL, 1, 0L, 0, 0, CONF_OPER_CS_LIST, 0 },
	{ "CHANINFO",		&o_chan_chaninfo,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CS_INFO, 0 },
	{ "REGISTER",	&s_chan_register,	1, NULL, 1, 0L, 1, 0, 0, UMODE_REGISTER	},
	{ "SET",	&s_chan_set,		2, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "ADDUSER",	&s_chan_adduser,	3, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "DELUSER",	&s_chan_deluser,	2, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "MODUSER",	&s_chan_moduser,	3, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "MODAUTO",	&s_chan_modauto,	3, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "LISTUSERS",	&s_chan_listusers,	1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "SUSPEND",	&s_chan_suspend,	3, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "UNSUSPEND",	&s_chan_unsuspend,	2, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "CLEARMODES",	&s_chan_clearmodes,	1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "CLEAROPS",	&s_chan_clearops,	1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "CLEARALLOPS",&s_chan_clearallops,	1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "CLEARBANS",	&s_chan_clearbans,	1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "INVITE",	&s_chan_invite,		1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "GETKEY",	&s_chan_getkey,		1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "OP",		&s_chan_op,		1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "VOICE",	&s_chan_voice,		1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "ADDBAN",	&s_chan_addban,		4, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "DELBAN",	&s_chan_delban,		2, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "MODBAN",	&s_chan_modban,		3, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "LISTBANS",	&s_chan_listbans,	1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "UNBAN",	&s_chan_unban,		1, NULL, 1, 0L, 1, 0, 0, 0 },
	{ "INFO",	&s_chan_info,		1, NULL, 1, 0L, 0, 0, 0, 0 }
};

static struct ucommand_handler chanserv_ucommand[] =
{
	{ "chanregister",	o_chan_chanregister,	0, CONF_OPER_CS_REGISTER,	2, NULL },
	{ "chandrop",		o_chan_chandrop,	0, CONF_OPER_CS_DROP,		1, NULL },
	{ "chansuspend",	o_chan_chansuspend,	0, CONF_OPER_CS_SUSPEND,	2, NULL },
	{ "chanunsuspend",	o_chan_chanunsuspend,	0, CONF_OPER_CS_SUSPEND,	1, NULL },
	{ "chanlist",		o_chan_chanlist,	0, CONF_OPER_CS_LIST,		0, NULL },
	{ "chaninfo",		o_chan_chaninfo,	0, CONF_OPER_CS_INFO,		1, NULL },
	{ "\0",			NULL,			0, 0, 0, NULL }
};

static struct service_handler chanserv_service = {
	"CHANSERV", "CHANSERV", "chanserv", "services.int", "Channel Service",
	30, 50, chanserv_command, sizeof(chanserv_command), chanserv_ucommand, init_s_chanserv, NULL
};

static void load_channel_db(void);
static void free_ban_reg(struct chan_reg *chreg_p, struct ban_reg *banreg_p);

static int h_chanserv_join(void *members, void *unused);
static int h_chanserv_mode_op(void *chptr, void *members);
static int h_chanserv_mode_voice(void *chptr, void *members);
static int h_chanserv_mode_simple(void *chptr, void *unused);
static int h_chanserv_sjoin_lowerts(void *chptr, void *unused);
static int h_chanserv_user_login(void *client, void *unused);
static int h_chanserv_dbsync(void *unused, void *unusedd);
static void e_chanserv_updatechan(void *unused);
static void e_chanserv_expirechan(void *unused);
static void e_chanserv_expireban(void *unused);
static void e_chanserv_enforcetopic(void *unused);

static void dump_info_extended(struct client *, struct lconn *, struct chan_reg *);
static void dump_info_accesslist(struct client *, struct lconn *, struct chan_reg *);

void
preinit_s_chanserv(void)
{
	chanserv_p = add_service(&chanserv_service);
}

static void
init_s_chanserv(void)
{
	channel_reg_heap = BlockHeapCreate(sizeof(struct chan_reg), HEAP_CHANNEL_REG);
	member_reg_heap = BlockHeapCreate(sizeof(struct member_reg), HEAP_MEMBER_REG);
	ban_reg_heap = BlockHeapCreate(sizeof(struct ban_reg), HEAP_BAN_REG);

	load_channel_db();

	hook_add(h_chanserv_join, HOOK_JOIN_CHANNEL);
	hook_add(h_chanserv_mode_op, HOOK_MODE_OP);
	hook_add(h_chanserv_mode_voice, HOOK_MODE_VOICE);
	hook_add(h_chanserv_mode_simple, HOOK_MODE_SIMPLE);
	hook_add(h_chanserv_sjoin_lowerts, HOOK_SJOIN_LOWERTS);
	hook_add(h_chanserv_user_login, HOOK_USER_LOGIN);
	hook_add(h_chanserv_dbsync, HOOK_DBSYNC);

	eventAdd("chanserv_updatechan", e_chanserv_updatechan, NULL, 3601);
	eventAdd("chanserv_expirechan", e_chanserv_expirechan, NULL, 43205);

	/* we add these with defaults, then update the timers when we parse
	 * the conf..
	 */
	eventAdd("chanserv_expireban", e_chanserv_expireban, NULL,
		DEFAULT_EXPIREBAN_FREQUENCY);
	eventAdd("chanserv_enforcetopic", e_chanserv_enforcetopic, NULL,
		DEFAULT_ENFORCETOPIC_FREQUENCY);
}

void
free_channel_reg(struct chan_reg *reg_p)
{
	dlink_node *ptr, *next_ptr;

	unsigned int hashv = hash_channel(reg_p->name);

	part_service(chanserv_p, reg_p->name);

	rsdb_exec(NULL, "DELETE FROM bans WHERE chname = '%Q'",
			reg_p->name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, reg_p->bans.head)
	{
		free_ban_reg(reg_p, ptr->data);
	}

	dlink_delete(&reg_p->node, &chan_reg_table[hashv]);

	rsdb_exec(NULL, "DELETE FROM channels WHERE chname = '%Q'",
			reg_p->name);

	my_free(reg_p->name);
	my_free(reg_p->topic);

	BlockHeapFree(channel_reg_heap, reg_p);
}

static void
destroy_channel_reg(struct chan_reg *reg_p)
{
	dlink_node *ptr, *next_ptr;

	rsdb_exec(NULL, "DELETE FROM members WHERE chname = '%Q'",
			reg_p->name);

	/* free_member_reg() will call free_channel_reg() when its done */
	DLINK_FOREACH_SAFE(ptr, next_ptr, reg_p->users.head)
	{
		free_member_reg(ptr->data, 0);
	}
}

static void
add_channel_reg(struct chan_reg *reg_p)
{
	unsigned int hashv = hash_channel(reg_p->name);
	reg_p->bants = 1L; /* initially allow UNBAN */
	dlink_add(reg_p, &reg_p->node, &chan_reg_table[hashv]);
}

static void
init_channel_reg(struct chan_reg *chreg_p, const char *name)
{
	struct channel *chptr;

	chptr = find_channel(name);

	chreg_p->name = my_strdup(name);
	chreg_p->tsinfo = chptr != NULL ? chptr->tsinfo : CURRENT_TIME;
	chreg_p->reg_time = chreg_p->last_time = CURRENT_TIME;
	chreg_p->cmode.mode = MODE_TOPIC|MODE_NOEXTERNAL;

	add_channel_reg(chreg_p);

	rsdb_exec(NULL, 
		"INSERT INTO channels "
		"(chname, tsinfo, reg_time, last_time, flags, createmodes) "
		"VALUES('%Q', %lu, %lu, %lu, 0, '%Q')",
		chreg_p->name, chreg_p->tsinfo, chreg_p->reg_time,
		chreg_p->last_time, "+nt");
}

static struct chan_reg *
find_channel_reg(struct client *client_p, const char *name)
{
	struct chan_reg *reg_p;
	unsigned int hashv = hash_channel(name);
	dlink_node *ptr;

	DLINK_FOREACH(ptr, chan_reg_table[hashv].head)
	{
		reg_p = ptr->data;

		if(!irccmp(reg_p->name, name))
			return reg_p;
	}

	if(client_p != NULL)
		service_error(chanserv_p, client_p, "Channel %s is not registered",
				name);

	return NULL;
}

static struct member_reg *
make_member_reg(struct user_reg *ureg_p, struct chan_reg *chreg_p,
		const char *lastmod, int level, int flags)
{
	struct member_reg *mreg_p = BlockHeapAlloc(member_reg_heap);

	mreg_p->user_reg = ureg_p;
	mreg_p->channel_reg = chreg_p;
	mreg_p->level = level;
	mreg_p->flags = flags;
	mreg_p->lastmod = my_strdup(lastmod);

	dlink_add(mreg_p, &mreg_p->usernode, &ureg_p->channels);
	dlink_add(mreg_p, &mreg_p->channode, &chreg_p->users);

	return mreg_p;
}

void
free_member_reg(struct member_reg *mreg_p, int upgrade)
{
	struct chan_reg *chreg_p = mreg_p->channel_reg;
	int level = mreg_p->level;

	/* remove the user before we find highest */
	dlink_delete(&mreg_p->usernode, &mreg_p->user_reg->channels);
	dlink_delete(&mreg_p->channode, &mreg_p->channel_reg->users);

	my_free(mreg_p->lastmod);
	BlockHeapFree(member_reg_heap, mreg_p);

	if(!dlink_list_length(&chreg_p->users))
	{
		free_channel_reg(chreg_p);
	}
	/* upgrade the highest user to owner */
	else if(level == S_C_OWNER && upgrade)
	{
		struct member_reg *mreg_tp;
		struct member_reg *mreg_top = NULL;
		struct member_reg *mreg_topsus = NULL;
		dlink_node *ptr;

		level = 0;

		DLINK_FOREACH(ptr, chreg_p->users.head)
		{
			mreg_tp = ptr->data;

			if(mreg_tp->suspend)
			{
				if(!mreg_topsus || mreg_tp->level > mreg_topsus->level)
					mreg_topsus = mreg_tp;
			}
			else if(!mreg_top || mreg_tp->level > mreg_top->level)
				mreg_top = mreg_tp;
		}

		if(mreg_topsus && !mreg_top)
		{
			mreg_top = mreg_topsus;
			mreg_top->suspend = 0;
		}

		/* now promote the highest user */
		mreg_top->level = S_C_OWNER;
		mreg_top->lastmod = my_strdup(MYNAME);

		rsdb_exec(NULL, 
				"UPDATE members SET level = %d, suspend = 0, lastmod = '%Q' "
				"WHERE chname = '%Q' AND username = '%Q'",
				mreg_top->level, MYNAME, chreg_p->name, 
				mreg_top->user_reg->name);
	}
}

static struct member_reg *
find_member_reg(struct user_reg *ureg_p, struct chan_reg *chreg_p)
{
	struct member_reg *mreg_p;
	dlink_node *ptr;

	if(ureg_p == NULL)
		return NULL;

	DLINK_FOREACH(ptr, ureg_p->channels.head)
	{
		mreg_p = ptr->data;

		if(mreg_p->channel_reg == chreg_p)
			return mreg_p;
	}

	return NULL;
}

#if 0
static struct member_reg *
find_member_reg_name(struct client *client_p, struct user_reg *ureg_p, const char *name)
{
	struct chan_reg *chreg_p;

	if((chreg_p = find_channel_reg(client_p, name)) == NULL)
		return NULL;

	return find_member_reg(ureg_p, chreg_p);
}
#endif

static struct member_reg *
verify_member_reg(struct client *client_p, struct channel **chptr, 
		struct chan_reg *chreg_p, int level)
{
	struct member_reg *mreg_p;

	if(chptr && (*chptr = find_channel(chreg_p->name)) == NULL)
	{
		service_error(chanserv_p, client_p, "Channel %s does not exist",
				chreg_p->name);
		return NULL;
	}

	if(chreg_p->flags & CS_FLAGS_SUSPENDED)
	{
		service_error(chanserv_p, client_p, "Channel %s is suspended",
				chreg_p->name);
		return NULL;
	}

	if((mreg_p = find_member_reg(client_p->user->user_reg, chreg_p)) == NULL ||
	   mreg_p->level < level || mreg_p->suspend)
	{
		service_error(chanserv_p, client_p, "Insufficient access on %s",
				chreg_p->name);
		return NULL;
	}

	/* this is called when someone issues a command.. */
	mreg_p->channel_reg->last_time = CURRENT_TIME;
	mreg_p->channel_reg->flags |= CS_FLAGS_NEEDUPDATE;

	return mreg_p;
}

static struct member_reg *
verify_member_reg_name(struct client *client_p, struct channel **chptr,
			const char *name, int level)
{
	struct chan_reg *chreg_p;

	if((chreg_p = find_channel_reg(client_p, name)) == NULL)
		return NULL;

	return verify_member_reg(client_p, chptr, chreg_p, level);
}

static int
channel_db_callback(int argc, const char **argv)
{
	struct chan_reg *reg_p;
	struct chmode mode;
	char *modev[MAXPARA + 1];
	char *tmpmode;
	int modec;

	if(EmptyString(argv[0]))
		return 0;

	reg_p = BlockHeapAlloc(channel_reg_heap);
	reg_p->name = my_strdup(argv[0]);

	if(!EmptyString(argv[1]))
		reg_p->topic = my_strdup(argv[1]);

	if(!EmptyString(argv[2]))
		reg_p->url = my_strdup(argv[2]);

	if(!EmptyString(argv[3]))
	{
		memset(&mode, 0, sizeof(struct chmode));
		tmpmode = LOCAL_COPY(argv[3]);
		modec = string_to_array(tmpmode, modev);

		if(parse_simple_mode(&mode, (const char **) modev, modec, 0))
		{
			reg_p->cmode.mode = mode.mode;
			reg_p->cmode.limit = mode.limit;

			if(mode.key[0])
				strlcpy(reg_p->cmode.key, mode.key,
					sizeof(reg_p->cmode.key));
		}
	}

	if(!EmptyString(argv[4]))
	{
		memset(&mode, 0, sizeof(struct chmode));
		tmpmode = LOCAL_COPY(argv[4]);
		modec = string_to_array(tmpmode, modev);

		if(parse_simple_mode(&mode, (const char **) modev, modec, 0))
		{
			reg_p->emode.mode = mode.mode;
			reg_p->emode.limit = mode.limit;

			if(mode.key[0])
				strlcpy(reg_p->emode.key, mode.key,
					sizeof(reg_p->emode.key));
		}
	}

	reg_p->tsinfo = atol(argv[5]);
	reg_p->reg_time = atol(argv[6]);
	reg_p->last_time = atol(argv[7]);
	reg_p->flags = atoi(argv[8]);

	if(!EmptyString(argv[9]))
		reg_p->suspender = my_strdup(argv[9]);

	/* note: suspend_reason may be blank even when a channel is
	 * suspended, as they were introduced in rserv-1.1
	 */
	if(!EmptyString(argv[10]))
		reg_p->suspend_reason = my_strdup(argv[10]);

	add_channel_reg(reg_p);

	if(reg_p->flags & CS_FLAGS_AUTOJOIN)
		join_service(chanserv_p, reg_p->name, reg_p->tsinfo,
			reg_p->emode.mode ? &reg_p->emode : &reg_p->cmode);

	return 0;
}

static int
member_db_callback(int argc, const char **argv)
{
	struct chan_reg *chreg_p;
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;

	if(EmptyString(argv[0]) || EmptyString(argv[1]))
		return 0;

	if((chreg_p = find_channel_reg(NULL, argv[0])) == NULL ||
	   (ureg_p = find_user_reg(NULL, argv[1])) == NULL)
		return 0;

	mreg_p = make_member_reg(ureg_p, chreg_p, argv[2], atoi(argv[3]),
			atoi(argv[4]) & CS_MEMBER_ALL);
	mreg_p->suspend = atoi(argv[5]);
	return 0;
}

static struct ban_reg *
make_ban_reg(struct chan_reg *chreg_p, const char *mask, const char *reason,
              const char *username, int level, int hold)
{
	struct ban_reg *banreg_p = BlockHeapAlloc(ban_reg_heap);

	banreg_p->mask = my_strdup(mask);
	banreg_p->reason = my_strdup(EmptyString(reason) ? "No Reason" : reason);
	banreg_p->username = my_strdup(EmptyString(username) ? "unknown" : username);
	banreg_p->level = level;
	banreg_p->hold = hold;

	collapse(banreg_p->mask);

	dlink_add(banreg_p, &banreg_p->channode, &chreg_p->bans);
	return banreg_p;
}

static void
free_ban_reg(struct chan_reg *chreg_p, struct ban_reg *banreg_p)
{
	dlink_delete(&banreg_p->channode, &chreg_p->bans);

	my_free(banreg_p->mask);
	my_free(banreg_p->reason);
	my_free(banreg_p->username);

	BlockHeapFree(ban_reg_heap, banreg_p);
}

static struct ban_reg *
find_ban_reg(struct chan_reg *chreg_p, const char *mask)
{
	struct ban_reg *banreg_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, chreg_p->bans.head)
	{
		banreg_p = ptr->data;

		if(!irccmp(banreg_p->mask, mask))
			return banreg_p;
	}

	return NULL;
}

static int
ban_db_callback(int argc, const char **argv)
{
	struct chan_reg *chreg_p;
	struct ban_reg *banreg_p;
	int level, hold;

	if(EmptyString(argv[0]) || EmptyString(argv[1]))
		return 0;

	if((chreg_p = find_channel_reg(NULL, argv[0])) == NULL)
		return 0;

	level = atoi(argv[4]);
	hold = atoi(argv[5]);
	banreg_p = make_ban_reg(chreg_p, argv[1], argv[2], argv[3], level, hold);

	return 0;
}

static void
load_channel_db(void)
{
	rsdb_exec(channel_db_callback, 
			"SELECT chname, topic, url, createmodes, "
			"enforcemodes, tsinfo, reg_time, last_time, "
			"flags, suspender, suspend_reason FROM channels");
	rsdb_exec(member_db_callback, 
			"SELECT chname, username, lastmod, level, "
			"flags, suspend FROM members");
	rsdb_exec(ban_db_callback, 
			"SELECT chname, mask, reason, username, "
			"level, hold FROM bans");
}

static void
update_chreg_flags(struct chan_reg *chreg_p)
{
	rsdb_exec(NULL, "UPDATE channels SET flags = %d "
			"WHERE chname = '%Q'",
			chreg_p->flags, chreg_p->name);
}

static void
enable_autojoin(struct chan_reg *chreg_p)
{
	struct channel *chptr;

	chreg_p->flags |= CS_FLAGS_AUTOJOIN;

	/* Store the TS, so we will join with the correct
	 * TS in future -- jilles
	 */
	chptr = find_channel(chreg_p->name);

	if(chptr != NULL && chptr->tsinfo != chreg_p->tsinfo)
	{
		chreg_p->flags |= CS_FLAGS_NEEDUPDATE;
		chreg_p->tsinfo = chptr->tsinfo;
	}

	/* Join with stored TS */
	join_service(chanserv_p, chreg_p->name, chreg_p->tsinfo,
			chreg_p->emode.mode ? &chreg_p->emode :
			&chreg_p->cmode);
}

static void
write_member_db_entry(struct member_reg *reg_p)
{
	rsdb_exec(NULL, "INSERT INTO members (chname, username, lastmod, level, flags, suspend) "
			      " VALUES('%Q', '%Q', '%Q', %u, %d, 0)",
			reg_p->channel_reg->name,
			reg_p->user_reg->name, reg_p->lastmod, reg_p->level,
			reg_p->flags);
}

static void
delete_member_db_entry(struct member_reg *reg_p)
{
	rsdb_exec(NULL, "DELETE FROM members WHERE chname = '%Q' AND username = '%Q'",
			reg_p->channel_reg->name, reg_p->user_reg->name);
}

static void
write_ban_db_entry(struct ban_reg *reg_p, const char *chname)
{
	rsdb_exec(NULL, "INSERT INTO bans (chname, mask, reason, username, level, hold) " 
			      "VALUES('%Q', '%Q', '%Q', '%Q', %d, %lu)",
			chname, reg_p->mask, reg_p->reason, reg_p->username,
			reg_p->level, reg_p->hold);
}

static const char *
find_owner(struct chan_reg *chreg_p)
{
	struct member_reg *mreg_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, chreg_p->users.head)
	{
		mreg_p = ptr->data;

		if(mreg_p->level == S_C_OWNER)
			return mreg_p->user_reg->name;
	}

	return NULL;
}

/* are any users with access on the channel? */
static int
access_users_on_channel(struct chan_reg *chreg_p)
{
	struct channel *chptr;
	struct member_reg *mreg_tp;
	struct user_reg *ureg_p;
	struct chmember *msptr;
	dlink_node *ptr;

	chptr = find_channel(chreg_p->name);
	if (chptr == NULL)
		return 0;

	DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;
		ureg_p = msptr->client_p->user->user_reg;

		if(ureg_p && (mreg_tp = find_member_reg(ureg_p, chreg_p)))
		{
			if(!mreg_tp->suspend)
				return 1;
		}
	}
	return 0;
}

/* e_chanserv_updatechan()
 *
 * inputs	-
 * outputs	-
 * side effects - any pending updates are written to the database
 */
static void
e_chanserv_updatechan(void *unused)
{
	struct chan_reg *chreg_p;
	dlink_node *ptr, *next_ptr;
	int i;

	/* Start a transaction, we're going to make a lot of changes */
	rsdb_transaction(RSDB_TRANS_START);

	HASH_WALK_SAFE(i, MAX_CHANNEL_TABLE, ptr, next_ptr, chan_reg_table)
	{
		chreg_p = ptr->data;

		if(chreg_p->flags & CS_FLAGS_NEEDUPDATE)
		{
			chreg_p->flags &= ~CS_FLAGS_NEEDUPDATE;
			rsdb_exec(NULL, "UPDATE channels "
					"SET last_time=%lu, tsinfo=%lu WHERE chname = '%Q'",
					chreg_p->last_time, chreg_p->tsinfo, chreg_p->name);
		}
	}
	HASH_WALK_END

	rsdb_transaction(RSDB_TRANS_END);
}

static int
h_chanserv_dbsync(void *unused, void *unusedd)
{
	e_chanserv_updatechan(NULL);
	return 0;
}

static void
e_chanserv_expirechan(void *unused)
{
	struct chan_reg *chreg_p;
	dlink_node *ptr, *next_ptr;
	int i;

	/* Start a transaction, we're going to make a lot of changes */
	rsdb_transaction(RSDB_TRANS_START);

	HASH_WALK_SAFE(i, MAX_CHANNEL_TABLE, ptr, next_ptr, chan_reg_table)
	{
		chreg_p = ptr->data;

		if(chreg_p->flags & CS_FLAGS_SUSPENDED)
		{
			if((chreg_p->last_time + config_file.cexpire_suspended_time) > CURRENT_TIME)
				continue;
		}
		else
		{
			if((chreg_p->last_time + config_file.cexpire_time) > CURRENT_TIME)
				continue;

			/* final check: make sure nobody with access is on the
			 * channel; to save cpu only do this if the channel would
			 * otherwise expire. */
			if(access_users_on_channel(chreg_p))
			{
				chreg_p->last_time = CURRENT_TIME;
				rsdb_exec(NULL, "UPDATE channels "
						"SET last_time = %lu WHERE chname = '%Q'",
						chreg_p->last_time, chreg_p->name);
				continue;
			}
		}

		destroy_channel_reg(chreg_p);
	}
	HASH_WALK_END

	rsdb_transaction(RSDB_TRANS_END);
}	

static void
e_chanserv_expireban(void *unused)
{
	struct chan_reg *chreg_p;
	struct ban_reg *banreg_p;
	dlink_node *hptr;
	dlink_node *ptr, *next_ptr;
	int i;

	/* Start a transaction, we're going to make a lot of changes */
	rsdb_transaction(RSDB_TRANS_START);

	HASH_WALK(i, MAX_CHANNEL_TABLE, hptr, chan_reg_table)
	{
		chreg_p = hptr->data;

		DLINK_FOREACH_SAFE(ptr, next_ptr, chreg_p->bans.head)
		{
			banreg_p = ptr->data;

			if(!banreg_p->hold || banreg_p->hold > CURRENT_TIME)
				continue;

			rsdb_exec(NULL, "DELETE FROM bans "
					"WHERE chname='%Q' and mask='%Q'",
					chreg_p->name, banreg_p->mask);
			free_ban_reg(chreg_p, banreg_p);
		}
	}
	HASH_WALK_END

	rsdb_transaction(RSDB_TRANS_END);
}

static void
e_chanserv_enforcetopic(void *unused)
{
	struct channel *chptr;
	struct chan_reg *chreg_p;
	dlink_node *ptr;
	int i;

	HASH_WALK(i, MAX_CHANNEL_TABLE, ptr, chan_reg_table)
	{
		chreg_p = ptr->data;

		/* we must be joined to set a topic.. */
		if(EmptyString(chreg_p->topic) ||
		   !(chreg_p->flags & CS_FLAGS_AUTOJOIN))
			continue;

		if((chptr = find_channel(chreg_p->name)) == NULL)
			continue;

		/* already has this topic set.. */
		if(!irccmp(chreg_p->topic, chptr->topic))
			continue;

		sendto_server(":%s TOPIC %s :%s",
				chanserv_p->name, chptr->name, chreg_p->topic);
		strlcpy(chptr->topic, chreg_p->topic, sizeof(chptr->topic));
		strlcpy(chptr->topicwho, MYNAME, sizeof(chptr->topicwho));
	}
	HASH_WALK_END
}

static int
h_chanserv_sjoin_lowerts(void *v_chptr, void *unused)
{
	struct channel *chptr = v_chptr;
	struct chan_reg *chreg_p;

	if((chreg_p = find_channel_reg(NULL, chptr->name)) == NULL)
		return 0;

	/* Save the new TS for later -- jilles */
	/* but only if it's actually different and autojoin is enabled:
	 * chreg_p->tsinfo is only used if autojoin is enabled and it
	 * is updated whenever autojoin is turned on. -- jilles
	 */
	if(chreg_p->tsinfo != chptr->tsinfo &&
		chreg_p->flags & CS_FLAGS_AUTOJOIN)
	{
		chreg_p->tsinfo = chptr->tsinfo;
		chreg_p->flags |= CS_FLAGS_NEEDUPDATE;
	}

	if(!chreg_p->emode.mode || 
	   (chptr->mode.mode & chreg_p->emode.mode) == chreg_p->emode.mode)
		return 0;

	/* if services is in there and we havent yet finished bursting then
	 * we will eventually send an sjoin, which will contain the updated
	 * modes..
	 */
	if(!sent_burst && dlink_list_length(&chptr->services))
	{
		int i;

		/* get the modes it doesnt have in common.. */
		i = chreg_p->emode.mode &
			~(chptr->mode.mode & chreg_p->emode.mode);

		/* and set them. */
		chptr->mode.mode |= i;

		if(i & MODE_LIMIT)
			chptr->mode.limit = chreg_p->emode.limit;

		if(i & MODE_KEY)
			strlcpy(chptr->mode.key, chreg_p->emode.key,
				sizeof(chptr->mode.key));
	}
	else
		h_chanserv_mode_simple(chptr, chreg_p);

	return 0;
}

static int
h_chanserv_mode_op(void *v_chptr, void *v_members)
{
	struct channel *chptr = v_chptr;
	struct chmember *member_p;
	struct chan_reg *chreg_p;
	dlink_list *members = v_members;
	dlink_node *ptr, *next_ptr;

	if(!dlink_list_length(members))
		return 0;

	if((chreg_p = find_channel_reg(NULL, chptr->name)) == NULL)
		return 0;

	if(chreg_p->flags & CS_FLAGS_SUSPENDED)
		return 0;

	/* only thing we're testing here.. */
	if(!(chreg_p->flags & CS_FLAGS_NOOPS) &&
	   !(chreg_p->flags & CS_FLAGS_RESTRICTOPS))
		return 0;

	modebuild_start(chanserv_p, chptr);

	if(chreg_p->flags & CS_FLAGS_NOOPS)
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, members->head)
		{
			member_p = ptr->data;
			member_p->flags &= ~MODE_OPPED;
			modebuild_add(DIR_DEL, "o", UID(member_p->client_p));
			dlink_destroy(ptr, members);
		}
	}
	else
	{
		struct member_reg *mreg_p;


		DLINK_FOREACH_SAFE(ptr, next_ptr, members->head)
		{
			member_p = ptr->data;
			mreg_p = find_member_reg(member_p->client_p->user->user_reg, 
						chreg_p);

			if(!mreg_p || mreg_p->suspend || mreg_p->level < S_C_OP)
			{
				member_p->flags &= ~MODE_OPPED;
				modebuild_add(DIR_DEL, "o",
						UID(member_p->client_p));
				dlink_destroy(ptr, members);
			}
		}
	}

	modebuild_finish();

	return 0;
}

static int
h_chanserv_mode_voice(void *v_chptr, void *v_members)
{
	struct channel *chptr = v_chptr;
	struct chmember *member_p;
	struct chan_reg *chreg_p;
	dlink_list *members = v_members;
	dlink_node *ptr, *next_ptr;

	if(!dlink_list_length(members))
		return 0;

	if((chreg_p = find_channel_reg(NULL, chptr->name)) == NULL)
		return 0;

	if(chreg_p->flags & CS_FLAGS_SUSPENDED)
		return 0;

	/* only thing we're testing here.. */
	if(!(chreg_p->flags & CS_FLAGS_NOVOICES))
		return 0;

	modebuild_start(chanserv_p, chptr);

	DLINK_FOREACH_SAFE(ptr, next_ptr, members->head)
	{
		member_p = ptr->data;
		member_p->flags &= ~MODE_VOICED;
		modebuild_add(DIR_DEL, "v", UID(member_p->client_p));
		dlink_destroy(ptr, members);
	}

	modebuild_finish();

	return 0;
}

static int
h_chanserv_mode_simple(void *v_chptr, void *v_chreg)
{
	struct channel *chptr = v_chptr;
	struct chan_reg *chreg_p = v_chreg;
	struct chmode mode;
	int i;

	if(chreg_p == NULL)
	{
		if((chreg_p = find_channel_reg(NULL, chptr->name)) == NULL)
			return 0;
	}

	/* only care about mode enforcement here.. */
	if(chreg_p->flags & CS_FLAGS_SUSPENDED || !chreg_p->emode.mode)
		return 0;

	memset(&mode, 0, sizeof(struct chmode));

	/* the modes it has in common.. */
	i = chptr->mode.mode & chreg_p->emode.mode;

	if(i != chreg_p->emode.mode)
	{
		/* to the modes it doesnt have in common */
		mode.mode = chreg_p->emode.mode & ~i;
		chptr->mode.mode |= mode.mode;
	}

	if(chptr->mode.limit != chreg_p->emode.limit)
	{
		mode.mode |= MODE_LIMIT;
		chptr->mode.limit = chreg_p->emode.limit;
		mode.limit = chreg_p->emode.limit;
	}
		
	if(strcasecmp(chptr->mode.key, chreg_p->emode.key))
	{
		mode.mode |= MODE_KEY;
		strlcpy(chptr->mode.key, chreg_p->emode.key,
			sizeof(chptr->mode.key));
		strlcpy(mode.key, chreg_p->emode.key, sizeof(mode.key));
	}

	/* we simply issue a mode of what it doesnt have in common */
	if(mode.mode)
		sendto_server(":%s MODE %s %s",
				chanserv_p->name, chptr->name,
				chmode_to_string(&mode));
	return 0;
}

static int
h_chanserv_join(void *v_chptr, void *v_members)
{
	struct chan_reg *chreg_p;
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	struct channel *chptr = v_chptr;
	struct chmember *member_p;
	dlink_list *members = v_members;
	dlink_node *ptr, *next_ptr;
	dlink_node *bptr;
	int hit;

	/* another hook couldve altered this.. */
	if(!dlink_list_length(members))
		return 0;

	/* not registered, cant ban anyone.. */
	if((chreg_p = find_channel_reg(NULL, chptr->name)) == NULL)
		return 0;

	if(chreg_p->flags & CS_FLAGS_SUSPENDED)
		return 0;

	modebuild_start(chanserv_p, chptr);
	kickbuild_start();

	current_mark++;

	DLINK_FOREACH_SAFE(ptr, next_ptr, members->head)
	{
		member_p = ptr->data;
		hit = 0;

		mreg_p = find_member_reg(member_p->client_p->user->user_reg,
					chreg_p);
		/* so we don't have to check mreg_p->suspend for the rest
		 * of this loop */
		if(mreg_p != NULL && mreg_p->suspend)
			mreg_p = NULL;

		DLINK_FOREACH(bptr, chreg_p->bans.head)
		{
			banreg_p = bptr->data;

			if(!match(banreg_p->mask, member_p->client_p->user->mask))
				continue;

			if(mreg_p && mreg_p->level >= banreg_p->level)
				continue;

			if(find_exempt(member_p->chptr, member_p->client_p))
				break;

			/* explained in delban */
			if(mreg_p)
				mreg_p->bants = chreg_p->bants;

			if(is_opped(member_p))
				modebuild_add(DIR_DEL, "o", UID(member_p->client_p));

			if(banreg_p->marked != current_mark)
			{
				if(!dlink_find_string(banreg_p->mask, &chptr->bans))
					dlink_add_alloc(my_strdup(banreg_p->mask), &chptr->bans);

				modebuild_add(DIR_ADD, "b", banreg_p->mask);
				banreg_p->marked = current_mark;
			}

			/* if we're kicking out the last user, enable
			 * autojoin so that kicking them doesnt destroy the
			 * channel --fl
			 */
			if(dlink_list_length(&chptr->users) == 1)
				enable_autojoin(chreg_p);

			kickbuild_add(UID(member_p->client_p), banreg_p->reason);

			dlink_destroy(ptr, members);
			del_chmember(member_p);
			hit++;
			break;
		}

		if(hit)
			continue;
		else
			hit = 0;

		if(mreg_p != NULL)
		{
			/* update last_time whenever a user with access joins */
			mreg_p->channel_reg->last_time = CURRENT_TIME;
			mreg_p->channel_reg->flags |= CS_FLAGS_NEEDUPDATE;
		}

		if(is_opped(member_p))
		{
			if((chreg_p->flags & CS_FLAGS_NOOPS) ||
			   (chreg_p->flags & CS_FLAGS_RESTRICTOPS &&
			    (!mreg_p || mreg_p->level < S_C_OP)))
			{
				/* stop them cycling for ops */
				if(dlink_list_length(&chptr->users) == 1)
					enable_autojoin(chreg_p);

				modebuild_add(DIR_DEL, "o", 
						UID(member_p->client_p));
				member_p->flags &= ~MODE_OPPED;
			}

			hit++;
		}

		if(is_voiced(member_p) && (chreg_p->flags & CS_FLAGS_NOVOICES))
		{
			if(dlink_list_length(&chptr->users) == 1)
				enable_autojoin(chreg_p);

			modebuild_add(DIR_DEL, "v", UID(member_p->client_p));
			member_p->flags &= ~MODE_VOICED;
			continue;
		}

		/* removed ops from them.. cant be setting them back */
		if(hit)
			continue;

		if(mreg_p != NULL)
		{

			if(mreg_p->flags & CS_MEMBER_AUTOOP &&
			   mreg_p->level >= S_C_OP &&
			   !(chreg_p->flags & CS_FLAGS_NOOPS))
			{
				modebuild_add(DIR_ADD, "o", 
					UID(member_p->client_p));
				member_p->flags &= ~MODE_DEOPPED;
				member_p->flags |= MODE_OPPED;
			}
			else if(mreg_p->flags & CS_MEMBER_AUTOVOICE &&
				!is_voiced(member_p) &&
				!(chreg_p->flags & CS_FLAGS_NOVOICES))
			{
				modebuild_add(DIR_ADD, "v",
					UID(member_p->client_p));
				member_p->flags |= MODE_VOICED;
			}
		}
	}

	modebuild_finish();
	kickbuild_finish(chanserv_p, chptr);

	return 0;
}

/* A user logged in;
 * if they have access, note the channel is being used and op/voice them if
 * appropriate
 */
static int
h_chanserv_user_login(void *v_client_p, void *unused)
{
	dlink_node *ptr;
	struct user *user;
	struct user_reg *ureg_p;
	struct channel *chptr;
	struct chmember *member_p;
	struct chan_reg *chreg_p;
	struct member_reg *mreg_p;

	user = ((struct client *)v_client_p)->user;
	ureg_p = user->user_reg;

	DLINK_FOREACH(ptr, user->channels.head)
	{
		member_p = ptr->data;
		chptr = member_p->chptr;

		if((chreg_p = find_channel_reg(NULL, chptr->name)) == NULL)
			continue;
		if(chreg_p->flags & CS_FLAGS_SUSPENDED)
			continue;

		/* user has no access to channel */
		if((mreg_p = find_member_reg(ureg_p, chreg_p)) == NULL)
			continue;
		if(mreg_p->suspend)
			continue;

		/* channel is being used */
		mreg_p->channel_reg->last_time = CURRENT_TIME;
		mreg_p->channel_reg->flags |= CS_FLAGS_NEEDUPDATE;

		/* autoop/voice dont work on +o users */
		if(is_opped(member_p))
			continue;

		if(mreg_p->flags & CS_MEMBER_AUTOOP && mreg_p->level >= S_C_OP &&
		   !(chreg_p->flags & CS_FLAGS_NOOPS))
		{
			sendto_server(":%s MODE %s +o %s",
					chanserv_p->name, chptr->name,
					UID(member_p->client_p));
			member_p->flags &= ~MODE_DEOPPED;
			member_p->flags |= MODE_OPPED;
		}
		else if(mreg_p->flags & CS_MEMBER_AUTOVOICE &&
			!is_voiced(member_p) &&
			!(chreg_p->flags & CS_FLAGS_NOVOICES))
		{
			sendto_server(":%s MODE %s +v %s",
					chanserv_p->name, chptr->name,
					UID(member_p->client_p));
			member_p->flags |= MODE_VOICED;
		}
	}

	return 0;
}

static int
o_chan_chanregister(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct chan_reg *chreg_p;
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;

	if((chreg_p = find_channel_reg(NULL, parv[0])))
	{
		service_send(chanserv_p, client_p, conn_p,
				"Channel %s is already registered", parv[0]);
		return 0;
	}

	if((ureg_p = find_user_reg_nick(NULL, parv[1])) == NULL)
	{
		if(*parv[1] == '=')
			service_send(chanserv_p, client_p, conn_p,
					"Nickname %s is not logged in", parv[1]);
		else
			service_send(chanserv_p, client_p, conn_p,
					"Username %s is not registered", parv[1]);

		return 0;
	}

	slog(chanserv_p, 1, "%s - CHANREGISTER %s %s",
		OPER_NAME(client_p, conn_p), parv[0], ureg_p->name);
	watch_send(WATCH_CSADMIN, client_p, conn_p, 1,
			"CHANREGISTER %s %s", parv[0], ureg_p->name);

	chreg_p = BlockHeapAlloc(channel_reg_heap);
	init_channel_reg(chreg_p, parv[0]);

	mreg_p = make_member_reg(ureg_p, chreg_p, OPER_NAME(client_p, conn_p), 200, 0);
	write_member_db_entry(mreg_p);

	service_send(chanserv_p, client_p, conn_p,
			"Channel %s registered to %s",
			chreg_p->name, ureg_p->name);
	return 0;
}

static int
o_chan_chandrop(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct chan_reg *reg_p;

	if((reg_p = find_channel_reg(NULL, parv[0])) == NULL)
	{
		service_send(chanserv_p, client_p, conn_p,
				"Channel %s is not registered", parv[0]);
		return 0;
	}

	slog(chanserv_p, 1, "%s - CHANDROP %s", 
		OPER_NAME(client_p, conn_p), parv[0]);
	watch_send(WATCH_CSADMIN, client_p, conn_p, 1,
			"CHANDROP %s", parv[0]);

	destroy_channel_reg(reg_p);

	service_send(chanserv_p, client_p, conn_p,
			"Channel %s registration dropped", parv[0]);
	return 0;
}

static int
o_chan_chansuspend(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct chan_reg *reg_p;
	const char *reason;

	if((reg_p = find_channel_reg(NULL, parv[0])) == NULL)
	{
		service_send(chanserv_p, client_p, conn_p,
				"Channel %s is not registered", parv[0]);
		return 0;
	}

	if(reg_p->flags & CS_FLAGS_SUSPENDED)
	{
		service_send(chanserv_p, client_p, conn_p,
				"Channel %s is already suspended", parv[0]);
		return 0;
	}

	reason = rebuild_params(parv, parc, 1);

	slog(chanserv_p, 1, "%s - CHANSUSPEND %s %s",
		OPER_NAME(client_p, conn_p), parv[0], reason);
	watch_send(WATCH_CSADMIN, client_p, conn_p, 1,
			"CHANSUSPEND %s %s", parv[0], reason);

	reg_p->flags |= CS_FLAGS_SUSPENDED;
	reg_p->suspender = my_strdup(OPER_NAME(client_p, conn_p));
	reg_p->suspend_reason = my_strndup(reason, SUSPENDREASONLEN);
	reg_p->last_time = CURRENT_TIME;

	rsdb_exec(NULL, "UPDATE channels SET flags=%d, suspender='%Q', "
			"suspend_reason='%Q', last_time=%lu WHERE chname = '%Q'",
			reg_p->flags, reg_p->suspender, reg_p->suspend_reason,
			reg_p->last_time, reg_p->name);

	service_send(chanserv_p, client_p, conn_p,
			"Channel %s suspended", parv[0]);
	return 0;
}

static int
o_chan_chanunsuspend(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct chan_reg *reg_p;

	if((reg_p = find_channel_reg(NULL, parv[0])) == NULL)
	{
		service_send(chanserv_p, client_p, conn_p,
				"Channel %s is not registered", parv[0]);
		return 0;
	}

	if((reg_p->flags & CS_FLAGS_SUSPENDED) == 0)
	{
		service_send(chanserv_p, client_p, conn_p,
				"Channel %s is not suspended", reg_p->name);
		return 0;
	}

	slog(chanserv_p, 1, "%s - CHANUNSUSPEND %s",
		OPER_NAME(client_p, conn_p), reg_p->name);
	watch_send(WATCH_CSADMIN, client_p, conn_p, 1,
			"CHANUNSUSPEND %s", reg_p->name);

	reg_p->flags &= ~CS_FLAGS_SUSPENDED;
	my_free(reg_p->suspender);
	reg_p->suspender = NULL;
	my_free(reg_p->suspend_reason);
	reg_p->suspend_reason = NULL;
	reg_p->last_time = CURRENT_TIME;

	rsdb_exec(NULL, "UPDATE channels SET flags=%d,suspender=NULL,suspend_reason=NULL,last_time=%lu WHERE chname = '%Q'",
			reg_p->flags, reg_p->last_time, reg_p->name);

	service_send(chanserv_p, client_p, conn_p,
			"Channel %s unsuspended", reg_p->name);
	return 0;
}

#define CHANLIST_LEN	350	/* should be long enough */

static int
o_chan_chanlist(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static char def_mask[] = "*";
	static char buf[BUFSIZE];
	struct chan_reg *chreg_p;
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

	service_send(chanserv_p, client_p, conn_p,
			"Channel list matching %s, limit %u%s",
			mask, limit,
			suspended ? ", suspended" : "");

	HASH_WALK(i, MAX_CHANNEL_TABLE, ptr, chan_reg_table)
	{
		chreg_p = ptr->data;

		if(!match(mask, chreg_p->name))
			continue;

		if(suspended)
		{
			if((chreg_p->flags & CS_FLAGS_SUSPENDED) == 0)
				continue;
		}
		else if(chreg_p->flags & CS_FLAGS_SUSPENDED)
			continue;

		if(!longlist)
		{
			arglen = strlen(chreg_p->name);

			if(buflen + arglen >= CHANLIST_LEN)
			{
				service_send(chanserv_p, client_p, conn_p,
						"  %s", buf);
				buf[0] = '\0';
				buflen = 0;
			}

			strcat(buf, chreg_p->name);
			strcat(buf, " ");
			buflen += arglen+1;
		}
		else
		{
			static char last_active[] = "Active";
			char timebuf[BUFSIZE];
			const char *p = last_active;

			if(suspended)
			{
				snprintf(timebuf, sizeof(timebuf),
					"Suspended by %s: %s",
					chreg_p->suspender,
					chreg_p->suspend_reason ? chreg_p->suspend_reason : NULL);
				p = timebuf;
			}
			else if(!access_users_on_channel(chreg_p))
			{
				snprintf(timebuf, sizeof(timebuf), "Last %s",
					get_short_duration(CURRENT_TIME - chreg_p->last_time));
				p = timebuf;
			}

			service_send(chanserv_p, client_p, conn_p,
				"  %s - To %s For %s %s",
				chreg_p->name, find_owner(chreg_p),
				get_short_duration(CURRENT_TIME - chreg_p->reg_time),
				p);
		}

		if(limit == 1)
		{
			/* two loops to exit here, kludge it */
			i = MAX_CHANNEL_TABLE;
			break;
		}

		limit--;
	}
	HASH_WALK_END

	if(!longlist)
		service_send(chanserv_p, client_p, conn_p, "  %s", buf);

	service_send(chanserv_p, client_p, conn_p,
			"End of channel list%s",
			(limit == 1) ? ", limit reached" : "");

	slog(chanserv_p, 1, "%s - CHANLIST %s",
		OPER_NAME(client_p, conn_p), mask);
	watch_send(WATCH_CSOPER, client_p, conn_p, 1, "CHANLIST %s", mask);

	return 0;
}

static int
o_chan_chaninfo(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct chan_reg *chreg_p;
	const char *owner;

	if((chreg_p = find_channel_reg(NULL, parv[0])) == NULL)
	{
		service_send(chanserv_p, client_p, conn_p,
				"Channel %s is not registered", parv[0]);
		return 0;
	}

	owner = find_owner(chreg_p);

	service_send(chanserv_p, client_p, conn_p,
			"[%s] Registered to %s for %s",
			chreg_p->name, owner ?  owner : "?unknown?",
			get_duration((time_t) (CURRENT_TIME - chreg_p->reg_time)));

	if(chreg_p->flags & CS_FLAGS_SUSPENDED)
		service_send(chanserv_p, client_p, conn_p,
				"[%s] Suspended by %s: %s",
				chreg_p->name, chreg_p->suspender, 
				chreg_p->suspend_reason ? chreg_p->suspend_reason : "");
	else
		dump_info_extended(client_p, conn_p, chreg_p);

	if(parc > 1 && !EmptyString(parv[1]))
	{
		if(!strcasecmp(parv[1], "-listusers"))
			dump_info_accesslist(client_p, conn_p, chreg_p);
	}

	slog(chanserv_p, 1, "%s - CHANINFO %s",
		OPER_NAME(client_p, conn_p), chreg_p->name);
	watch_send(WATCH_CSOPER, client_p, conn_p, 1, "CHANINFO %s", chreg_p->name);

	return 0;
}

static int
s_chan_register(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct chmember *mptr;
	struct chan_reg *reg_p;
	struct member_reg *mreg_p;

	if(config_file.disable_cregister)
	{
		service_error(chanserv_p, client_p, "%s::REGISTER is disabled", chanserv_p->name);
		return 1;
	}

	if((reg_p = find_channel_reg(NULL, parv[0])))
	{
		service_error(chanserv_p, client_p, "Channel %s is already registered", parv[0]);
		return 1;
	}

	if((chptr = find_channel(parv[0])) == NULL || 
	   (mptr = find_chmember(chptr, client_p)) == NULL ||
	   !is_opped(mptr))
	{
		service_error(chanserv_p, client_p, "You are not opped on %s", parv[0]);
		return 1;
	}

	/* apply timed registration limits */
	if(config_file.cregister_time && config_file.cregister_amount)
	{
		static time_t last_time = 0;
		static int last_count = 0;

		if((last_time + config_file.cregister_time) < CURRENT_TIME)
		{
			last_time = CURRENT_TIME;
			last_count = 1;
		}
		else if(last_count >= config_file.cregister_amount)
		{
			service_error(chanserv_p, client_p, 
				"%s::REGISTER rate-limited, try again shortly",
				chanserv_p->name);
			return 1;
		}
		else
			last_count++;
	}

	/* check per host registration limits */
	if(config_file.chregister_time && config_file.chregister_amount)
	{
		struct host_entry *hent = find_host(client_p->user->host);

		/* this host has gone over the limits.. */
		if(hent->cregister >= config_file.chregister_amount &&
		   hent->cregister_expire > CURRENT_TIME)
		{
			service_error(chanserv_p, client_p,
				"%s::REGISTER rate-limited for your host, try again later",
				chanserv_p->name);
			return 1;
		}

		/* its expired.. reset limits */
		if(hent->cregister_expire <= CURRENT_TIME)
		{
			hent->cregister_expire = CURRENT_TIME + config_file.chregister_time;
			hent->cregister = 0;
		}

		hent->cregister++;
	}

	slog(chanserv_p, 2, "%s %s REGISTER %s",
		client_p->user->mask, client_p->user->user_reg->name, parv[0]);
	watch_send(WATCH_CSREGISTER, client_p, NULL, 0, "REGISTER %s", chptr->name);

	reg_p = BlockHeapAlloc(channel_reg_heap);
	init_channel_reg(reg_p, parv[0]);

	mreg_p = make_member_reg(client_p->user->user_reg, reg_p,
				client_p->user->user_reg->name, 200, 0);
	write_member_db_entry(mreg_p);

	service_error(chanserv_p, client_p, "Channel %s registered",
			chptr->name);

	return 5;
}

static int
s_chan_adduser(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;
	int level;
	int flags = 0;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_USERLIST)) == NULL)
		return 1;

	if((ureg_p = find_user_reg_nick(client_p, parv[1])) == NULL)
		return 1;

	if(find_member_reg(ureg_p, mreg_p->channel_reg))
	{
		service_error(chanserv_p, client_p, "User %s on %s already has access",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	level = atoi(parv[2]);

	if(level < 1 || level >= mreg_p->level)
	{
		service_error(chanserv_p, client_p, "Access level %d invalid", level);
		return 1;
	}

	if(parc > 3)
	{
		if(!strcasecmp(parv[3], "OP"))
		{
			flags = CS_MEMBER_AUTOOP;
		}
		else if(!strcasecmp(parv[3], "VOICE"))
		{
			flags = CS_MEMBER_AUTOVOICE;
		}
		else if(!strcasecmp(parv[3], "NONE"))
		{
		}
		else
		{
			service_error(chanserv_p, client_p,
					"Auto level %s invalid", parv[3]);
			return 1;
		}
	}

	slog(chanserv_p, 5, "%s %s ADDUSER %s %s %d %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0], ureg_p->name, level, flags != 0 ? parv[3] : "NONE");

	mreg_tp = make_member_reg(ureg_p, mreg_p->channel_reg, mreg_p->user_reg->name, level, flags);
	write_member_db_entry(mreg_tp);

	service_error(chanserv_p, client_p, "User %s on %s level %d%s%s added",
			ureg_p->name, mreg_p->channel_reg->name, level,
			flags != 0 ? " autolevel " : "",
			flags != 0 ? parv[3] : "");

	return 1;
}

static int
s_chan_deluser(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct chan_reg *chreg_p;
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_USERLIST)) == NULL)
		return 1;

	if((ureg_p = find_user_reg_nick(client_p, parv[1])) == NULL)
		return 1;

	chreg_p = mreg_p->channel_reg;

	if((mreg_tp = find_member_reg(ureg_p, chreg_p)) == NULL)
	{
		service_error(chanserv_p, client_p, "User %s on %s does not have access",
				ureg_p->name, chreg_p->name);
		return 1;
	}

	/* allow users to delete themselves.. */
	if(mreg_p->level <= mreg_tp->level && mreg_p != mreg_tp)
	{
		service_error(chanserv_p, client_p, "User %s on %s access level equal or higher",
				ureg_p->name, chreg_p->name);
		return 1;
	}

	slog(chanserv_p, 5, "%s %s DELUSER %s %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0], mreg_tp->user_reg->name);

	service_error(chanserv_p, client_p, "User %s on %s removed",
			mreg_tp->user_reg->name, chreg_p->name);

	delete_member_db_entry(mreg_tp);

	free_member_reg(mreg_tp, 1);

	return 1;
}

static int
s_chan_moduser(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;
	char *endptr;
	int level;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_USERLIST)) == NULL)
		return 1;

	if((ureg_p = find_user_reg_nick(client_p, parv[1])) == NULL)
		return 1;

	if((mreg_tp = find_member_reg(ureg_p, mreg_p->channel_reg)) == NULL)
	{
		service_error(chanserv_p, client_p, "User %s on %s does not have access",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	if(mreg_p->level <= mreg_tp->level)
	{
		service_error(chanserv_p, client_p, "User %s on %s access level equal or higher",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	level = strtol(parv[2], &endptr, 10);

	if(!EmptyString(endptr) || level < 1 || level >= mreg_p->level)
	{
		service_error(chanserv_p, client_p, "Access level %s invalid",
				parv[2]);
		return 1;
	}

	slog(chanserv_p, 5, "%s %s MODUSER %s %s %d",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0], mreg_tp->user_reg->name, level);

	mreg_tp->level = level;
	my_free(mreg_tp->lastmod);
	mreg_tp->lastmod = my_strdup(mreg_p->user_reg->name);

	service_error(chanserv_p, client_p, "User %s on %s level %d set",
			mreg_tp->user_reg->name, mreg_tp->channel_reg->name, level);

	rsdb_exec(NULL, 
			"UPDATE members SET level = %d, lastmod = '%Q' "
			"WHERE chname = '%Q' AND username = '%Q'",
			level, mreg_p->user_reg->name, 
			mreg_tp->channel_reg->name, mreg_tp->user_reg->name);

	return 1;
}

static int
s_chan_modauto(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_USERLIST)) == NULL)
		return 1;

	if((ureg_p = find_user_reg_nick(client_p, parv[1])) == NULL)
		return 1;

	if((mreg_tp = find_member_reg(ureg_p, mreg_p->channel_reg)) == NULL)
	{
		service_error(chanserv_p, client_p, "User %s on %s does not have access",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	if(mreg_p->level <= mreg_tp->level && mreg_p != mreg_tp)
	{
		service_error(chanserv_p, client_p, "User %s on %s access level equal or higher",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	if(!strcasecmp(parv[2], "OP"))
	{
		mreg_tp->flags &= ~CS_MEMBER_AUTOVOICE;
		mreg_tp->flags |= CS_MEMBER_AUTOOP;
	}
	else if(!strcasecmp(parv[2], "VOICE"))
	{
		mreg_tp->flags &= ~CS_MEMBER_AUTOOP;
		mreg_tp->flags |= CS_MEMBER_AUTOVOICE;
	}
	else if(!strcasecmp(parv[2], "NONE"))
	{
		mreg_tp->flags &= ~(CS_MEMBER_AUTOVOICE|CS_MEMBER_AUTOOP);
	}
	else
	{
		service_error(chanserv_p, client_p, "Auto level %s invalid",
				parv[2]);
		return 1;
	}

	slog(chanserv_p, 5, "%s %s MODAUTO %s %s %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0], mreg_tp->user_reg->name, parv[2]);

	my_free(mreg_tp->lastmod);
	mreg_tp->lastmod = my_strdup(mreg_p->user_reg->name);

	service_error(chanserv_p, client_p, "User %s on %s autolevel %s set",
			mreg_tp->user_reg->name, mreg_tp->channel_reg->name, 
			parv[2]);

	rsdb_exec(NULL, 
			"UPDATE members SET flags = %d, lastmod = '%Q' "
			"WHERE chname = '%Q' AND username = '%Q'",
			mreg_tp->flags, mreg_p->user_reg->name, 
			mreg_tp->channel_reg->name, mreg_tp->user_reg->name);

	return 1;
}

static int
s_chan_listusers(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *mreg_p;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_SUSPEND)) == NULL)
		return 1;

	slog(chanserv_p, 3, "%s %s LISTUSERS %s",
		client_p->user->mask, client_p->user->user_reg->name, parv[0]);

	dump_info_accesslist(client_p, NULL, mreg_p->channel_reg);

	service_error(chanserv_p, client_p, "End of access list");

	return 3;
}

static int
s_chan_suspend(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;
	int level;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_SUSPEND)) == NULL)
		return 1;

	if((ureg_p = find_user_reg_nick(client_p, parv[1])) == NULL)
		return 1;

	if((mreg_tp = find_member_reg(ureg_p, mreg_p->channel_reg)) == NULL)
	{
		service_error(chanserv_p, client_p, "User %s on %s does not have access",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	if(mreg_p->level <= mreg_tp->level)
	{
		service_error(chanserv_p, client_p, "User %s on %s access level equal or higher",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	/* suspended already at a higher level? */
	if(mreg_tp->suspend > mreg_p->level)
	{
		service_error(chanserv_p, client_p, "User %s on %s suspend level higher",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	level = atoi(parv[2]);

	if(level < 1 || level > mreg_p->level)
	{
		service_error(chanserv_p, client_p, "Suspend level %d invalid", level);
		return 1;
	}

	slog(chanserv_p, 6, "%s %s SUSPEND %s %s %d",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0], mreg_tp->user_reg->name, level);

	mreg_tp->suspend = level;
	my_free(mreg_tp->lastmod);
	mreg_tp->lastmod = my_strdup(mreg_p->user_reg->name);

	service_error(chanserv_p, client_p, "User %s on %s suspend %d set",
			mreg_tp->user_reg->name, mreg_tp->channel_reg->name, level);

	rsdb_exec(NULL, "UPDATE members SET suspend = %d, lastmod = '%Q' "
			"WHERE chname = '%Q' AND username = '%Q'",
			level, mreg_p->user_reg->name, 
			mreg_tp->channel_reg->name, mreg_tp->user_reg->name);

	return 1;
}

static int
s_chan_unsuspend(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_SUSPEND)) == NULL)
		return 1;

	if((ureg_p = find_user_reg_nick(client_p, parv[1])) == NULL)
		return 1;

	if((mreg_tp = find_member_reg(ureg_p, mreg_p->channel_reg)) == NULL)
	{
		service_error(chanserv_p, client_p, "User %s on %s does not have access",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	/* suspended at a higher level?  we allow the user level being
	 * higher here, as the suspend level dictates who can unsuspend
	 */
	if(mreg_tp->suspend > mreg_p->level)
	{
		service_error(chanserv_p, client_p, "User %s on %s suspend level higher",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	slog(chanserv_p, 6, "%s %s UNSUSPEND %s %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0], mreg_tp->user_reg->name);

	mreg_tp->suspend = 0;
	my_free(mreg_tp->lastmod);
	mreg_tp->lastmod = my_strdup(mreg_p->user_reg->name);

	service_error(chanserv_p, client_p, "User %s on %s unsuspended",
			 mreg_tp->user_reg->name, mreg_tp->channel_reg->name);

	rsdb_exec(NULL, "UPDATE members SET suspend = 0, lastmod = '%Q' "
			"WHERE chname = '%Q' AND username = '%Q'",
			mreg_p->user_reg->name, 
			mreg_tp->channel_reg->name, mreg_tp->user_reg->name);

	return 1;
}

static int
s_chan_clearmodes(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static const char def_wild[] = "*";
	struct member_reg *mreg_p;
	struct channel *chptr;
	const char *modev[3];
	char modestr[10];

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_CLEAR)) == NULL)
		return 1;

	if(!chptr->mode.key[0] && !chptr->mode.limit &&
	   !(chptr->mode.mode & MODE_INVITEONLY))
	{
		service_error(chanserv_p, client_p, "Channel %s has no modes to clear",
				chptr->name);
		return 1;
	}

	slog(chanserv_p, 4, "%s %s CLEARMODES %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	snprintf(modestr, sizeof(modestr), "-%s%s%s",
			(chptr->mode.mode & MODE_INVITEONLY) ? "i" : "",
			chptr->mode.limit ? "l" : "",
			chptr->mode.key[0] ? "k" : "");
	modev[0] = modestr;
	modev[1] = chptr->mode.key[0] ? def_wild : NULL;
	modev[2] = NULL;

	parse_full_mode(chptr, chanserv_p, modev, chptr->mode.key[0] ? 2 : 1, 0);

	service_error(chanserv_p, client_p, "Channel %s modes cleared",
			chptr->name);

	return 1;
}

static void
s_chan_clearops_loc(struct channel *chptr, struct chan_reg *chreg_p, 
			int level)
{
	struct member_reg *mreg_tp;
	struct user_reg *ureg_p;
	struct chmember *msptr;
	dlink_node *ptr;

	modebuild_start(chanserv_p, chptr);

	DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;

		if(!is_opped(msptr))
			continue;

		ureg_p = msptr->client_p->user->user_reg;

		if(ureg_p && (mreg_tp = find_member_reg(ureg_p, chreg_p)))
		{
			if(mreg_tp->level >= level && !mreg_tp->suspend)
				continue;
		}

		modebuild_add(DIR_DEL, "o", UID(msptr->client_p));
		msptr->flags &= ~MODE_OPPED;
	}

	modebuild_finish();
}

static void
s_chan_clearvoices_loc(struct channel *chptr, struct chan_reg *chreg_p)
{
	struct chmember *msptr;
	dlink_node *ptr;

	modebuild_start(chanserv_p, chptr);

	DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;

		if(!is_voiced(msptr))
			continue;

		modebuild_add(DIR_DEL, "v", UID(msptr->client_p));
		msptr->flags &= ~MODE_VOICED;
	}

	modebuild_finish();
}

static int
s_chan_clearops(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct channel *chptr;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_CLEAR)) == NULL)
		return 1;

	slog(chanserv_p, 4, "%s %s CLEAROPS %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	s_chan_clearops_loc(chptr, mreg_p->channel_reg, S_C_OP);

	service_error(chanserv_p, client_p, "Channel %s ops cleared", 
			chptr->name);
	return 3;
}

static int
s_chan_clearallops(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct channel *chptr;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_CLEAR)) == NULL)
		return 1;

	slog(chanserv_p, 4, "%s %s CLEARALLOPS %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	s_chan_clearops_loc(chptr, mreg_p->channel_reg, mreg_p->level);

	service_error(chanserv_p, client_p, "Channel %s ops cleared", 
			chptr->name);
	return 3;
}

static int
s_chan_clearbans(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	dlink_node *ptr, *next_ptr;
	dlink_node *bptr;
	int found;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_CLEAR)) == NULL)
		return 1;

	slog(chanserv_p, 4, "%s %s CLEARBANS %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	modebuild_start(chanserv_p, chptr);

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->bans.head)
	{
		found = 0;

		DLINK_FOREACH(bptr, mreg_p->channel_reg->bans.head)
		{
			banreg_p = bptr->data;

			if(!irccmp((const char *) ptr->data, banreg_p->mask))
			{
				found++;
				break;
			}
		}

		if(!found)
		{
			modebuild_add(DIR_DEL, "b", ptr->data);
			my_free(ptr->data);
			dlink_destroy(ptr, &chptr->bans);
		}
	}

	modebuild_finish();

	service_error(chanserv_p, client_p, "Channel %s bans cleared", chptr->name);

	return 3;
}

/* s_chan_set_flag()
 *
 * inputs	- client setting flag, channel reg, flag name,
 * 		  argument (ON/OFF etc), flag bitmask
 * outputs	- 1 if we add flag, -1 if we remove flag, 0 if unchanged
 * side effects - 
 */
static int
s_chan_set_flag(struct client *client_p, struct chan_reg *chreg_p,
		const char *name, const char *arg, int flag)
{
	if(!strcasecmp(arg, "ON"))
	{
		service_error(chanserv_p, client_p,
			"Channel %s %s set ON", chreg_p->name, name);

		if(chreg_p->flags & flag)
			return 0;

		chreg_p->flags |= flag;
		update_chreg_flags(chreg_p);
		return 1;
	}
	else if(!strcasecmp(arg, "OFF"))
	{
		service_error(chanserv_p, client_p,
			"Channel %s %s set OFF", chreg_p->name, name);

		if((chreg_p->flags & flag) == 0)
			return 0;

		chreg_p->flags &= ~flag;
		update_chreg_flags(chreg_p);
		return -1;
	}
	
	service_error(chanserv_p, client_p,
			"Channel %s %s is %s", chreg_p->name, name,
			(chreg_p->flags & flag) ? "ON" : "OFF");
	return 0;
}

static int
s_chan_set(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct chan_reg *chreg_p;
	struct member_reg *mreg_p;
	struct channel *chptr;
	static const char dummy[] = "\0";
	const char *arg;
	int retval;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_MANAGER)) == NULL)
		return 1;

	chreg_p = mreg_p->channel_reg;

	/* we need to strcasecmp() this, could be NULL.. */
	arg = EmptyString(parv[2]) ? dummy : parv[2];

	if(!strcasecmp(parv[1], "NOOPS"))
	{
		retval = s_chan_set_flag(client_p, chreg_p, parv[1], arg, CS_FLAGS_NOOPS);

		if(retval == 1)
		{
			if(!(chptr = find_channel(chreg_p->name)))
				return 1;

			/* hack! noone can match level S_C_OWNER+1 :) */
			s_chan_clearops_loc(chptr, chreg_p, S_C_OWNER+1);
		}

		return 1;
	}
	else if(!strcasecmp(parv[1], "RESTRICTOPS"))
	{
		retval = s_chan_set_flag(client_p, chreg_p, parv[1], arg, CS_FLAGS_RESTRICTOPS);

		if(retval == 1)
		{
			if(!(chptr = find_channel(chreg_p->name)))
				return 1;

			/* hack! */
			s_chan_clearops_loc(chptr, chreg_p, S_C_OP);
		}

		return 1;
	}
	else if(!strcasecmp(parv[1], "NOVOICES"))
	{
		retval = s_chan_set_flag(client_p, chreg_p, parv[1], arg, CS_FLAGS_NOVOICES);

		if(retval == 1)
		{
			if(!(chptr = find_channel(chreg_p->name)))
				return 1;

			s_chan_clearvoices_loc(chptr, chreg_p);
		}

		return 1;
	}
	else if(!strcasecmp(parv[1], "NOVOICECMD"))
	{
		s_chan_set_flag(client_p, chreg_p, parv[1], arg, CS_FLAGS_NOVOICECMD);
		return 1;
	}
	else if(!strcasecmp(parv[1], "AUTOJOIN"))
	{
		retval = s_chan_set_flag(client_p, chreg_p, parv[1], arg, CS_FLAGS_AUTOJOIN);

		if(retval == 1)
			enable_autojoin(chreg_p);
		else if(retval == -1)
			part_service(chanserv_p, chreg_p->name);

		return 1;
	}
	else if(!strcasecmp(parv[1], "WARNOVERRIDE"))
	{
		s_chan_set_flag(client_p, chreg_p, parv[1], arg, CS_FLAGS_WARNOVERRIDE);
		return 1;
	}
	else if(!strcasecmp(parv[1], "CREATEMODES"))
	{
		struct chmode mode;
		const char *modestring;

		if(EmptyString(arg))
		{
			service_error(chanserv_p, client_p,
				"Channel %s CREATEMODES are %s",
				chreg_p->name,
				chmode_to_string(&chreg_p->cmode));
			return 1;
		}

		memset(&mode, 0, sizeof(struct chmode));

		if(strchr(parv[2], '-') ||
		   !parse_simple_mode(&mode, (const char **) parv, parc, 2))
		{
			service_error(chanserv_p, client_p,
				"Mode %s invalid",
				parv[2]);
			return 1;
		}

		chreg_p->cmode.mode = mode.mode;
		chreg_p->cmode.limit = mode.limit;

		if(mode.key[0])
			strlcpy(chreg_p->cmode.key, mode.key,
				sizeof(chreg_p->cmode.key));

		modestring = chmode_to_string(&mode);

		rsdb_exec(NULL, "UPDATE channels SET createmodes = '%Q' "
				"WHERE chname = '%Q'",
				modestring, chreg_p->name);

		service_error(chanserv_p, client_p,
				"Channel %s CREATEMODES set %s",
				chreg_p->name, modestring);

		return 1;
	}
	else if(!strcasecmp(parv[1], "ENFORCEMODES"))
	{
		struct chmode mode;
		const char *modestring;

		if(EmptyString(arg))
		{
			service_error(chanserv_p, client_p,
				"Channel %s ENFORCEMODES are %s",
				chreg_p->name,
				chmode_to_string(&chreg_p->emode));
			return 1;
		}

		memset(&mode, 0, sizeof(struct chmode));

		if(strchr(arg, '-') ||
		   !parse_simple_mode(&mode, (const char **) parv, parc, 2))
		{
			service_error(chanserv_p, client_p,
				"Mode %s invalid", arg);
			return 1;
		}

		chreg_p->emode.mode = mode.mode;
		chreg_p->emode.limit = mode.limit;

		if(mode.key[0])
			strlcpy(chreg_p->emode.key, mode.key,
				sizeof(chreg_p->emode.key));

		modestring = chmode_to_string(&mode);

		rsdb_exec(NULL, "UPDATE channels SET enforcemodes='%Q' "
				"WHERE chname='%Q'",
				modestring, chreg_p->name);
		service_error(chanserv_p, client_p,
				"Channel %s ENFORCEMODES set %s",
				chreg_p->name, modestring);

		/* this will do all the hard work for us.. */
		if((chptr = find_channel(chreg_p->name)))
			h_chanserv_mode_simple(chptr, chreg_p);

		return 1;
	}
	else if(!strcasecmp(parv[1], "TOPIC"))
	{
		char *data;

		if(EmptyString(arg))
		{
			service_error(chanserv_p, client_p,
				"Channel %s TOPIC is '%s'",
				chreg_p->name, EmptyString(chreg_p->topic) ?
				 "<none>" : chreg_p->topic);
			return 1;
		}

		data = rebuild_params(parv, parc, 2);

		if(!irccmp(data, "-none"))
		{
			my_free(chreg_p->topic);
			chreg_p->topic = NULL;

			rsdb_exec(NULL, "UPDATE channels SET "
					"topic = NULL WHERE chname = '%Q'",
					chreg_p->name);

			service_error(chanserv_p, client_p,
					"Channel %s TOPIC unset",
					chreg_p->name);
			return 1;
		}

		if(strlen(data) > TOPICLEN)
			data[TOPICLEN] = '\0';

		my_free(chreg_p->topic);
		chreg_p->topic = my_strdup(data);

		rsdb_exec(NULL, "UPDATE channels SET topic='%Q' "
				"WHERE chname='%Q'",
				data, chreg_p->name);

		service_error(chanserv_p, client_p,
				"Channel %s TOPIC set '%s'",
				chreg_p->name, data);
		return 1;
	}
	else if(!strcasecmp(parv[1], "URL"))
	{
		if(EmptyString(arg))
		{
			service_error(chanserv_p, client_p,
				"Channel %s URL is '%s'",
				chreg_p->name, EmptyString(chreg_p->url) ?
				 "<none>" : chreg_p->url);
			return 1;
		}

		if(!irccmp(arg, "-none"))
		{
			my_free(chreg_p->url);
			chreg_p->url = NULL;

			rsdb_exec(NULL, "UPDATE channels SET "
					"url = NULL WHERE chname = '%Q'",
					chreg_p->name);

			service_error(chanserv_p, client_p,
					"Channel %s URL unset",
					chreg_p->name);
			return 1;
		}

		my_free(chreg_p->url);
		chreg_p->url = my_strndup(arg, TOPICLEN);

		rsdb_exec(NULL, "UPDATE channels SET url='%Q' "
				"WHERE chname='%Q'",
				arg, chreg_p->name);

		service_error(chanserv_p, client_p,
				"Channel %s URL set '%s'",
				chreg_p->name, arg);
		return 1;
	}


	service_error(chanserv_p, client_p, "Set option invalid");
	return 1;
}


#if 0
/* This will only work if chanserv is on the channel itself.. */
static int
s_chan_topic(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static char buf[BUFSIZE];
	struct member_reg *reg_p;
	struct channel *chptr;
	int i = 0;

	if((reg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_REGULAR)) == NULL)
		return 1;

	if(!EmptyString(reg_p->channel_reg->topic))
	{
		service_error(chanserv_p, client_p, "Channel %s has an enforced topic",
				parv[0]);
		return 1;
	}

	buf[0] = '\0';

	while(++i < parc)
		strlcat(buf, parv[i], sizeof(buf));
		
	sendto_server(":%s TOPIC %s :[%s] %s",
			MYNAME, parv[0], reg_p->user_reg->name, buf);

	return 1;
			
}
#endif

static int
s_chan_op(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *reg_p;
	struct channel *chptr;
	struct chmember *msptr;

	if((reg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_OP)) == NULL)
		return 1;

	/* noone is allowed to be opped.. */
	if(reg_p->channel_reg->flags & CS_FLAGS_NOOPS)
	{
		service_error(chanserv_p, client_p,
			"Channel %s is set NOOPS",
			reg_p->channel_reg->name);
		return 1;
	}

	if((msptr = find_chmember(chptr, client_p)) == NULL)
	{
		service_error(chanserv_p, client_p, "You are not on %s", parv[0]);
		return 1;
	}

	if(is_opped(msptr))
	{
		service_error(chanserv_p, client_p, "You are already opped on %s", parv[0]);
		return 1;
	}

	slog(chanserv_p, 6, "%s %s OP %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	msptr->flags &= ~MODE_DEOPPED;
	msptr->flags |= MODE_OPPED;
	sendto_server(":%s MODE %s +o %s",
			chanserv_p->name, parv[0], UID(client_p));
	return 1;
}

static int
s_chan_voice(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *reg_p;
	struct channel *chptr;
	struct chmember *msptr;

	if((reg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_USER)) == NULL)
		return 1;

	/* noone is allowed to be voiced.. */
	if(reg_p->channel_reg->flags & CS_FLAGS_NOVOICES)
	{
		service_error(chanserv_p, client_p,
			"Channel %s is set NOVOICES",
			reg_p->channel_reg->name);
		return 1;
	}

	if(reg_p->channel_reg->flags & CS_FLAGS_NOVOICECMD)
	{
		service_error(chanserv_p, client_p,
				"Channel %s is set NOVOICECMD",
				reg_p->channel_reg->name);
		return 1;
	}

	if((msptr = find_chmember(chptr, client_p)) == NULL)
	{
		service_error(chanserv_p, client_p, "You are not on %s", parv[0]);
		return 1;
	}

	if(is_voiced(msptr))
	{
		service_error(chanserv_p, client_p, "You are already voiced on %s", parv[0]);
		return 1;
	}

	slog(chanserv_p, 6, "%s %s VOICE %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	msptr->flags |= MODE_VOICED;
	sendto_server(":%s MODE %s +v %s",
			chanserv_p->name, parv[0], UID(client_p));
	return 1;
}


static int
s_chan_invite(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *reg_p;
	struct channel *chptr;

	if((reg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_USER)) == NULL)
		return 1;

	if((chptr->mode.mode & MODE_INVITEONLY) == 0)
	{
		service_error(chanserv_p, client_p, "Channel %s is not invite-only", parv[0]);
		return 1;
	}

	if(find_chmember(chptr, client_p))
	{
		service_error(chanserv_p, client_p, "You are already on %s", parv[0]);
		return 1;
	}

	slog(chanserv_p, 6, "%s %s INVITE %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	sendto_server(":%s INVITE %s %s",
			chanserv_p->name, UID(client_p), chptr->name);

	if(reg_p->channel_reg->flags & CS_FLAGS_WARNOVERRIDE &&
	   reg_p->channel_reg->flags & CS_FLAGS_AUTOJOIN)
		sendto_server(":%s NOTICE @%s :INVITE requested by %s:%s",
				chanserv_p->name, chptr->name,
				reg_p->user_reg->name, client_p->user->mask);

	return 1;
}

static int
s_chan_getkey(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct channel *chptr;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_USER)) == NULL)
		return 1;

	if(!chptr->mode.key[0])
	{
		service_error(chanserv_p, client_p,
			"Channel %s is not keyed", parv[0]);
		return 1;
	}

	if(find_chmember(chptr, client_p))
	{
		service_error(chanserv_p, client_p,
				"You are already on %s", parv[0]);
		return 1;
	}

	slog(chanserv_p, 6, "%s %s GETKEY %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	service_error(chanserv_p, client_p,
			"Channel %s key is: %s", parv[0], chptr->mode.key);

	if(mreg_p->channel_reg->flags & CS_FLAGS_WARNOVERRIDE &&
	   mreg_p->channel_reg->flags & CS_FLAGS_AUTOJOIN)
		sendto_server(":%s NOTICE @%s :GETKEY requested by %s:%s",
				chanserv_p->name, chptr->name,
				mreg_p->user_reg->name, client_p->user->mask);

	return 1;
}

static int
s_chan_addban(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static char reason[BUFSIZE];
	struct channel *chptr;
	struct chmember *msptr;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;
	struct ban_reg *banreg_p;
	dlink_node *ptr, *next_ptr;
	const char *mask;
	char *endptr;
	int duration;
	int level;
	int loc = 1;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_REGULAR)) == NULL)
		return 1;

	duration = (int) strtol(parv[1], &endptr, 10);

	/* valid duration.. */
	if(EmptyString(endptr))
		loc++;
	else
		duration = 0;

	mask = parv[loc++];

	if(dlink_list_length(&mreg_p->channel_reg->bans) > config_file.cmax_bans)
	{
		service_error(chanserv_p, client_p, "Channel %s banlist full",
				chptr->name);
		return 1;
	}

	if(!valid_ban(mask))
	{
		service_error(chanserv_p, client_p, "Ban %s invalid",
				mask);
		return 1;
	}

	if((banreg_p = find_ban_reg(mreg_p->channel_reg, mask)) != NULL)
	{
		service_error(chanserv_p, client_p, "Ban %s on %s already set",
				mask, mreg_p->channel_reg->name);
		return 1;
	}

	level = (int) strtol(parv[loc], &endptr, 10);

	if(!EmptyString(endptr) || level < 1 || level > mreg_p->level)
	{
		service_error(chanserv_p, client_p, "Access level %s invalid",
				parv[loc]);
		return 1;
	}
	
	loc++;

	/* we only require 4 params (chname, mask, level, reason) - but if a
	 * time was specified there may not be a reason..
	 */
	if(loc >= parc)
	{
		service_error(chanserv_p, client_p, "Insufficient parameters to CHANSERV::ADDBAN");
		return 1;
	}

	reason[0] = '\0';

	while(loc < parc)
	{
		if(strlcat(reason, parv[loc], sizeof(reason)) >= REASON_MAGIC)
		{
			reason[REASON_MAGIC] = '\0';
			break;
		}

		loc++;

		/* more params to come */
		if(loc != parc)
			strlcat(reason, " ", sizeof(reason));
	}

	slog(chanserv_p, 6, "%s %s ADDBAN %s %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0], mask);

	banreg_p = make_ban_reg(mreg_p->channel_reg, mask, reason, 
			mreg_p->user_reg->name, level, 
			duration ? CURRENT_TIME + (duration*60) : 0);
	write_ban_db_entry(banreg_p, mreg_p->channel_reg->name);

	service_error(chanserv_p, client_p, "Ban %s on %s added",
			mask, mreg_p->channel_reg->name);

	if(chptr == NULL)
		return 1;

	/* already +b'd */
	DLINK_FOREACH(ptr, chptr->bans.head)
	{
		if(!irccmp((const char *) ptr->data, mask))
			return 1;
	}

	loc = 0;

	modebuild_start(chanserv_p, chptr);
	kickbuild_start();

	/* now enforce the ban.. */
	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->users.head)
	{
		msptr = ptr->data;

		if(!match(mask, msptr->client_p->user->mask))
			continue;

		/* matching +e */
		if(find_exempt(chptr, msptr->client_p))
			continue;

		/* dont kick people who have access to the channel,
		 * this prevents an unban, join, ban cycle.
		 */
		if((mreg_tp = find_member_reg(msptr->client_p->user->user_reg,
					      mreg_p->channel_reg)))
		{
			if(mreg_tp->level >= level && !mreg_tp->suspend)
				continue;

			mreg_tp->bants = mreg_tp->channel_reg->bants;
		}

		if(is_opped(msptr))
			modebuild_add(DIR_DEL, "o", UID(msptr->client_p));

		kickbuild_add(UID(msptr->client_p), reason);
		del_chmember(msptr);
		loc++;
	}

	/* only issue the +b if theres someone there it will
	 * actually match..
	 */
	if(loc)
	{
		if(!dlink_find_string(mask, &chptr->bans))
			dlink_add_alloc(my_strdup(mask), &chptr->bans);

		modebuild_add(DIR_ADD, "b", mask);
		modebuild_finish();
		kickbuild_finish(chanserv_p, chptr);
	}

	return 2;
}

static int
s_chan_delban(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	dlink_node *ptr;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_REGULAR)) == NULL)
		return 1;

	if((banreg_p = find_ban_reg(mreg_p->channel_reg, parv[1])) == NULL)
	{
		service_error(chanserv_p, client_p, "Ban %s on %s not found",
				parv[1], mreg_p->channel_reg->name);
		return 1;
	}

	if(banreg_p->level > mreg_p->level)
	{
		service_error(chanserv_p, client_p, "Ban %s on %s higher level",
				parv[1], mreg_p->channel_reg->name);
		return 1;
	}

	slog(chanserv_p, 6, "%s %s DELBAN %s %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0], parv[1]);

	service_error(chanserv_p, client_p, "Ban %s on %s removed",
			parv[1], mreg_p->channel_reg->name);

	rsdb_exec(NULL, "DELETE FROM bans WHERE chname = '%Q' AND mask = '%Q'",
			mreg_p->channel_reg->name, parv[1]);

	free_ban_reg(mreg_p->channel_reg, banreg_p);

	/* Everytime a user who has access to unban has a higher level ban
	 * placed against them, we cache the fact theyre banned in bants.
	 * This makes unban quicker, as it doesnt have to check the level of
	 * bans - it doesnt check them at all.
	 *
	 * We need to invalidate this cache on DELBAN/MODBAN.  Not needed on
	 * ADDBAN, as that cannot mean a previously banned user can now
	 * possibly join.
	 */
	mreg_p->channel_reg->bants++;

	if(chptr == NULL)
		return 1;

	DLINK_FOREACH(ptr, chptr->bans.head)
	{
		if(!irccmp((const char *) ptr->data, parv[1]))
		{
			sendto_server(":%s MODE %s -b %s",
					chanserv_p->name, chptr->name, parv[1]);
			my_free(ptr->data);
			dlink_destroy(ptr, &chptr->bans);
			return 1;
		}
	}

	return 2;
}

static int
s_chan_modban(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	char *endptr;
	int level;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_REGULAR)) == NULL)
		return 1;

	level = (int) strtol(parv[2], &endptr, 10);

	if(!EmptyString(endptr) || level < 1 || level > mreg_p->level)
	{
		service_error(chanserv_p, client_p,
				"Access level %s invalid", parv[2]);
		return 1;
	}

	if((banreg_p = find_ban_reg(mreg_p->channel_reg, parv[1])) == NULL)
	{
		service_error(chanserv_p, client_p, "Ban %s on %s not found",
				parv[1], mreg_p->channel_reg->name);
		return 1;
	}

	if(banreg_p->level > mreg_p->level)
	{
		service_error(chanserv_p, client_p,
				"Ban %s on %s higher level",
				parv[1], mreg_p->channel_reg->name);
		return 1;
	}

	banreg_p->level = level;
	my_free(banreg_p->username);
	banreg_p->username = my_strdup(mreg_p->user_reg->name);

	service_error(chanserv_p, client_p,
			"Ban %s on %s level %d set",
			parv[1], mreg_p->channel_reg->name, level);

	rsdb_exec(NULL, "UPDATE bans SET level = %d, username = '%Q' "
			"WHERE chname = '%Q' AND mask = '%Q'",
			level, mreg_p->user_reg->name,
			mreg_p->channel_reg->name, parv[1]);

	mreg_p->channel_reg->bants++;

	return 1;
}

static int
s_chan_listbans(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	dlink_node *ptr;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_REGULAR)) == NULL)
		return 1;

	slog(chanserv_p, 3, "%s %s LISTBANS %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	service_error(chanserv_p, client_p, "Channel %s ban list:",
			mreg_p->channel_reg->name);

	DLINK_FOREACH(ptr, mreg_p->channel_reg->bans.head)
	{
		banreg_p = ptr->data;

		if(banreg_p->hold)
			service_error(chanserv_p, client_p, 
				"  %s %d (%d min) [mod: %s] :%s",
				banreg_p->mask, banreg_p->level,
				(int) ((banreg_p->hold - CURRENT_TIME) / 60),
				banreg_p->username, banreg_p->reason);
		else
			service_error(chanserv_p, client_p, 
				"  %s %d (perm) [mod: %s] :%s",
				banreg_p->mask, banreg_p->level,
				banreg_p->username, banreg_p->reason);
	}

	service_error(chanserv_p, client_p, "End of ban list");

	return 3;
}

static int
s_chan_unban(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct member_reg *mreg_p;
	dlink_node *ptr, *next_ptr;
	int found = 0;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_REGULAR)) == NULL)
		return 1;

	slog(chanserv_p, 6, "%s %s UNBAN %s",
		client_p->user->mask, client_p->user->user_reg->name,
		parv[0]);

	/* cached ban of higher level */
	if(mreg_p->bants == mreg_p->channel_reg->bants)
	{
		service_error(chanserv_p, client_p,
			"Channel %s has a higher level ban",
			chptr->name);
		return 1;
	}

	if(find_exempt(chptr, client_p))
	{
		service_error(chanserv_p, client_p, "Channel %s has a +e for your mask",
				chptr->name);
		return 1;
	}

	modebuild_start(chanserv_p, chptr);

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->bans.head)
	{
		if(match((const char *) ptr->data, client_p->user->mask))
		{
			modebuild_add(DIR_DEL, "b", (const char *) ptr->data);
			my_free(ptr->data);
			dlink_destroy(ptr, &chptr->bans);
			found++;
		}
	}

	modebuild_finish();

	if(found)
		service_error(chanserv_p, client_p, 
			"Channel %s matching bans cleared", chptr->name);
	else
		service_error(chanserv_p, client_p,
			"Channel %s has no +b for you", chptr->name);

	return 3;
}

static void
dump_info_extended(struct client *client_p, struct lconn *conn_p,
			struct chan_reg *chreg_p)
{
	if(!EmptyString(chreg_p->url))
		service_send(chanserv_p, client_p, conn_p,
			"[%s] URL: %s",
			chreg_p->name, chreg_p->url);

	if(chreg_p->flags & CS_FLAGS_SHOW)
		service_send(chanserv_p, client_p, conn_p,
			"[%s] Settings: %s%s%s%s%s%s",
			chreg_p->name,
			(chreg_p->flags & CS_FLAGS_AUTOJOIN) ? "AUTOJOIN " : "",
			(chreg_p->flags & CS_FLAGS_NOOPS) ? "NOOPS " : "",
			(chreg_p->flags & CS_FLAGS_NOVOICES) ? "NOVOICES " : "",
			(chreg_p->flags & CS_FLAGS_NOVOICECMD) ? "NOVOICECMD " : "",
			(chreg_p->flags & CS_FLAGS_RESTRICTOPS) ? "RESTRICTOPS " : "",
			(chreg_p->flags & CS_FLAGS_WARNOVERRIDE) ? "WARNOVERRIDE" : "");

	if(!EmptyString(chreg_p->topic))
		service_send(chanserv_p, client_p, conn_p,
			"[%s] Topic: %s", chreg_p->name, chreg_p->topic);

	if(chreg_p->emode.mode)
		service_send(chanserv_p, client_p, conn_p,
			"[%s] Enforced modes: %s",
			chreg_p->name, chmode_to_string(&chreg_p->emode));
}

static void
dump_info_accesslist(struct client *client_p, struct lconn *conn_p,
			struct chan_reg *chreg_p)
{
	struct member_reg *mreg_p;
	dlink_node *ptr;
	char buf[30];

	service_send(chanserv_p, client_p, conn_p,
			"[%s] Access list:", chreg_p->name);

	DLINK_FOREACH(ptr, chreg_p->users.head)
	{
		mreg_p = ptr->data;
		snprintf(buf, sizeof(buf), "(susp %d) ", mreg_p->suspend);

		service_send(chanserv_p, client_p, conn_p, 
				"  %-10s %3d%s %s[mod: %s]",
				mreg_p->user_reg->name, mreg_p->level,
				(mreg_p->flags & (CS_MEMBER_AUTOOP|CS_MEMBER_AUTOVOICE)) ?
				 ((mreg_p->flags & CS_MEMBER_AUTOOP) ? ":AOP" : ":AVOICE") : "",
				mreg_p->suspend ? buf : "", mreg_p->lastmod);
	}
}

static int
s_chan_info(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct chan_reg *reg_p;
	struct member_reg *mreg_p;
	const char *owner;

	if((reg_p = find_channel_reg(client_p, parv[0])) == NULL)
		return 1;

	owner = find_owner(reg_p);

	service_error(chanserv_p, client_p, 
			"[%s] Registered to %s for %s",
			reg_p->name, owner ?  owner : "?unknown?",
			get_duration((time_t) (CURRENT_TIME - reg_p->reg_time)));

	if(reg_p->flags & CS_FLAGS_SUSPENDED)
		service_error(chanserv_p, client_p, "[%s] Suspended by services admin",
				reg_p->name);
	else if((mreg_p = find_member_reg(client_p->user->user_reg, reg_p)) &&
		!mreg_p->suspend)
	{
		dump_info_extended(client_p, NULL, reg_p);
		return 3;
	}

	return 1;
}
