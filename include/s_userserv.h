/* $Id$ */
#ifndef INCLUDED_s_userserv_h
#define INCLUDED_s_userserv_h

#define MAX_USER_REG_HASH	65536

struct client;

struct user_reg
{
	char name[USERREGNAME_LEN];
	char *password;
	char *email;
	char *suspender;
	char *suspend_reason;

	time_t reg_time;
	time_t last_time;

	int flags;

	unsigned int language;

	dlink_node node;
	dlink_list channels;
	dlink_list users;
	dlink_list nicks;
};

/* Flags stored in the DB: 0xFFFF */
#define US_FLAGS_SUSPENDED	0x0001
#define US_FLAGS_PRIVATE	0x0002
#define US_FLAGS_NEVERLOGGEDIN	0x0004
#define US_FLAGS_NOACCESS	0x0008

/* Flags not stored in the DB: 0xFFFF0000 */
#define US_FLAGS_NEEDUPDATE	0x00010000

extern struct user_reg *find_user_reg(struct client *, const char *name);
extern struct user_reg *find_user_reg_nick(struct client *, const char *name);

void s_userserv_countmem(size_t *, size_t *, size_t *, size_t *);

#endif
