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
struct cachefile;

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

	int loglevel;

	struct service_command *command;
        struct ucommand_handler *ucommand;

        unsigned long help_count;
        unsigned long ehelp_count;
        unsigned long paced_count;
        unsigned long ignored_count;

	dlink_list ucommand_list;

	struct cachefile *help;
	struct cachefile *helpadmin;
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

#define SERVICE_OPERED		0x001 /* service is opered */
#define SERVICE_MSGSELF		0x002 /* messages come from services nick */
#define SERVICE_DISABLED	0x004 /* should this service be disabled? */
#define SERVICE_INTRODUCED	0x008 /* service has been introduced */
#define SERVICE_REINTRODUCE	0x010 /* service needs reintroducing */
#define SERVICE_SHORTHELP	0x020 /* service gives short help */

#define ServiceOpered(x)	((x)->service->flags & SERVICE_OPERED)
#define ServiceMsgSelf(x)	((x)->service->flags & SERVICE_MSGSELF)
#define ServiceDisabled(x)	((x)->service->flags & SERVICE_DISABLED)
#define ServiceIntroduced(x)	((x)->service->flags & SERVICE_INTRODUCED)
#define ServiceReintroduce(x)	((x)->service->flags & SERVICE_REINTRODUCE)
#define ServiceShortHelp(x)	((x)->service->flags & SERVICE_SHORTHELP)

#define SetServiceIntroduced(x)	((x)->service->flags |= SERVICE_INTRODUCED)
#define SetServiceReintroduce(x) ((x)->service->flags |= SERVICE_REINTRODUCE)
#define ClearServiceIntroduced(x)  ((x)->service->flags &= ~SERVICE_INTRODUCED)
#define ClearServiceReintroduce(x) ((x)->service->flags &= ~SERVICE_REINTRODUCE)

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
