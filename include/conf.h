/* $Id$ */
#ifndef INCLUDED_conf_h
#define INCLUDED_conf_h

struct connection_entry;

#define MYNAME config_file.my_name

extern void conf_parse(void);

struct _config_file
{
	char *my_name;
	char *my_gecos;
	char *vhost;
	int server_port;

	char *admin1;
	char *admin2;
	char *admin3;

	time_t first_time;
};

struct conf_server
{
	char *name;
	char *host;
	char *pass;
	char *vhost;
	int port;
};

struct conf_oper
{
        char *name;
        char *username;
        char *host;
        char *pass;
};

extern struct _config_file config_file;
extern dlink_list conf_server_list;
extern dlink_list conf_oper_list;

extern struct conf_server *find_conf_server(const char *name);

extern struct conf_oper *find_oper(struct connection_entry *conn_p, const char *name);
extern int is_conf_oper(const char *username, const char *host);

#endif
