/* $Id$ */
#ifndef INCLUDED_conf_h
#define INCLUDED_conf_h

struct connection_entry;
struct FileBuf;

#define MYNAME config_file.name

#define DEFAULT_ENFORCETOPIC_FREQUENCY	3600
#define DEFAULT_EXPIREBAN_FREQUENCY	900

extern time_t first_time;

struct _config_file
{
	char *name;
	char *gecos;
	char *vhost;

	char *dcc_vhost;
	int dcc_low_port;
	int dcc_high_port;

	int reconnect_time;
	int ping_time;
	int ratbox;

	char *admin1;
	char *admin2;
	char *admin3;

	/* userserv */
	int disable_uregister;
	char *uregister_url;
	int uregister_time;		/* overall registrations */
	int uregister_amount;
	int uhregister_time;		/* per host registrations */
	int uhregister_amount;
	int uregister_email;
	int uexpire_time;
	int allow_set_password;
	int allow_set_email;
	int umax_logins;

	/* chanserv */
	int disable_cregister;
	int cregister_time;		/* overall registrations */
	int cregister_amount;
	int chregister_time;		/* per host registrations */
	int chregister_amount;
	int cexpire_time;
	int cmax_bans;
	int cexpireban_frequency;
	int cenforcetopic_frequency;

	/* jupeserv */
	int oper_score;
	int jupe_score;
	int unjupe_score;
	int pending_time;

	/* alis */
	int max_matches;
};

struct conf_server
{
	char *name;
	char *host;
	char *pass;
	char *vhost;
	int port;
        int defport;
	int flags;
        time_t last_connect;
};

struct conf_oper
{
        char *name;
        char *username;
        char *host;
        char *pass;
	char *server;
        int flags;
	int refcount;
};

#define CONF_DEAD		0x0001

#define ConfDead(x)		((x)->flags & CONF_DEAD)
#define SetConfDead(x)		((x)->flags |= CONF_DEAD)
#define ClearConfDead(x)	((x)->flags &= ~CONF_DEAD)

#define CONF_OPER_ENCRYPTED     0x0010
#define CONF_OPER_DCC		0x0020

/* x is an oper_p */
#define ConfOperEncrypted(x)	((x)->flags & CONF_OPER_ENCRYPTED)
#define ConfOperDcc(x)		((x)->flags & CONF_OPER_DCC)

/* set in conf, but are moved to ->privs, x here is a connection */
#define CONF_OPER_ADMIN		0x0000100
#define CONF_OPER_ROUTE		0x0000200

#define CONF_OPER_CHANSERV	0x0001000
#define CONF_OPER_CREGISTER	0x0002000
#define CONF_OPER_USERSERV	0x0004000
#define CONF_OPER_UREGISTER	0x0008000

#define CONF_OPER_OPERSERV	0x0010000

#define CONF_OPER_OPERBOT	0x0100000
#define CONF_OPER_JUPESERV	0x0200000
#define CONF_OPER_GLOBAL	0x0400000

#define OperAdmin(x)	((x)->privs & CONF_OPER_ADMIN)
#define CliOperAdmin(x)	((x)->user->oper && (x)->user->oper->flags & CONF_OPER_ADMIN)
#define OperRoute(x)	((x)->privs & CONF_OPER_ROUTE)

#define OperCSAdmin(x)		((x)->privs & CONF_OPER_CHANSERV)
#define CliOperCSAdmin(x)	((x)->user->oper && (x)->user->oper->flags & CONF_OPER_CHANSERV)
#define OperCSRegister(x)	((x)->privs & CONF_OPER_CREGISTER)
#define CliOperCSRegister(x)	((x)->user->oper && (x)->user->oper->flags & CONF_OPER_CREGISTER)

#define OperUSAdmin(x)		((x)->privs & CONF_OPER_USERSERV)
#define CliOperUSAdmin(x)	((x)->user->oper && (x)->user->oper->flags & CONF_OPER_USERSERV)
#define OperUSRegister(x)	((x)->privs & CONF_OPER_UREGISTER)
#define CliOperUSRegister(x)	((x)->user->oper && (x)->user->oper->flags & CONF_OPER_UREGISTER)

#define CONF_SERVER_AUTOCONN	0x0001

#define ConfServerAutoconn(x)	((x)->flags & CONF_SERVER_AUTOCONN)

extern struct _config_file config_file;
extern dlink_list conf_server_list;
extern dlink_list conf_oper_list;
extern FILE *conf_fbfile_in;

extern void conf_parse(int cold);

extern int lineno;
extern void yyerror(const char *msg);
extern int conf_fbgets(char *lbuf, int max_size);

extern void rehash(int sig);

extern void free_conf_oper(struct conf_oper *conf_p);
extern void deallocate_conf_oper(struct conf_oper *conf_p);
extern const char *conf_oper_flags(int flags);

extern void free_conf_server(struct conf_server *conf_p);

extern struct conf_server *find_conf_server(const char *name);

extern struct conf_oper *find_conf_oper(const char *username, const char *host,
					const char *server);

#endif
