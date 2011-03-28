/* $Id$ */
#ifndef INCLUDED_s_userserv_h
#define INCLUDED_s_userserv_h

#define MAX_USER_REG_HASH	65536

struct client;

struct user_reg
{
	unsigned int id;	/* database id */

	char name[USERREGNAME_LEN+1];
	char *password;
	char *email;
	char *suspender;
	char *suspend_reason;
	time_t suspend_time;

	time_t reg_time;
	time_t last_time;

	int flags;

	unsigned int language;

	rb_dlink_node node;
	rb_dlink_list channels;
	rb_dlink_list users;
	rb_dlink_list nicks;
};

/* Flags stored in the DB: 0xFFFF */
#define US_FLAGS_SUSPENDED	0x0001
#define US_FLAGS_PRIVATE	0x0002
#define US_FLAGS_NEVERLOGGEDIN	0x0004
#define US_FLAGS_NOACCESS	0x0008
#define US_FLAGS_NOMEMOS	0x0010

/* Flags not stored in the DB: 0xFFFF0000 */
#define US_FLAGS_NEEDUPDATE	0x00010000

#define USER_SUSPEND_EXPIRED(x)	((x)->flags & US_FLAGS_SUSPENDED && (x)->suspend_time && (x)->suspend_time <= rb_time())

extern struct user_reg *find_user_reg(struct client *, const char *name);
extern struct user_reg *find_user_reg_nick(struct client *, const char *name);

void s_userserv_countmem(size_t *, size_t *, size_t *, size_t *);

#endif
