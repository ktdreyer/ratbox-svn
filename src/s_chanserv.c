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
#include "modebuild.h"

#define S_C_OWNER	200
#define S_C_MANAGER	190
#define S_C_USERLIST	150
#define S_C_CLEAR	140
#define S_C_SUSPEND	100
#define S_C_OP		50
#define S_C_REGULAR	10
#define S_C_USER	1

#define REASON_MAGIC	50

static struct client *chanserv_p;
static BlockHeap *channel_reg_heap;
static BlockHeap *member_reg_heap;
static BlockHeap *ban_reg_heap;

static dlink_list channel_reg_table[MAX_CHANNEL_TABLE];

static void load_channel_db(void);

static int s_chanserv_register(struct client *, char *parv[], int parc);
static int s_chanserv_adduser(struct client *, char *parv[], int parc);
static int s_chanserv_deluser(struct client *, char *parv[], int parc);
static int s_chanserv_moduser(struct client *, char *parv[], int parc);
static int s_chanserv_listusers(struct client *, char *parv[], int parc);
static int s_chanserv_suspend(struct client *, char *parv[], int parc);
static int s_chanserv_unsuspend(struct client *, char *parv[], int parc);
static int s_chanserv_clearmodes(struct client *, char *parv[], int parc);
static int s_chanserv_clearops(struct client *, char *parv[], int parc);
static int s_chanserv_clearallops(struct client *, char *parv[], int parc);
static int s_chanserv_clearbans(struct client *, char *parv[], int parc);
static int s_chanserv_invite(struct client *, char *parv[], int parc);
static int s_chanserv_op(struct client *, char *parv[], int parc);
static int s_chanserv_voice(struct client *, char *parv[], int parc);
static int s_chanserv_addban(struct client *, char *parv[], int parc);
static int s_chanserv_delban(struct client *, char *parv[], int parc);
static int s_chanserv_listbans(struct client *, char *parv[], int parc);
static int s_chanserv_unban(struct client *, char *parv[], int parc);

