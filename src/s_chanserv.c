/* src/s_chanserv.c
 *   Contains code for channel registration service.
 *
 * Copyright (C) 2004 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#include <sqlite.h>
#include "service.h"
#include "client.h"
#include "channel.h"
#include "rserv.h"
#include "log.h"
#include "io.h"
#include "s_chanserv.h"
#include "c_init.h"
#include "balloc.h"
#include "conf.h"

#define S_C_OWNER	200
#define S_C_MANAGER	190
#define S_C_USERLIST	150
#define S_C_CLEAR	100
#define S_C_SUSPEND	80
#define S_C_OP		50
#define S_C_VOICE	20

static struct client *chanserv_p;
static BlockHeap *channel_reg_heap;
static BlockHeap *member_reg_heap;

static dlink_list channel_reg_table[MAX_CHANNEL_TABLE];

static void load_channel_db(void);

static int s_chanserv_register(struct client *, char *parv[], int parc);
static int s_chanserv_adduser(struct client *, char *parv[], int parc);
static int s_chanserv_deluser(struct client *, char *parv[], int parc);
static int s_chanserv_moduser(struct client *, char *parv[], int parc);
static int s_chanserv_invite(struct client *, char *parv[], int parc);

static struct service_command chanserv_command[] =
{
	{ "REGISTER",	&s_chanserv_register,	1, NULL, 0, 1, 1, 0L },
	{ "ADDUSER",	&s_chanserv_adduser,	3, NULL, 0, 1, 1, 0L },
	{ "DELUSER",	&s_chanserv_deluser,	2, NULL, 0, 1, 1, 0L },
	{ "MODUSER",	&s_chanserv_moduser,	3, NULL, 0, 1, 1, 0L },
	{ "INVITE",	&s_chanserv_invite,	1, NULL, 0, 1, 1, 0L },
	{ "\0", NULL, 0, NULL, 0, 0, 0, 0L }
};

static struct service_handler chanserv_service = {
	"CHANSERV", "CHANSERV", "chanserv", "services.chanserv", "Channel Service", 0,
	30, 50, chanserv_command, NULL, NULL
};

void
init_s_chanserv(void)
{
	channel_reg_heap = BlockHeapCreate(sizeof(struct chan_reg), HEAP_CHANNEL_REG);
	member_reg_heap = BlockHeapCreate(sizeof(struct member_reg), HEAP_MEMBER_REG);

	chanserv_p = add_service(&chanserv_service);
	load_channel_db();
}

static void
add_channel_reg(struct chan_reg *reg_p)
{
	unsigned int hashv = hash_channel(reg_p->name);
	dlink_add(reg_p, &reg_p->node, &channel_reg_table[hashv]);
}

static struct chan_reg *
find_channel_reg(struct client *client_p, const char *name)
{
	struct chan_reg *reg_p;
	unsigned int hashv = hash_channel(name);
	dlink_node *ptr;

	DLINK_FOREACH(ptr, channel_reg_table[hashv].head)
	{
		reg_p = ptr->data;

		if(!strcasecmp(reg_p->name, name))
			return reg_p;
	}

	if(client_p != NULL)
		service_error(chanserv_p, client_p, "Channel %s is not registered",
				name);

	return NULL;
}

static struct member_reg *
make_member_reg(struct user_reg *ureg_p, struct chan_reg *chreg_p,
		const char *lastmod, int level)
{
	struct member_reg *mreg_p = BlockHeapAlloc(member_reg_heap);

	mreg_p->user_reg = ureg_p;
	mreg_p->channel_reg = chreg_p;
	mreg_p->level = level;
	mreg_p->lastmod = my_strdup(lastmod);

	dlink_add(mreg_p, &mreg_p->usernode, &ureg_p->channels);
	dlink_add(mreg_p, &mreg_p->channode, &chreg_p->users);

	return mreg_p;
}

static struct member_reg *
find_member_reg(struct user_reg *ureg_p, struct chan_reg *chreg_p)
{
	struct member_reg *mreg_p;
	dlink_node *ptr;

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

	if((mreg_p = find_member_reg(client_p->user->user_reg, chreg_p)) == NULL ||
	   mreg_p->level < level || mreg_p->suspend)
	{
		service_error(chanserv_p, client_p, "Insufficient access on %s",
				chreg_p->name);
		return NULL;
	}

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
channel_db_callback(void *db, int argc, char **argv, char **colnames)
{
	struct chan_reg *reg_p;

	if(argc < 6)
		return 0;

	if(EmptyString(argv[0]))
		return 0;

	reg_p = BlockHeapAlloc(channel_reg_heap);
	reg_p->name = my_strdup(argv[0]);

	if(!EmptyString(argv[1]))
		reg_p->topic = my_strdup(argv[1]);

	/* modes - argv[2] */

	reg_p->reg_time = atol(argv[3]);
	reg_p->last_time = atol(argv[4]);
	reg_p->flags = atoi(argv[5]);

	add_channel_reg(reg_p);
	return 0;
}

