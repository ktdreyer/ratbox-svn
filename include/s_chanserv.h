/* $Id$ */
#ifndef INCLUDED_s_chanserv_h
#define INCLUDED_s_chanserv_h

struct user_reg;
struct chmod;

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

extern dlink_list chan_reg_table[MAX_CHAN_REG_HASH];

extern struct chan_reg *make_chan_reg(void);
extern void free_chan_reg(struct chan_reg *);

extern struct member_reg *make_member_reg(struct user_reg *, struct chan_reg *, int);
extern void free_member_reg(struct member_reg *);

extern void add_chan_reg(struct chan_reg *);
extern struct chan_reg *find_chan_reg(const char *name);

extern void load_chan_db(void);
extern void save_chan_db(void);

#endif