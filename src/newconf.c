/* This code is in the public domain.
 * $Id$
 */

#include "stdinc.h"
#include "newconf.h"
#include "rserv.h"
#include "client.h"
#include "tools.h"
#include "log.h"
#include "conf.h"
#include "service.h"

#define CF_TYPE(x) ((x) & CF_MTYPE)

struct TopConf *conf_cur_block;
static char *conf_cur_block_name;

static dlink_list conf_items;

struct conf_server *yy_server;
struct conf_oper *yy_oper;
struct conf_oper *yy_tmpoper;
static dlink_list yy_oper_list;

struct client *yy_service;

struct mode_table
{
	const char *name;
	int mode;
};

#if 0
static struct mode_table umode_table[] = {
	{"bots", UMODE_BOTS},
	{"cconn", UMODE_CCONN},
	{NULL}
};
#endif

static const char *
conf_strtype(int type)
{
	switch (type & CF_MTYPE)
	{
	case CF_INT:
		return "integer value";
	case CF_STRING:
		return "unquoted string";
	case CF_YESNO:
		return "yes/no value";
	case CF_QSTRING:
		return "quoted string";
	case CF_TIME:
		return "time/size value";
	default:
		return "unknown type";
	}
}

int
add_top_conf(const char *name, int (*sfunc) (struct TopConf *), int (*efunc) (struct TopConf *))
{
	struct TopConf *tc;

	tc = my_malloc(sizeof(struct TopConf));

	tc->tc_name = my_strdup(name);
	tc->tc_sfunc = sfunc;
	tc->tc_efunc = efunc;

	dlink_add_alloc(tc, &conf_items);
	return 0;
}

static struct TopConf *
find_top_conf(const char *name)
{
	dlink_node *d;
	struct TopConf *tc;

	DLINK_FOREACH(d, conf_items.head)
	{
		tc = d->data;
		if(strcasecmp(tc->tc_name, name) == 0)
			return tc;
	}

	return NULL;
}

static struct ConfEntry *
find_conf_item(const struct TopConf *top, const char *name)
{
	dlink_node *d;
	struct ConfEntry *cf;

	DLINK_FOREACH(d, top->tc_items.head)
	{
		cf = d->data;
		if(strcasecmp(cf->cf_name, name) == 0)
			return cf;
	}

	return NULL;
}

#if 0
static int
find_umode(struct mode_table *tab, char *name)
{
	int i;

	for (i = 0; tab[i].name; i++)
	{
		if(strcmp(tab[i].name, name) == 0)
			return tab[i].mode;
	}

	return 0;
}

static void
set_modes_from_table(int *modes, const char *whatis, struct mode_table *tab, conf_parm_t * args)
{
	for (; args; args = args->next)
	{
		int mode;

		if((args->type & CF_MTYPE) != CF_STRING)
		{
			conf_report_error("Warning -- %s is not a string; ignoring.", whatis);
			continue;
		}

		mode = find_umode(tab, args->v.string);

		if(!mode)
		{
			conf_report_error("Warning -- unknown %s %s.", whatis, args->v.string);
			continue;
		}

		*modes |= mode;
	}
}
#endif

void
conf_report_error(const char *fmt, ...)
{
	va_list ap;
	char msg[BUFSIZE + 1] = { 0 };
	extern char *current_file;

	va_start(ap, fmt);
	vsnprintf(msg, BUFSIZE, fmt, ap);
	va_end(ap);

	slog("conf error: \"%s\", line %d: %s", current_file, lineno + 1, msg);
}

int
conf_start_block(const char *block, const char *name)
{
	if((conf_cur_block = find_top_conf(block)) == NULL)
	{
		conf_report_error("Configuration block '%s' is not defined.", block);
		return -1;
	}

	if(name)
		conf_cur_block_name = my_strdup(name);
	else
		conf_cur_block_name = NULL;

	if(conf_cur_block->tc_sfunc)
		if(conf_cur_block->tc_sfunc(conf_cur_block) < 0)
			return -1;

	return 0;
}

int
conf_end_block(struct TopConf *tc)
{
	if(tc->tc_efunc)
		return tc->tc_efunc(tc);

	my_free(conf_cur_block_name);
	return 0;
}

