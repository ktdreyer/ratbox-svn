/* $Id$ */
#ifndef INCLUDED_s_userserv_h
#define INCLUDED_s_userserv_h

#define USERREGNAME_LEN	10
#define MAX_USER_REG_HASH	65536

struct client;

struct user_reg
{
	char name[USERREGNAME_LEN+1];
	char *password;

	time_t reg_time;
	time_t last_time;

	dlink_node node;
	dlink_list channels;
};

extern dlink_list user_reg_table[MAX_USER_REG_HASH];

extern struct user_reg *make_user_reg(void);
extern void free_user_reg(struct user_reg *);

extern void add_user_reg(struct user_reg *);

extern struct user_reg *find_user_reg(struct client *, const char *name);
extern struct user_reg *find_user_reg_nick(struct client *, const char *name);

extern void load_user_db(void);
extern void write_user_db_entry(struct user_reg *);

extern const char *get_crypt(const char *password, const char *salt);

#endif
