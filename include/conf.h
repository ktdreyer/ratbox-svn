/* $Id$ */
#ifndef INCLUDED_conf_h
#define INCLUDED_conf_h

struct connection_entry;
struct FileBuf;

#define MYNAME config_file.name

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

	char *admin1;
	char *admin2;
	char *admin3;

	/* userserv */
	int disable_uregister;

	/* chanserv */
	int disable_cregister;

	/* jupeserv */
	int oper_score;
	int admin_score;
	int jupe_score;
	int unjupe_score;
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
#define CONF_OPER_ADMIN		0x0100
#define CONF_OPER_SADMIN	0x0200

#define OperAdmin(x)	((x)->privs & CONF_OPER_ADMIN)
#define CliOperAdmin(x)	((x)->user->oper && (x)->user->oper->flags & CONF_OPER_ADMIN)
#define OperSAdminx(x)	((x)->privs & CONF_OPER_SADMIN)


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

extern void free_conf_server(struct conf_server *conf_p);

extern struct conf_server *find_conf_server(const char *name);

extern struct conf_oper *find_conf_oper(const char *username, const char *host,
					const char *server);

#endif