int
conf_call_set(struct TopConf *tc, const char *item, conf_parm_t * value, int type)
{
	struct ConfEntry *cf;
	conf_parm_t *cp;

	if(!tc)
		return -1;

	if((cf = find_conf_item(tc, item)) == NULL)
	{
		conf_report_error("Non-existant configuration setting %s::%s.",
				  tc->tc_name, (char *) item);
		return -1;
	}

	/* if it takes one thing, make sure they only passed one thing,
	   and handle as needed. */
	if(value->type & CF_FLIST && !cf->cf_type & CF_FLIST)
	{
		conf_report_error("Option %s::%s does not take a list of values.",
				  tc->tc_name, item);
		return -1;
	}

	cp = value->v.list;


	if(CF_TYPE(value->v.list->type) != CF_TYPE(cf->cf_type))
	{
		/* if it expects a string value, but we got a yesno, 
		 * convert it back
		 */
		if((CF_TYPE(value->v.list->type) == CF_YESNO) &&
		   (CF_TYPE(cf->cf_type) == CF_STRING))
		{
			value->v.list->type = CF_STRING;

			if(cp->v.number == 1)
				cp->v.string = my_strdup("yes");
			else
				cp->v.string = my_strdup("no");
		}

		/* maybe it's a CF_TIME and they passed CF_INT --
		   should still be valid */
		else if(!((CF_TYPE(value->v.list->type) == CF_INT) &&
			  (CF_TYPE(cf->cf_type) == CF_TIME)))
		{
			conf_report_error("Wrong type for %s::%s (expected %s, got %s)",
					  tc->tc_name, (char *) item, conf_strtype(cf->cf_type),
					  conf_strtype(value->v.list->type));
			return -1;
		}
	}

	if(cf->cf_type & CF_FLIST)
	{
		/* just pass it the extended argument list */
		cf->cf_func(value->v.list);
	}
	else
	{
		/* it's old-style, needs only one arg */
		switch (cf->cf_type)
		{
		case CF_INT:
		case CF_TIME:
		case CF_YESNO:
			cf->cf_func(&cp->v.number);
			break;
		case CF_STRING:
		case CF_QSTRING:
			cf->cf_func(cp->v.string);
			break;
		}
	}


	return 0;
}

int
add_conf_item(const char *topconf, const char *name, int type, void (*func) (void *))
{
	struct TopConf *tc;
	struct ConfEntry *cf;

	if((tc = find_top_conf(topconf)) == NULL)
		return -1;

	if((cf = find_conf_item(tc, name)) != NULL)
		return -1;

	cf = my_malloc(sizeof(struct ConfEntry));

	cf->cf_name = my_strdup(name);
	cf->cf_type = type;
	cf->cf_func = func;

	dlink_add_alloc(cf, &tc->tc_items);

	return 0;
}

static void
split_user_host(const char *userhost, const char **user, const char **host)
{
        static const char star[] = "*";
        static char uh[USERHOSTLEN+1];
        char *p;

        strlcpy(uh, userhost, sizeof(uh));

        if((p = strchr(uh, '@')) != NULL)
        {
                *p++ = '\0';
                *user = &uh[0];
                *host = p;
        }
        else
        {
                *user = star;
                *host = userhost;
        }
}


static void
conf_set_serverinfo_name(void *data)
{
        my_free(config_file.name);
        config_file.name = my_strdup(data);
}

static void
conf_set_serverinfo_description(void *data)
{
        my_free(config_file.gecos);
        config_file.gecos = my_strdup(data);
}

static void
conf_set_serverinfo_vhost(void *data)
{
        my_free(config_file.vhost);
        config_file.vhost = my_strdup(data);
}

static void
conf_set_serverinfo_dcc_vhost(void *data)
{
        my_free(config_file.dcc_vhost);
        config_file.dcc_vhost = my_strdup(data);
}

static void
conf_set_serverinfo_dcc_low_port(void *data)
{
	config_file.dcc_low_port = *(unsigned int *) data;
}

static void
conf_set_serverinfo_dcc_high_port(void *data)
{
	config_file.dcc_high_port = *(unsigned int *) data;
}

static void
conf_set_serverinfo_reconnect_time(void *data)
{
	config_file.reconnect_time = *(unsigned int *) data;
}

static void
conf_set_serverinfo_ping_time(void *data)
{
	config_file.ping_time = *(unsigned int *) data;
}

static void
conf_set_admin_name(void *data)
{
        my_free(config_file.admin1);
        config_file.admin1 = my_strdup(data);
}

