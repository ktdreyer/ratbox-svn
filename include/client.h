/* $Id$ */
#define NICKLEN 9
#define USERLEN 10
#define HOSTLEN 63
#define REALLEN 50
#define REASONLEN 50

#include "config.h"

/* XXX UGLY */
#ifdef USER_SERVICE
#include "s_userserv.h"
#endif

#define USERHOSTLEN (USERLEN + HOSTLEN + 1)
#define NICKUSERHOSTLEN	(NICKLEN + USERLEN + HOSTLEN + 2)

#define MAX_NAME_HASH 65536

extern dlink_list user_list;
extern dlink_list server_list;
extern dlink_list exited_list;

struct connection_entry;
struct service_command;
struct ucommand_handler;

struct client
{
	char name[HOSTLEN+1];
	char info[REALLEN+1];
	int flags;

	struct server *server;
	struct user *user;
	struct service *service;
	struct client *uplink;		/* server this is connected to */

	dlink_node nameptr;		/* dlink_node in name_table */
	dlink_node listnode;		/* in client/server/exited_list */
	dlink_node upnode;		/* in uplinks servers/clients list */
};

struct user
{
	char username[USERLEN+1];
	char host[HOSTLEN+1];
	char *servername;		/* name of server its on */
	char *mask;

	int umode;			/* usermodes this client has */
	time_t tsinfo;

	struct user_reg *user_reg;
	struct conf_oper *oper;

	dlink_list channels;

	dlink_node servptr;
	dlink_node hostptr;
	dlink_node uhostptr;
};

struct server
{
	dlink_list users;
	dlink_list servers;

	int hops;
};

struct service
{
	char username[USERLEN+1];
	char host[HOSTLEN+1];
	char id[NICKLEN+1];
	int flags;

	dlink_list channels;		/* the channels this service is in */

	FILE *logfile;

	int flood;
        int flood_max;
        int flood_max_ignore;

	int reintroduce;		/* needs reintroducing */

	struct service_command *command;
        struct ucommand_handler *ucommand;

        unsigned long help_count;
        unsigned long ehelp_count;
        unsigned long paced_count;
        unsigned long ignored_count;

	dlink_list ucommand_list;

        void (*stats)(struct connection_entry *, char *parv[], int parc);
};

#define IsServer(x) ((x)->server != NULL)
#define IsUser(x) ((x)->user != NULL)
#define IsService(x) ((x)->service != NULL)

#define FLAGS_DEAD	0x0001

#define IsDead(x)	((x) && (x)->flags & FLAGS_DEAD)
#define SetDead(x)	((x)->flags |= FLAGS_DEAD)

#define CLIENT_INVIS	0x001
#define CLIENT_OPER	0x002
#define CLIENT_ADMIN	0x004

#define ClientInvis(x)	 ((x)->user && (x)->user->umode & CLIENT_INVIS)
#define ClientOper(x)	 ((x)->user && (x)->user->umode & CLIENT_OPER)
#define is_oper(x)       ((x)->user && (x)->user->umode & CLIENT_OPER)
#define ClientAdmin(x)	 ((x)->user && (x)->user->umode & CLIENT_ADMIN)

#define SERVICE_OPERED	0x001
#define SERVICE_MSGSELF	0x002

#define ServiceOpered(x)	((x)->service && (x)->service->flags & SERVICE_OPERED)
#define ServiceMsgSelf(x)	((x)->service && (x)->service->flags & SERVICE_MSGSELF)

extern void init_client(void);

unsigned int hash_name(const char *p);

extern void add_client(struct client *target_p);
extern void del_client(struct client *target_p);
extern struct client *find_client(const char *name);
extern struct client *find_user(const char *name);
extern struct client *find_server(const char *name);
extern struct client *find_service(const char *name);

extern void exit_client(struct client *target_p);
extern void free_client(struct client *target_p);

extern int string_to_umode(const char *p, int current_umode);
extern const char *umode_to_string(int umode);
