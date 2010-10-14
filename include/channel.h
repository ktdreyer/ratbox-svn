/* $Id$ */
#ifndef INCLUDED_channel_h
#define INCLUDED_channel_h

#define CHANNELLEN	200
#define KEYLEN		24

#define MAX_MODES	10

#define MAX_CHANNEL_TABLE	16384

extern rb_dlink_list channel_list;

#define DIR_NONE -1
#define DIR_ADD  1
#define DIR_DEL  0

struct chmode
{
	unsigned int mode;
	char key[KEYLEN+1];
	int limit;
};

struct channel
{
	char name[CHANNELLEN+1];
	char topic[TOPICLEN+1];
	char topicwho[NICKUSERHOSTLEN+1];

	time_t tsinfo;
	time_t topic_tsinfo;

	rb_dlink_list users;		/* users in this channel */
	rb_dlink_list users_opped;		/* subset of users who are opped */
	rb_dlink_list users_unopped;	/* subset of users who are unopped */
	rb_dlink_list services;

	rb_dlink_list bans;		/* +b */
	rb_dlink_list excepts;		/* +e */
	rb_dlink_list invites;		/* +I */

	struct chmode mode;

	rb_dlink_node listptr;		/* node in channel_list */
	rb_dlink_node nameptr;		/* node in channel hash */

#ifdef ENABLE_CHANFIX
	void *cfptr;			/* chanfix pointer */
#endif
};

struct chmember
{
	rb_dlink_node chnode;		/* node in struct channel */
	rb_dlink_node choppednode;		/* node in struct channel for opped/unopped */
	rb_dlink_node usernode;		/* node in struct client */

	struct channel *chptr;
	struct client *client_p;
	unsigned int flags;
};

#define MODE_INVITEONLY		0x0001
#define MODE_MODERATED		0x0002
#define MODE_NOEXTERNAL		0x0004
#define MODE_PRIVATE		0x0008
#define MODE_SECRET		0x0010
#define MODE_TOPIC		0x0020
#define MODE_LIMIT		0x0040
#define MODE_KEY		0x0080
#define MODE_REGONLY		0x0100
#define MODE_SSLONLY		0x0200

#define MODE_OPPED		0x0001
#define MODE_VOICED		0x0002
#define MODE_DEOPPED		0x0004

#define is_opped(x)	((x)->flags & MODE_OPPED)
#define is_voiced(x)	((x)->flags & MODE_VOICED)

extern void init_channel(void);

unsigned int hash_channel(const char *p);

int valid_chname(const char *name);

extern void add_channel(struct channel *chptr);
extern void del_channel(struct channel *chptr);
extern void free_channel(struct channel *chptr);
extern struct channel *find_channel(const char *name);

void remove_our_simple_modes(struct channel *chptr, struct client *service_p, 
				int prevent_join);
void remove_our_ov_modes(struct channel *chptr);
void remove_our_bans(struct channel *chptr, struct client *service_p, 
			int remove_bans, int remove_exceptions, int remove_invex);

extern const char *chmode_to_string(struct chmode *mode);
extern const char *chmode_to_string_simple(struct chmode *mode);

extern struct chmember *add_chmember(struct channel *chptr, struct client *target_p, int flags);
extern void del_chmember(struct chmember *mptr);
extern struct chmember *find_chmember(struct channel *chptr, struct client *target_p);
#define is_member(chptr, target_p) ((find_chmember(chptr, target_p)) ? 1 : 0)

extern void op_chmember(struct chmember *member_p);
extern void deop_chmember(struct chmember *member_p);

int find_exempt(struct channel *chptr, struct client *target_p);

extern unsigned long count_topics(void);

extern void join_service(struct client *service_p, const char *chname,
			time_t tsinfo, struct chmode *mode, int override);
extern int part_service(struct client *service_p, const char *chname);
extern void rejoin_service(struct client *service_p, struct channel *chptr, int reop);

/* c_mode.c */
int valid_ban(const char *banstr);

/* DO NOT DEREFERENCE THE VOID POINTER RETURNED FROM THIS */
void *del_ban(const char *banstr, rb_dlink_list *list);

int parse_simple_mode(struct chmode *, const char **, int, int, int);
void parse_full_mode(struct channel *, struct client *, const char **, int, int, int);

#endif