static void
conf_set_admin_description(void *data)
{
        my_free(config_file.admin2);
        config_file.admin2 = my_strdup(data);
}

static void
conf_set_admin_email(void *data)
{
        my_free(config_file.admin3);
        config_file.admin3 = my_strdup(data);
}

static int
conf_begin_connect(struct TopConf *tc)
{
        if(yy_server != NULL)
        {
                my_free(yy_server);
                yy_server = NULL;
        }

        yy_server = my_malloc(sizeof(struct conf_server));
        yy_server->defport = 6667;
	yy_server->flags = CONF_SERVER_AUTOCONN;

        return 0;
}

static int
conf_end_connect(struct TopConf *tc)
{
        if(conf_cur_block_name != NULL)
                yy_server->name = my_strdup(conf_cur_block_name);

        if(EmptyString(yy_server->name) || EmptyString(yy_server->host) ||
           EmptyString(yy_server->pass))
        {
                conf_report_error("Ignoring connect block, missing fields.");
                my_free(yy_server);
                yy_server = NULL;
                return 0;
        }

        dlink_add_tail_alloc(yy_server, &conf_server_list);

        yy_server = NULL;
        return 0;
}

static void
conf_set_connect_host(void *data)
{
        my_free(yy_server->host);
        yy_server->host = my_strdup(data);
}

static void
conf_set_connect_password(void *data)
{
        my_free(yy_server->pass);
        yy_server->pass = my_strdup(data);
}

static void
conf_set_connect_vhost(void *data)
{
        my_free(yy_server->vhost);
        yy_server->vhost = my_strdup(data);
}

static void
conf_set_connect_port(void *data)
{
        yy_server->defport = *(unsigned int *) data;
}

static void
conf_set_connect_autoconn(void *data)
{
	int yesno = *(unsigned int *) data;

	if(yesno)
		yy_server->flags |= CONF_SERVER_AUTOCONN;
	else
		yy_server->flags &= ~CONF_SERVER_AUTOCONN;
}

static int
conf_begin_oper(struct TopConf *tc)
{
        dlink_node *ptr;
        dlink_node *next_ptr;

        if(yy_oper != NULL)
        {
                free_conf_oper(yy_oper);
                yy_oper = NULL;
        }

        DLINK_FOREACH_SAFE(ptr, next_ptr, yy_oper_list.head)
        {
                free_conf_oper(ptr->data);
                dlink_destroy(ptr, &yy_oper_list);
        }

        yy_oper = my_malloc(sizeof(struct conf_oper));
        yy_oper->flags |= CONF_OPER_ENCRYPTED|CONF_OPER_DCC;

        return 0;
}

static int
conf_end_oper(struct TopConf *tc)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

        if(conf_cur_block_name != NULL)
                yy_oper->name = my_strdup(conf_cur_block_name);

        if(EmptyString(yy_oper->name) || EmptyString(yy_oper->pass))
                return 0;

	DLINK_FOREACH_SAFE(ptr, next_ptr, yy_oper_list.head)
	{
		yy_tmpoper = ptr->data;
		yy_tmpoper->name = my_strdup(yy_oper->name);
		yy_tmpoper->pass = my_strdup(yy_oper->pass);
		yy_tmpoper->flags = yy_oper->flags;

		dlink_add_tail_alloc(yy_tmpoper, &conf_oper_list);
		dlink_destroy(ptr, &yy_oper_list);
	}

	free_conf_oper(yy_oper);
        yy_oper = NULL;

        return 0;
}

static void
conf_set_oper_user(void *data)
{
	const char *username;
	const char *host;

	yy_tmpoper = my_malloc(sizeof(struct conf_oper));

	split_user_host((char *) data, &username, &host);
	yy_tmpoper->username = my_strdup(username);
	yy_tmpoper->host = my_strdup(host);

        dlink_add_tail_alloc(yy_tmpoper, &yy_oper_list);
}

static void
conf_set_oper_password(void *data)
{
        my_free(yy_oper->pass);
        yy_oper->pass = my_strdup(data);
}

static void
conf_set_oper_encrypted(void *data)
{
        int yesno = *(unsigned int *) data;

        if(yesno)
                yy_oper->flags |= CONF_OPER_ENCRYPTED;
        else
                yy_oper->flags &= ~CONF_OPER_ENCRYPTED;
}

