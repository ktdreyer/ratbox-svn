#ifndef INCLUDED_conf_h
#define INCLUDED_conf_h

#define MYNAME config_file.my_name

extern void conf_parse(void);

struct _config_file
{
	char *my_name;
	char *my_gecos;
	int server_port;

	time_t first_time;
};

struct conf_server
{
	char *name;
	char *host;
	char *rpass;
	char *spass;
	int port;
};

extern struct _config_file config_file;
extern dlink_list conf_server_list;

#endif
