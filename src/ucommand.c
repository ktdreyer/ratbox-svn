/* src/ucommand.c
 *   Contains code for handling of commands received from local users.
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "ucommand.h"
#include "rserv.h"
#include "tools.h"
#include "io.h"
#include "log.h"
#include "conf.h"
#include "event.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "serno.h"
#include "cache.h"

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#define DIR_ADD 1
#define DIR_DEL 2
#define MAX_HELP_ROW 8

static dlink_list ucommand_table[MAX_UCOMMAND_HASH];
dlink_list ucommand_list;

static void u_login(struct connection_entry *, char *parv[], int parc);

static void u_connect(struct connection_entry *, char *parv[], int parc);
static void u_die(struct connection_entry *, char *parv[], int parc);
static void u_events(struct connection_entry *, char *parv[], int parc);
static void u_flags(struct connection_entry *conn_p, char *parv[], int parc);
static void u_help(struct connection_entry *, char *parv[], int parc);
static void u_quit(struct connection_entry *, char *parv[], int parc);
static void u_rehash(struct connection_entry *, char *parv[], int parc);
static void u_service(struct connection_entry *, char *parv[], int parc);
static void u_status(struct connection_entry *, char *parv[], int parc);

static struct ucommand_handler ucommands[] =
{
	{ "connect",	u_connect,	CONF_OPER_ROUTE,	2, NULL },
	{ "die",	u_die,		CONF_OPER_ADMIN,	2, NULL },
	{ "events",	u_events,	CONF_OPER_ADMIN,	0, NULL },
	{ "flags",	u_flags,	0,			0, NULL },
	{ "help",	u_help,		0,			0, NULL },
	{ "quit",	u_quit,		0,			0, NULL },
	{ "rehash",	u_rehash,	CONF_OPER_ADMIN,	0, NULL },
	{ "service",	u_service,	0,			0, NULL },
	{ "status",	u_status,	0,			0, NULL },
	{ "\0",         NULL,		0,			0, NULL }
};

void
init_ucommand(void)
{
        add_ucommands(NULL, ucommands, NULL);
}

static int
hash_command(const char *p)
{
	unsigned int hash_val = 0;

	while(*p)
	{
		hash_val += ((int) (*p) & 0xDF);
		p++;
	}

	return(hash_val % MAX_UCOMMAND_HASH);
}

struct ucommand_handler *
find_ucommand(const char *command)
{
        struct ucommand_handler *handler;
        dlink_node *ptr;
	unsigned int hashv = hash_command(command);

	DLINK_FOREACH(ptr, ucommand_table[hashv].head)
	{
		handler = ptr->data;

		if(!strcasecmp(command, handler->cmd))
			return handler;
	}

        return NULL;
}

void
handle_ucommand(struct connection_entry *conn_p, const char *command, 
		char *parv[], int parc)
{
	struct ucommand_handler *handler;

        /* people who arent logged in, can only do .login */
        if(!UserAuth(conn_p))
        {
                if(strcasecmp(command, "login"))
                {
                        sendto_one(conn_p, "You must .login first");
                        return;
                }

                u_login(conn_p, parv, parc);
                return;
        }

        if((handler = find_ucommand(command)) != NULL)
	{
		if(parc < handler->minpara)
			sendto_one(conn_p, "Insufficient parameters");
		else if(!handler->flags || conn_p->privs & handler->flags)
			handler->func(conn_p, parv, parc);
		else
			sendto_one(conn_p, "Insufficient access");
	}
        else
                sendto_one(conn_p, "Invalid command: %s", command);
}

void
add_ucommand_handler(struct client *service_p, 
		struct ucommand_handler *chandler, const char *servicename)
{
        static char def_servicename[] = "main";
        char filename[PATH_MAX];
	unsigned int hashv;

	if(chandler == NULL || EmptyString(chandler->cmd))
		return;

	hashv = hash_command(chandler->cmd);
	dlink_add_alloc(chandler, &ucommand_table[hashv]);

        if(servicename == NULL)
	{
		dlink_add_tail_alloc(chandler, &ucommand_list);
                servicename = def_servicename;
	}
	else
		dlink_add_tail_alloc(chandler, &service_p->service->ucommand_list);

        /* now see if we can load a helpfile.. */
        snprintf(filename, sizeof(filename), "%s%s/u-",
                 HELP_PATH, lcase(servicename));
        strlcat(filename, lcase(chandler->cmd), sizeof(filename));

        chandler->helpfile = cache_file(filename, chandler->cmd);
}