static struct service_command chanserv_command[] =
{
	{ "REGISTER",	&s_chanserv_register,	1, NULL, 0, 1, 1, 0L },
	{ "ADDUSER",	&s_chanserv_adduser,	3, NULL, 0, 1, 1, 0L },
	{ "DELUSER",	&s_chanserv_deluser,	2, NULL, 0, 1, 1, 0L },
	{ "MODUSER",	&s_chanserv_moduser,	3, NULL, 0, 1, 1, 0L },
	{ "LISTUSERS",	&s_chanserv_listusers,	1, NULL, 0, 1, 1, 0L },
	{ "SUSPEND",	&s_chanserv_suspend,	3, NULL, 0, 1, 1, 0L },
	{ "UNSUSPEND",	&s_chanserv_unsuspend,	2, NULL, 0, 1, 1, 0L },
	{ "CLEARMODES",	&s_chanserv_clearmodes,	1, NULL, 0, 1, 1, 0L },
	{ "CLEAROPS",	&s_chanserv_clearops,	1, NULL, 0, 1, 1, 0L },
	{ "CLEARALLOPS",&s_chanserv_clearallops,1, NULL, 0, 1, 1, 0L },
	{ "CLEARBANS",	&s_chanserv_clearbans,	1, NULL, 0, 1, 1, 0L },
	{ "INVITE",	&s_chanserv_invite,	1, NULL, 0, 1, 1, 0L },
	{ "OP",		&s_chanserv_op,		1, NULL, 0, 1, 1, 0L },
	{ "VOICE",	&s_chanserv_voice,	1, NULL, 0, 1, 1, 0L },
	{ "ADDBAN",	&s_chanserv_addban,	4, NULL, 0, 1, 1, 0L },
	{ "DELBAN",	&s_chanserv_delban,	2, NULL, 0, 1, 1, 0L },
	{ "LISTBANS",	&s_chanserv_listbans,	1, NULL, 0, 1, 1, 0L },
	{ "UNBAN",	&s_chanserv_unban,	1, NULL, 0, 1, 1, 0L },
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
	ban_reg_heap = BlockHeapCreate(sizeof(struct ban_reg), HEAP_BAN_REG);

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

static int
ban_db_callback(void *db, int argc, char **argv, char **colnames)
{
	struct chan_reg *chreg_p;
	struct ban_reg *banreg_p;
	int level, hold;

	if(argc < 6)
		return 0;

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
	loc_sqlite_exec(channel_db_callback, "SELECT * FROM channels");
	loc_sqlite_exec(member_db_callback, "SELECT * FROM members");
	loc_sqlite_exec(ban_db_callback, "SELECT * FROM bans");
}

static void
write_channel_db_entry(struct chan_reg *reg_p)
{
	loc_sqlite_exec(NULL, "INSERT INTO channels (chname, reg_time, last_time, flags) VALUES(%Q, %lu, %lu, 0)",
			reg_p->name, reg_p->reg_time, reg_p->last_time);
}

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

static void
write_ban_db_entry(struct ban_reg *reg_p, const char *chname)
{
	loc_sqlite_exec(NULL, "INSERT INTO bans VALUES(%Q, %Q, %Q, %Q, %d, %lu)",
			chname, reg_p->mask, reg_p->reason, reg_p->username,
			reg_p->level, reg_p->hold);
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
s_chanserv_listusers(struct client *client_p, char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;
	dlink_node *ptr;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_SUSPEND)) == NULL)
		return 1;

	service_error(chanserv_p, client_p, "Channel %s access list:",
			mreg_p->channel_reg->name);

	DLINK_FOREACH(ptr, mreg_p->channel_reg->users.head)
	{
		mreg_tp = ptr->data;

		service_error(chanserv_p, client_p, "  %-10s %3d (%d) [mod: %s]",
				mreg_tp->user_reg->name, mreg_tp->level,
				mreg_tp->suspend, mreg_tp->lastmod);
	}

	service_error(chanserv_p, client_p, "End of access list");

	return 3;
}

static int
s_chanserv_suspend(struct client *client_p, char *parv[], int parc)
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

	mreg_tp->suspend = level;
	my_free(mreg_tp->lastmod);
	mreg_tp->lastmod = my_strdup(mreg_p->user_reg->name);

	service_error(chanserv_p, client_p, "User %s on %s suspend %d set",
			mreg_tp->user_reg->name, mreg_tp->channel_reg->name, level);

	loc_sqlite_exec(NULL, "UPDATE members SET suspend = %d WHERE chname = %Q AND username = %Q",
			level, mreg_tp->channel_reg->name, mreg_tp->user_reg->name);

	return 1;
}

static int
s_chanserv_unsuspend(struct client *client_p, char *parv[], int parc)
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

	mreg_tp->suspend = 0;
	my_free(mreg_tp->lastmod);
	mreg_tp->lastmod = my_strdup(mreg_p->user_reg->name);

	service_error(chanserv_p, client_p, "User %s on %s unsuspended",
			 mreg_tp->user_reg->name, mreg_tp->channel_reg->name);

	loc_sqlite_exec(NULL, "UPDATE members SET suspend = 0 WHERE chname = %Q AND username = %Q",
			mreg_tp->channel_reg->name, mreg_tp->user_reg->name);

	return 1;
}

static int
s_chanserv_clearmodes(struct client *client_p, char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct channel *chptr;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_CLEAR)) == NULL)
		return 1;

	if(!chptr->mode.key[0] && !chptr->mode.limit &&
	   !(chptr->mode.mode & MODE_INVITEONLY))
	{
		service_error(chanserv_p, client_p, "Channel %s has no modes to clear",
				chptr->name);
		return 1;
	}

	sendto_server(":%s MODE %s -%s%s%s%s",
			chanserv_p->name, chptr->name,
			(chptr->mode.mode & MODE_INVITEONLY) ? "i" : "",
			chptr->mode.limit ? "l" : "",
			chptr->mode.key ? "k " : "",
			chptr->mode.key ? chptr->mode.key : "");

	service_error(chanserv_p, client_p, "Channel %s modes cleared",
			chptr->name);

	return 1;
}