static void
conf_set_oper_dcc(void *data)
{
        int yesno = *(unsigned int *) data;

        if(yesno)
                yy_oper->flags |= CONF_OPER_DCC;
        else
                yy_oper->flags &= ~CONF_OPER_DCC;
}

static int
conf_begin_service(struct TopConf *tc)
{
	if(conf_cur_block_name == NULL)
	{
		conf_report_error("Ignoring service block, missing service id");
		yy_service = NULL;
	}

	yy_service = find_service_id(conf_cur_block_name);

	if(yy_service == NULL)
		conf_report_error("Ignoring service block, unknown service id %s",
				conf_cur_block_name);

	return 0;
}

static int
conf_end_service(struct TopConf *tc)
{
	yy_service = NULL;
	return 0;
}

static void
conf_set_service_nick(void *data)
{
	if(yy_service == NULL)
		return;

	del_client(yy_service);
	strlcpy(yy_service->name, (const char *) data,
		sizeof(yy_service->name));
	add_client(yy_service);
}

static void
conf_set_service_username(void *data)
{
	if(yy_service == NULL)
		return;

	strlcpy(yy_service->service->username, (const char *) data,
		sizeof(yy_service->service->username));
}

static void
conf_set_service_host(void *data)
{
	if(yy_service == NULL)
		return;

	strlcpy(yy_service->service->host, (const char *) data,
		sizeof(yy_service->service->host));
}

static void
conf_set_service_realname(void *data)
{
	if(yy_service == NULL)
		return;

	strlcpy(yy_service->info, (const char *) data,
		sizeof(yy_service->info));
}

void
newconf_init()
{
	add_top_conf("serverinfo", NULL, NULL);
	add_conf_item("serverinfo", "name", CF_QSTRING,
			conf_set_serverinfo_name);
        add_conf_item("serverinfo", "description", CF_QSTRING,
			conf_set_serverinfo_description);
        add_conf_item("serverinfo", "vhost", CF_QSTRING,
			conf_set_serverinfo_vhost);
        add_conf_item("serverinfo", "dcc_vhost", CF_QSTRING,
			conf_set_serverinfo_dcc_vhost);
	add_conf_item("serverinfo", "dcc_low_port", CF_INT,
			conf_set_serverinfo_dcc_low_port);
	add_conf_item("serverinfo", "dcc_high_port", CF_INT,
			conf_set_serverinfo_dcc_high_port);
	add_conf_item("serverinfo", "reconnect_time", CF_TIME,
			conf_set_serverinfo_reconnect_time);
	add_conf_item("serverinfo", "ping_time", CF_TIME,
			conf_set_serverinfo_ping_time);

        add_top_conf("admin", NULL, NULL);
        add_conf_item("admin", "name", CF_QSTRING,
                      conf_set_admin_name);
        add_conf_item("admin", "description", CF_QSTRING,
                      conf_set_admin_description);
        add_conf_item("admin", "email", CF_QSTRING,
                      conf_set_admin_email);

        add_top_conf("connect", conf_begin_connect, conf_end_connect);
        add_conf_item("connect", "host", CF_QSTRING,
                      conf_set_connect_host);
        add_conf_item("connect", "password", CF_QSTRING,
                      conf_set_connect_password);
        add_conf_item("connect", "vhost", CF_QSTRING,
                      conf_set_connect_vhost);
        add_conf_item("connect", "port", CF_INT,
                      conf_set_connect_port);
	add_conf_item("connect", "autoconn", CF_YESNO,
			conf_set_connect_autoconn);

        add_top_conf("oper", conf_begin_oper, conf_end_oper);
        add_conf_item("oper", "user", CF_QSTRING,
                      conf_set_oper_user);
        add_conf_item("oper", "password", CF_QSTRING,
                      conf_set_oper_password);
        add_conf_item("oper", "encrypted", CF_YESNO,
                      conf_set_oper_encrypted);
        add_conf_item("oper", "dcc", CF_YESNO,
                      conf_set_oper_dcc);

	add_top_conf("service", conf_begin_service, conf_end_service);
	add_conf_item("service", "nick", CF_QSTRING,
			conf_set_service_nick);
	add_conf_item("service", "username", CF_QSTRING,
			conf_set_service_username);
	add_conf_item("service", "host", CF_QSTRING,
			conf_set_service_host);
	add_conf_item("service", "realname", CF_QSTRING,
			conf_set_service_realname);
}
