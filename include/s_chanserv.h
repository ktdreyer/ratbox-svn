/* $Id$ */
#ifndef INCLUDED_s_chanserv_h
#define INCLUDED_s_chanserv_h

struct user_reg;
struct chmode;

#define MAX_CHAN_REG_HASH	16384

struct chan_reg
{
	char *name;
	char *topic;
	struct chmode *mode;
	int flags;

	time_t reg_time;
	time_t last_used;

	dlink_node node;

	dlink_list users;
	dlink_list bans;
};

struct member_reg
{
	struct user_reg *user_reg;
	struct chan_reg *chan_reg;

	int level;
	int suspend;
	int flags;

	char *lastmod;			/* last user to modify this membership */

	dlink_node usernode;
	dlink_node channode;
};

struct ban_reg
{
	char *mask;
	char *reason;
	int level;
	time_t hold;
};

extern void load_chan_db(void);
extern void save_chan_db(void);

#endif