static void
s_chanserv_clearops_loc(struct client *client_p, struct channel *chptr,
			struct member_reg *mreg_p, int level)
{
	struct member_reg *mreg_tp;
	struct chmember *msptr;
	dlink_node *ptr;

	modebuild_start(chanserv_p, chptr);

	DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;

		if(!is_opped(msptr))
			continue;

		if(msptr->client_p->user->user_reg &&
		   (mreg_tp = find_member_reg(msptr->client_p->user->user_reg, mreg_p->channel_reg)))
		{
			if(mreg_tp->level >= level || mreg_tp->suspend)
				continue;
		}

		modebuild_add(DIR_DEL, "o", msptr->client_p->name);
		msptr->flags &= ~MODE_OPPED;
	}

	modebuild_finish();

	service_error(chanserv_p, client_p, "Channel %s ops cleared", chptr->name);
}

static int
s_chanserv_clearops(struct client *client_p, char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct channel *chptr;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_CLEAR)) == NULL)
		return 1;

	s_chanserv_clearops_loc(client_p, chptr, mreg_p, 0);
	return 3;
}

static int
s_chanserv_clearallops(struct client *client_p, char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct channel *chptr;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_CLEAR)) == NULL)
		return 1;

	s_chanserv_clearops_loc(client_p, chptr, mreg_p, mreg_p->level);
	return 3;
}

static int
s_chanserv_clearbans(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	dlink_node *ptr, *next_ptr;
	dlink_node *bptr;
	int found;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_CLEAR)) == NULL)
		return 1;

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

#if 0
/* This will only work if chanserv is on the channel itself.. */
static int
s_chanserv_topic(struct client *client_p, char *parv[], int parc)
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
s_chanserv_op(struct client *client_p, char *parv[], int parc)
{
	struct member_reg *reg_p;
	struct channel *chptr;
	struct chmember *msptr;

	if((reg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_OP)) == NULL)
		return 1;

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

	msptr->flags |= MODE_OPPED;
	sendto_server(":%s MODE %s +o %s",
			chanserv_p->name, parv[0], client_p->name);
	return 1;
}

static int
s_chanserv_voice(struct client *client_p, char *parv[], int parc)
{
	struct member_reg *reg_p;
	struct channel *chptr;
	struct chmember *msptr;

	if((reg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_USER)) == NULL)
		return 1;

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

	msptr->flags |= MODE_VOICED;
	sendto_server(":%s MODE %s +v %s",
			chanserv_p->name, parv[0], client_p->name);
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

