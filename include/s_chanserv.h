/* $Id$ */
#ifndef INCLUDED_s_chanserv_h
#define INCLUDED_s_chanserv_h

#define MAX_CHAN_REG_HASH	16384

struct user_reg;

struct chan_reg
{
	char *name;
	char *topic;

	time_t reg_time;
	time_t last_used;

	dlink_node node;

	dlink_list users;
};

struct member_reg
{
	struct user_reg *user_reg;
	struct chan_reg *chan_reg;

	int level;

	dlink_node usernode;
	dlink_node channode;
};

extern dlink_list chan_reg_table[MAX_CHAN_REG_HASH];

extern struct chan_reg *make_chan_reg(void);
extern void free_chan_reg(struct chan_reg *);

extern void add_chan_reg(struct chan_reg *);

extern void load_chan_db(void);
extern void save_chan_db(void);

#endif
