/* $Id$ */
#ifndef INCLUDED_conf_h
#define INCLUDED_conf_h

struct connection_entry;
struct FileBuf;

#define MYNAME config_file.name

extern void conf_parse(void);
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
        int flags;
};

#define CONF_OPER_ENCRYPTED     0x0001
#define CONF_OPER_DCC		0x0002

#define ConfOperEncrypted(x)	((x)->flags & CONF_OPER_ENCRYPTED)
#define ConfOperDcc(x)		((x)->flags & CONF_OPER_DCC)

#define CONF_SERVER_AUTOCONN	0x0001

#define ConfServerAutoconn(x)	((x)->flags & CONF_SERVER_AUTOCONN)

extern struct _config_file config_file;
extern dlink_list conf_server_list;
extern dlink_list conf_oper_list;
extern struct FileBuf *conf_fbfile_in;

extern int lineno;
extern void yyerror(const char *msg);
extern int conf_fbgets(char *lbuf, int max_size);

extern void free_conf_oper(struct conf_oper *conf_p);
extern struct conf_server *find_conf_server(const char *name);

extern struct conf_oper *find_oper(struct connection_entry *conn_p, const char *name);
extern struct conf_oper *find_conf_oper(const char *username, const char *host);

#endif