static int
member_db_callback(void *db, int argc, char **argv, char **colnames)
{
	struct chan_reg *chreg_p;
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;

	if(argc < 5)
		return 0;

	if(EmptyString(argv[0]) || EmptyString(argv[1]))
		return 0;

	if((chreg_p = find_channel_reg(NULL, argv[0])) == NULL ||
	   (ureg_p = find_user_reg(NULL, argv[1])) == NULL)
		return 0;

	mreg_p = make_member_reg(ureg_p, chreg_p, argv[2], atoi(argv[3]));
	mreg_p->suspend = atoi(argv[4]);
	return 0;
}

typedef int (*db_callback) (void *, int, char **, char **);


static void
loc_sqlite_exec(db_callback cb, const char *format, ...)
{
	va_list args;
	char *errmsg;
	int i;

	va_start(args, format);
	if((i = sqlite_exec_vprintf(rserv_db, format, cb, NULL, &errmsg, args)))
	{
		slog("fatal error: problem with db file: %s", errmsg);
		die("problem with db file");
	}
	va_end(args);
}

static void
load_channel_db(void)
{
	loc_sqlite_exec(channel_db_callback, "SELECT * FROM channels");
	loc_sqlite_exec(member_db_callback, "SELECT * FROM members");
}

static void
write_channel_db_entry(struct chan_reg *reg_p)
{
	loc_sqlite_exec(NULL, "INSERT INTO channels (chname, reg_time, last_time, flags) VALUES(%Q, %lu, %lu, 0)",
			reg_p->name, reg_p->reg_time, reg_p->last_time);
}

#if 0
static void
update_channel_db_entry(struct chan_reg *reg_p, const char *field)
{
	loc_sqlite_exec(NULL, "UPDATE channels SET %s WHERE chname = %Q",
			field, reg_p->name);
}
#endif

static void
write_member_db_entry(struct member_reg *reg_p)
{
	loc_sqlite_exec(NULL, "INSERT INTO members VALUES(%Q, %Q, %Q, %u, 0)",
			reg_p->channel_reg->name,
			reg_p->user_reg->name, reg_p->lastmod, reg_p->level);
}

static void
delete_member_db_entry(struct member_reg *reg_p)
{
	loc_sqlite_exec(NULL, "DELETE FROM members WHERE chname = %Q AND username = %Q",
			reg_p->channel_reg->name, reg_p->user_reg->name);
}

static int
s_chanserv_register(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;
	struct chmember *mptr;
	struct chan_reg *reg_p;
	struct member_reg *mreg_p;

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

	reg_p = BlockHeapAlloc(channel_reg_heap);
	reg_p->name = my_strdup(parv[0]);
	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	add_channel_reg(reg_p);

	mreg_p = make_member_reg(client_p->user->user_reg, reg_p,
				client_p->user->user_reg->name, 200);

	write_channel_db_entry(reg_p);
	write_member_db_entry(mreg_p);

	service_error(chanserv_p, client_p, "Registration of %s successful",
			chptr->name);

	return 5;
}

static int
s_chanserv_adduser(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;
	int level;

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

	mreg_tp = make_member_reg(ureg_p, mreg_p->channel_reg, mreg_p->user_reg->name, level);
	write_member_db_entry(mreg_tp);

	service_error(chanserv_p, client_p, "User %s on %s level %d added",
			ureg_p->name, mreg_p->channel_reg->name, level);

	return 1;
}

static int
s_chanserv_deluser(struct client *client_p, char *parv[], int parc)
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

	if(mreg_p->level <= mreg_tp->level)
	{
		service_error(chanserv_p, client_p, "User %s on %s access level equal or higher",
				ureg_p->name, mreg_p->channel_reg->name);
		return 1;
	}

	service_error(chanserv_p, client_p, "User %s on %s removed",
			mreg_tp->user_reg->name, mreg_tp->channel_reg->name);

	delete_member_db_entry(mreg_tp);

	my_free(mreg_tp->lastmod);
	dlink_delete(&mreg_tp->usernode, &mreg_tp->user_reg->channels);
	dlink_delete(&mreg_tp->channode, &mreg_tp->channel_reg->users);
	BlockHeapFree(member_reg_heap, mreg_tp);

	return 1;
}

static int
s_chanserv_moduser(struct client *client_p, char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;
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

	level = atoi(parv[2]);

	if(level < 1 || level >= mreg_p->level)
	{
		service_error(chanserv_p, client_p, "Access level %d invalid", level);
		return 1;
	}

	mreg_tp->level = level;
	my_free(mreg_tp->lastmod);
	mreg_tp->lastmod = my_strdup(mreg_p->user_reg->name);

	service_error(chanserv_p, client_p, "User %s on %s level %d set",
			mreg_tp->user_reg->name, mreg_tp->channel_reg->name, level);

	loc_sqlite_exec(NULL, "UPDATE members SET level = %d WHERE chname = %Q AND username = %Q",
			level, mreg_tp->channel_reg->name, mreg_tp->user_reg->name);
	loc_sqlite_exec(NULL, "UPDATE members SET lastmod = %Q WHERE chname = %Q AND username = %Q",
			mreg_p->user_reg->name, mreg_tp->channel_reg->name, mreg_tp->user_reg->name);

	return 1;
}
			
static int
s_chanserv_invite(struct client *client_p, char *parv[], int parc)
{
	struct member_reg *reg_p;
	struct channel *chptr;

	if((reg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_OP)) == NULL)
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

	sendto_server(":%s INVITE %s %s",
			chanserv_p->name, client_p->name, chptr->name);
	return 1;
}
