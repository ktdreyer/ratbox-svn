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
	char *email;
	char *suspender;

	time_t reg_time;
	time_t last_time;

	int flags;

	dlink_node node;
	dlink_list channels;
	dlink_list users;
};

#define US_FLAGS_SUSPENDED	0x0001

extern struct user_reg *find_user_reg(struct client *, const char *name);
extern struct user_reg *find_user_reg_nick(struct client *, const char *name);

#endif
