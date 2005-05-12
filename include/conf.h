/* $Id$ */
#ifndef INCLUDED_conf_h
#define INCLUDED_conf_h

struct lconn;
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

	unsigned int client_flood_time;
	unsigned int client_flood_ignore_time;
	unsigned int client_flood_max;
	unsigned int client_flood_max_ignore;

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

	/* nickserv */
	int nmax_nicks;
	int nallow_set_warn;
	char *nwarn_string;

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
        int flags;		/* general flags */
	int sflags;		/* individual service flags */
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

#define CONF_OPER_US_REGISTER	0x0000001
#define CONF_OPER_US_SUSPEND	0x0000002
#define CONF_OPER_US_DROP	0x0000004
#define CONF_OPER_US_LIST	0x0000008
#define CONF_OPER_US_INFO	0x0000010
#define CONF_OPER_US_SETPASS	0x0000020

#define CONF_OPER_US_OPER	(CONF_OPER_US_LIST|CONF_OPER_US_INFO)
#define CONF_OPER_US_ADMIN	(CONF_OPER_US_REGISTER|CONF_OPER_US_SUSPEND|CONF_OPER_US_DROP|\
				 CONF_OPER_US_SETPASS|CONF_OPER_US_OPER)

#define CONF_OPER_CS_REGISTER	0x0000100
#define CONF_OPER_CS_SUSPEND	0x0000200
#define CONF_OPER_CS_DROP	0x0000400
#define CONF_OPER_CS_LIST	0x0000800
#define CONF_OPER_CS_INFO	0x0001000

#define CONF_OPER_CS_OPER	(CONF_OPER_CS_LIST|CONF_OPER_CS_INFO)
#define CONF_OPER_CS_ADMIN	(CONF_OPER_CS_OPER|CONF_OPER_CS_REGISTER|CONF_OPER_CS_SUSPEND|\
				 CONF_OPER_CS_DROP)

#define CONF_OPER_NS_DROP	0x0010000

#define CONF_OPER_OS_CHANNEL	0x0080000
#define CONF_OPER_OS_TAKEOVER	0x0100000
#define CONF_OPER_OS_OMODE	0x0200000

#define CONF_OPER_OS_ADMIN	(CONF_OPER_OS_CHANNEL|CONF_OPER_OS_TAKEOVER|CONF_OPER_OS_OMODE)

#define CONF_OPER_OB_CHANNEL	0x1000000
#define CONF_OPER_GLOB_NETMSG	0x2000000
#define CONF_OPER_JS_JUPE	0x4000000

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