void
add_ucommands(struct client *service_p, 
		struct ucommand_handler *handler, const char *servicename)
{
        int i;

        for(i = 0; handler[i].cmd[0] != '\0'; i++)
        {
                add_ucommand_handler(service_p, &handler[i], servicename);
        }
}

static void
u_login(struct connection_entry *conn_p, char *parv[], int parc)
{
	struct conf_oper *oper_p = conn_p->oper;
        char *crpass;

        if(parc < 3 || EmptyString(parv[2]))
        {
                sendto_one(conn_p, "Usage: .login <username> <password>");
                return;
        }

        if(ConfOperEncrypted(oper_p))
                crpass = crypt(parv[2], oper_p->pass);
        else
                crpass = parv[2];

        if(strcmp(oper_p->pass, crpass))
        {
                sendto_one(conn_p, "Invalid password");
                return;
        }

        /* update our name with them from one in O: */
        my_free(conn_p->name);
        conn_p->name = my_strdup(oper_p->name);

        /* newly opered user wont get this. */
        sendto_all(UMODE_AUTH, "%s has logged in", conn_p->name);

        /* set them as 'logged in' */
        SetUserAuth(conn_p);
        conn_p->flags |= UMODE_DEFAULT;
	conn_p->privs = oper_p->flags;

        sendto_one(conn_p, "Login successful, for available commands see .help");

	deallocate_conf_oper(oper_p);
	conn_p->oper = NULL;
}

static void
u_connect(struct connection_entry *conn_p, char *parv[], int parc)
{
        struct conf_server *conf_p;
        int port = 0;

        if((conf_p = find_conf_server(parv[1])) == NULL)
        {
                sendto_one(conn_p, "No such server %s", parv[1]);
                return;
        }

        if(parc > 2)
        {
                if((port = atoi(parv[2])) <= 0)
                {
                        sendto_one(conn_p, "Invalid port %s", parv[2]);
                        return;
                }

                conf_p->port = port;
        }
        else
                conf_p->port = abs(conf_p->defport);

        if(server_p != NULL && (server_p->flags & CONN_DEAD) == 0)
        {
                (server_p->io_close)(server_p);

                sendto_all(UMODE_SERVER, "Connection to server %s "
                           "disconnected by %s: (reroute to %s)",
                           server_p->name, conn_p->name, conf_p->name);
                mlog("Connection to server %s disconnected by %s: "
                     "(reroute to %s)",
                     server_p->name, conn_p->name, conf_p->name);
        }

        /* remove any pending events for connecting.. */
        eventDelete(connect_to_server, NULL);

        eventAddOnce("connect_to_server", connect_to_server, conf_p, 2);
}

static void
u_die(struct connection_entry *conn_p, char *parv[], int parc)
{
        if(strcasecmp(MYNAME, parv[1]))
        {
                sendto_one(conn_p, "Usage: .die <servername>");
                return;
        }

        sendto_all(0, "Services terminated by %s", conn_p->name);
        mlog("ratbox-services terminated by %s", conn_p->name);
        die("Services terminated");
}

static void
u_events(struct connection_entry *conn_p, char *parv[], int parc)
{
        event_show(conn_p);
}

static void
u_quit(struct connection_entry *conn_p, char *parv[], int parc)
{
	sendto_one(conn_p, "Goodbye.");
	(conn_p->io_close)(conn_p);
}

static void
u_rehash(struct connection_entry *conn_p, char *parv[], int parc)
{
	mlog("services rehashing: %s reloading config file", conn_p->name);
	sendto_all(0, "services rehashing: %s reloading config file", conn_p->name);

	rehash(0);
}

static void
u_service(struct connection_entry *conn_p, char *parv[], int parc)
{
        struct client *service_p;
        dlink_node *ptr;

        if(parc > 1)
        {
                if((service_p = find_service_id(parv[1])) == NULL)
                {
                        sendto_one(conn_p, "No such service %s", parv[1]);
                        return;
                }

                service_stats(service_p, conn_p);

                if(service_p->service->stats != NULL)
                        (service_p->service->stats)(conn_p, parv, parc);
                return;
        }

        sendto_one(conn_p, "Services:");

        DLINK_FOREACH(ptr, service_list.head)
        {
                service_p = ptr->data;

                sendto_one(conn_p, "  %s: Online as %s!%s@%s [%s]",
                           service_p->service->id,service_p->name,
                           service_p->service->username,
                           service_p->service->host, service_p->info);
        }
}

