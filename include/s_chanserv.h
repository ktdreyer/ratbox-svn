/* $Id$ */
#ifndef INCLUDED_s_chanserv_h
#define INCLUDED_s_chanserv_h

struct user_reg;
struct chmode;

struct chan_reg
{
	char *name;
	char *topic;
	struct chmode mode;
	int flags;

	time_t reg_time;
	time_t last_time;

	dlink_node node;

	dlink_list users;
	dlink_list bans;
};

struct member_reg
{
	struct user_reg *user_reg;
	struct chan_reg *channel_reg;

	int level;
	int suspend;

	char *lastmod;			/* last user to modify this membership */

	dlink_node usernode;
	dlink_node channode;
};

struct ban_reg
{
	char *mask;
	char *reason;
	char *username;
	int level;
	time_t hold;

	dlink_node channode;
};

extern void load_chan_db(void);
extern void save_chan_db(void);

#endif
