/* src/ucommand.c
 *  Contains code for handling of commands received from local users.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
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

static dlink_list ucommand_table[MAX_UCOMMAND_HASH];

static void u_login(struct connection_entry *, char *parv[], int parc);

static void u_connect(struct connection_entry *, char *parv[], int parc);
static void u_die(struct connection_entry *, char *parv[], int parc);
static void u_quit(struct connection_entry *, char *parv[], int parc);
static void u_stats(struct connection_entry *, char *parv[], int parc);

static struct ucommand_handler connect_ucommand = { "connect", u_connect, 0 };
static struct ucommand_handler die_ucommand = { "die", u_die, 0 };
static struct ucommand_handler quit_ucommand = { "quit", u_quit, 0 };
static struct ucommand_handler stats_ucommand = { "stats", u_stats, 0 };

void
init_ucommand(void)
{
        add_ucommand_handler(&connect_ucommand);
        add_ucommand_handler(&die_ucommand);
	add_ucommand_handler(&quit_ucommand);
	add_ucommand_handler(&stats_ucommand);
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

void
handle_ucommand(struct connection_entry *conn_p, const char *command, 
		char *parv[], int parc)
{
	struct ucommand_handler *handler;
	dlink_node *ptr;
	unsigned int hashv = hash_command(command);

        /* people who arent logged in, can only do .login */
        if(conn_p->oper == NULL)
        {
                if(strcasecmp(command, "login"))
                {
                        sendto_connection(conn_p, "Please .login first");
                        return;
                }

                u_login(conn_p, parv, parc);
                return;
        }

	DLINK_FOREACH(ptr, ucommand_table[hashv].head)
	{
		handler = ptr->data;
		if(!strcasecmp(command, handler->cmd))
		{
			handler->func(conn_p, parv, parc);
			return;
		}
	}

        sendto_connection(conn_p, "Invalid command: %s", command);
}

void
add_ucommand_handler(struct ucommand_handler *chandler)
{
	unsigned int hashv;

	if(chandler == NULL || EmptyString(chandler->cmd))
		return;

	hashv = hash_command(chandler->cmd);
	dlink_add_alloc(chandler, &ucommand_table[hashv]);
}

static void
u_login(struct connection_entry *conn_p, char *parv[], int parc)
{
        struct conf_oper *oper_p;

        if(parc < 3 || EmptyString(parv[2]))
        {
                sendto_connection(conn_p, "Usage: .login <username> <password>");
                return;
        }

        if((oper_p = find_oper(conn_p, parv[1])) == NULL)
        {
                sendto_connection(conn_p, "No matching O: found");
                return;
        }

        if(strcmp(oper_p->pass, parv[2]))
        {
                sendto_connection(conn_p, "Invalid password");
                return;
        }

        /* newly opered user wont get this. */
        sendto_connections("%s has logged in", conn_p->name);

        /* set them as 'logged in' */
        conn_p->oper = oper_p;

        /* update our name with them from one in O: */
        my_free(conn_p->name);
        conn_p->name = my_strdup(oper_p->name);

        sendto_connection(conn_p, "Login successful, for available commands see .help");
}

static void
u_connect(struct connection_entry *conn_p, char *parv[], int parc)
{
        struct conf_server *conf_p;
        int port = 0;

        if(parc < 2 || EmptyString(parv[1]))
        {
                sendto_connection(conn_p, "Usage: .connect <server> [port]");
                return;
        }

        if((conf_p = find_conf_server(parv[1])) == NULL)
        {
                sendto_connection(conn_p, "No such server %s", parv[1]);
                return;
        }

        if(parc > 2)
        {
                if((port = atoi(parv[2])) <= 0)
                {
                        sendto_connection(conn_p, "Invalid port %s", parv[2]);
                        return;
                }

                conf_p->port = port;
        }
        else
                conf_p->port = abs(conf_p->defport);

        if(server_p != NULL && (server_p->flags & CONN_DEAD) == 0)
        {
                (server_p->io_close)(server_p);

                sendto_connections("Connection to server %s disconnected by "
                                   "%s: (reroute to %s)",
                                   server_p->name, conn_p->name, conf_p->name);
                slog("Connection to server %s disconnected by %s: "
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
        if(parc < 2 || EmptyString(parv[1]) ||
           strcasecmp(MYNAME, parv[1]))
        {
                sendto_connection(conn_p, "Usage: .die <servername>");
                return;
        }

        sendto_connections("Services terminated by %s", conn_p->name);
        slog("ratbox-services terminated by %s", conn_p->name);
        exit(0);
}

static void
u_quit(struct connection_entry *conn_p, char *parv[], int parc)
{
	sendto_connection(conn_p, "Goodbye.");
	(conn_p->io_close)(conn_p);
}

static void
u_stats(struct connection_entry *conn_p, char *parv[], int parc)
{
        struct client *service_p;

        if(parc < 2 || EmptyString(parv[1]))
        {
                sendto_connection(conn_p, "Usage: .stats <service>");
                return;
        }

        if((service_p = find_service_id(parv[1])) == NULL)
        {
                sendto_connection(conn_p, "No such service %s", parv[1]);
                return;
        }

        (service_p->service->stats)(conn_p, parv, parc);
}