static void
u_status(struct connection_entry *conn_p, char *parv[], int parc)
{
        sendto_one(conn_p, "%s, version ratbox-services-%s(%s), up %s",
                   MYNAME, RSERV_VERSION, SERIALNUM,
                   get_duration(CURRENT_TIME - first_time));

        if(server_p != NULL)
                sendto_one(conn_p, "Currently connected to %s", server_p->name);
        else
                sendto_one(conn_p, "Currently disconnected");

        sendto_one(conn_p, "Services: Clients: %lu Services: %lu",
                   dlink_list_length(&connection_list),
                   dlink_list_length(&service_list));
        sendto_one(conn_p, "Network: Users: %lu Servers: %lu",
                   dlink_list_length(&user_list),
                   dlink_list_length(&server_list));
        sendto_one(conn_p, "         Channels: %lu Topics: %lu",
                   dlink_list_length(&channel_list), count_topics());
                          
}

static void
dump_commands(struct connection_entry *conn_p, struct client *service_p, dlink_list *list)
{
	struct ucommand_handler *handler;
	const char *hparv[MAX_HELP_ROW];
	dlink_node *ptr;
	int j = 0;
	int header = 0;

	DLINK_FOREACH(ptr, list->head)
	{
		handler = ptr->data;

		if(handler->flags && !(conn_p->privs & handler->flags))
			continue;

		if(!header)
		{
			header++;
			sendto_one(conn_p, "%s commands:",
					service_p ? ucase(service_p->name) : "Available");
		}

		hparv[j] = handler->cmd;
		j++;

		if(j >= MAX_HELP_ROW)
		{
			sendto_one(conn_p,
				"   %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s",
				hparv[0], hparv[1], hparv[2], hparv[3],
				hparv[4], hparv[5], hparv[6], hparv[7]);
			j = 0;
		}
	}

	if(j)
	{
		char buf[BUFSIZE];
		char *p = buf;
		int i;

		for(i = 0; i < j; i++)
		{
			p += sprintf(p, "%-8s ", hparv[i]);
		}

		sendto_one(conn_p, "   %s", buf);
	}
}

static void
u_help(struct connection_entry *conn_p, char *parv[], int parc)
{
        struct ucommand_handler *handler;
	dlink_node *ptr;

        if(parc < 2 || EmptyString(parv[1]))
        {
		struct client *service_p;

		dump_commands(conn_p, NULL, &ucommand_list);

		DLINK_FOREACH(ptr, service_list.head)
		{
			service_p = ptr->data;

			dump_commands(conn_p, service_p, &service_p->service->ucommand_list);
		}

                sendto_one(conn_p, "For more information see .help <command>");
                return;
        }

        if((handler = find_ucommand(parv[1])) != NULL)
        {
                if(handler->helpfile == NULL)
                        sendto_one(conn_p, "No help available on %s", parv[1]);
                else
                        send_cachefile(handler->helpfile, conn_p);

                return;
        }

        sendto_one(conn_p, "Unknown help topic: %s", parv[1]);
}

struct _flags_table
{
        const char *name;
        int flag;
};
static struct _flags_table flags_table[] = {
        { "chat",       UMODE_CHAT,     },
        { "auth",       UMODE_AUTH,     },
        { "server",     UMODE_SERVER,   },
        { "\0",         0,              }
};

static void
show_flags(struct connection_entry *conn_p)
{
        char buf[BUFSIZE];
        char *p = buf;
        int i;

        for(i = 0; flags_table[i].flag; i++)
        {
                p += sprintf(p, "%s%s ",
                             (conn_p->flags & flags_table[i].flag) ? "+" : "-",
                             flags_table[i].name);
        }

        sendto_one(conn_p, "Current flags: %s", buf);
}

static void
u_flags(struct connection_entry *conn_p, char *parv[], int parc)
{
        const char *param;
        int dir;
        int i;
        int j;

        if(parc < 2)
        {
                show_flags(conn_p);
                return;
        }

        for(i = 1; i < parc; i++)
        {
                param = parv[i];

                if(*param == '+')
                {
                        dir = DIR_ADD;
                        param++;
                }
                else if(*param == '-')
                {
                        dir = DIR_DEL;
                        param++;
                }
                else
                        dir = DIR_ADD;

                for(j = 0; flags_table[j].flag; j++)
                {
                        if(!strcasecmp(flags_table[j].name, param))
                        {
                                if(dir == DIR_ADD)
                                        conn_p->flags |= flags_table[j].flag;
                                else
                                        conn_p->flags &= ~flags_table[j].flag;
                                break;
                        }
                }
        }

        show_flags(conn_p);
}