static int
s_chanserv_addban(struct client *client_p, char *parv[], int parc)
{
	static char reason[BUFSIZE];
	struct channel *chptr;
	struct chmember *msptr;
	struct member_reg *mreg_p;
	struct member_reg *mreg_tp;
	struct ban_reg *banreg_p;
	dlink_node *ptr, *next_ptr;
	const char *mask;
	const char *p;
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

	p = mask;

	while(*p)
	{
		if(!IsBanChar(*p))
		{
			service_error(chanserv_p, client_p, "Ban %s invalid",
					mask);
			return 1;
		}

		p++;
	}

	DLINK_FOREACH(ptr, mreg_p->channel_reg->bans.head)
	{
		banreg_p = ptr->data;

		if(!irccmp(banreg_p->mask, mask))
		{
			service_error(chanserv_p, client_p, "Ban %s on %s already set",
					mask, mreg_p->channel_reg->name);
			return 1;
		}
	}

	level = (int) strtol(parv[loc], &endptr, 10);

	if(!EmptyString(endptr))
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
	}

	banreg_p = make_ban_reg(mreg_p->channel_reg, mask, reason, mreg_p->user_reg->name, level, duration);
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

	/* now enforce the ban.. */
	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->users.head)
	{
		msptr = ptr->data;

		if(match(mask, msptr->client_p->user->mask))
		{
			/* matching +e */
			if(find_exempt(chptr, msptr->client_p))
				continue;

			/* dont kick people who have access to the channel,
			 * this prevents an unban, join, ban cycle.
			 */
			if(msptr->client_p->user->user_reg &&
			   (mreg_tp = find_member_reg(msptr->client_p->user->user_reg,
						      mreg_p->channel_reg)))
			{
				if(!mreg_tp->suspend)
					continue;
			}

			if(!loc)
			{
				char *banstr = my_strdup(mask);

				sendto_server(":%s MODE %s +b %s",
						chanserv_p->name, chptr->name, mask);
				dlink_add_alloc(banstr, &chptr->bans);
				loc++;
			}

			sendto_server(":%s KICK %s %s :%s",
					chanserv_p->name, chptr->name, msptr->client_p->name,
					reason);
			del_chmember(msptr);
		}
	}

	return 1;
}

static int
s_chanserv_delban(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	dlink_node *ptr;
	int found = 0;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_REGULAR)) == NULL)
		return 1;

	DLINK_FOREACH(ptr, mreg_p->channel_reg->bans.head)
	{
		banreg_p = ptr->data;

		if(!irccmp(banreg_p->mask, parv[1]))
		{
			found++;
			break;
		}
	}

	if(!found)
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

	service_error(chanserv_p, client_p, "Ban %s on %s removed",
			parv[1], mreg_p->channel_reg->name);

	loc_sqlite_exec(NULL, "DELETE FROM bans WHERE chname = %Q AND mask = %Q",
			mreg_p->channel_reg->name, parv[1]);

	free_ban_reg(mreg_p->channel_reg, banreg_p);

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

	return 1;
}

static int
s_chanserv_listbans(struct client *client_p, char *parv[], int parc)
{
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	dlink_node *ptr;

	if((mreg_p = verify_member_reg_name(client_p, NULL, parv[0], S_C_REGULAR)) == NULL)
		return 1;

	service_error(chanserv_p, client_p, "Channel %s ban list:",
			mreg_p->channel_reg->name);

	DLINK_FOREACH(ptr, mreg_p->channel_reg->bans.head)
	{
		banreg_p = ptr->data;

		service_error(chanserv_p, client_p, "  %s %d (%lu) [mod: %s] :%s",
				banreg_p->mask, banreg_p->level, 
				(unsigned long) banreg_p->hold, banreg_p->username,
				banreg_p->reason);
	}

	service_error(chanserv_p, client_p, "End of ban list");

	return 3;
}

static int
s_chanserv_unban(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;
	struct member_reg *mreg_p;
	struct ban_reg *banreg_p;
	dlink_node *ptr, *next_ptr;
	dlink_node *bptr;
	int found;

	if((mreg_p = verify_member_reg_name(client_p, &chptr, parv[0], S_C_REGULAR)) == NULL)
		return 1;

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
			DLINK_FOREACH(bptr, mreg_p->channel_reg->bans.head)
			{
				banreg_p = bptr->data;

				if(!irccmp(banreg_p->mask, (const char *) ptr->data))
				{
					service_error(chanserv_p, client_p,
						"Ban %s on %s higher level",
						banreg_p->mask, chptr->name);
					return 2;
				}
			}

			modebuild_add(DIR_DEL, "b", (const char *) ptr->data);
			found++;
		}
	}

	modebuild_finish();

	service_error(chanserv_p, client_p, "Channel %s matching bans cleared", chptr->name);

	return 3;
}
