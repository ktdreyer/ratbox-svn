/* src/u_stats.c
 *   Contains code for handling the 'stats' user command
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "c_init.h"
#include "rserv.h"
#include "conf.h"
#include "ucommand.h"
#include "io.h"

static void u_stats(struct connection_entry *, char *parv[], int parc);
struct ucommand_handler stats_ucommand = { "stats", u_stats, 0, NULL };

struct _stats_table
{
        const char *type;
        void (*func)(struct connection_entry *);
};

static void
stats_opers(struct connection_entry *conn_p)
{
        struct conf_oper *conf_p;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_oper_list.head)
        {
                conf_p = ptr->data;

                sendto_one(conn_p, "Oper %s %s@%s",
                           conf_p->name, conf_p->username, conf_p->host);
        }
}

static void
stats_servers(struct connection_entry *conn_p)
{
        struct conf_server *conf_p;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_server_list.head)
        {
                conf_p = ptr->data;

                sendto_one(conn_p, "Server %s/%d %s",
                           conf_p->name, abs(conf_p->defport),
                           (conf_p->defport > 0) ? "A" : "");
        }
}

static void
stats_uplink(struct connection_entry *conn_p)
{
        if(server_p != NULL)
                sendto_one(conn_p, "Currently connected to %s Idle: %d "
                           "SendQ: %d Connected: %s",
                           server_p->name,
                           (CURRENT_TIME - server_p->last_time), 
                           get_sendq(server_p),
                           get_duration(CURRENT_TIME - server_p->first_time));
        else
                sendto_one(conn_p, "Currently disconnected");
}

static void
stats_uptime(struct connection_entry *conn_p)
{
        sendto_one(conn_p, "%s up %s",
                   MYNAME,
                   get_duration(CURRENT_TIME - first_time));
}

static struct _stats_table stats_table[] =
{
        { "opers",      &stats_opers,   },
        { "servers",    &stats_servers, },
        { "uplink",     &stats_uplink,  },
        { "uptime",     &stats_uptime,  },
        { "\0",         NULL,           }
};

static void
u_stats(struct connection_entry *conn_p, char *parv[], int parc)
{
        int i;

        if(parc < 2 || EmptyString(parv[1]))
        {
                sendto_one(conn_p, "Usage: .stats <type>");
                return;
        }

        for(i = 0; stats_table[i].type[0] != '\0'; i++)
        {
                if(!strcasecmp(stats_table[i].type, parv[1]))
                {
                        (stats_table[i].func)(conn_p);
                        return;
                }
        }

        sendto_one(conn_p, "Unknown stats type: %s", parv[1]);
};
        
