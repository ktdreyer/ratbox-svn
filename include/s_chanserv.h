/* $Id$ */
#ifndef INCLUDED_s_chanserv_h
#define INCLUDED_s_chanserv_h

struct user_reg;
struct chmode;

#define CS_FLAGS_SUSPENDED	0x001
#define CS_FLAGS_NOOPS		0x002
#define CS_FLAGS_AUTOJOIN	0x004
#define CS_FLAGS_WARNOVERRIDE	0x008
#define CS_FLAGS_RESTRICTOPS	0x010

/* those flags shown in CHANSERV::INFO */
#define CS_FLAGS_SHOW	(CS_FLAGS_NOOPS|CS_FLAGS_AUTOJOIN|\
			 CS_FLAGS_WARNOVERRIDE|CS_FLAGS_RESTRICTOPS)

#define CS_MEMBER_AUTOOP	0x001
#define CS_MEMBER_AUTOVOICE	0x002

/* used to validate flags on db load.. */
#define CS_MEMBER_ALL		(CS_MEMBER_AUTOOP|CS_MEMBER_AUTOVOICE)

struct chan_reg
{
	char *name;
	char *topic;
	char *url;
	char *suspender;
	struct chmode cmode;
	struct chmode emode;

	int flags;

	time_t tsinfo;
	time_t reg_time;
	time_t last_time;
	unsigned long bants;

	dlink_node node;

	dlink_list users;
	dlink_list bans;
};

struct member_reg
{
	struct user_reg *user_reg;
	struct chan_reg *channel_reg;

	int level;
	int flags;
	int suspend;
	unsigned long bants;

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
	int marked;

	dlink_node channode;
};

void free_channel_reg(struct chan_reg *);
void free_member_reg(struct member_reg *, int);

#endif
