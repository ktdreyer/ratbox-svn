/* src/u_stats.c
 *   Contains code for handling the 'stats' user command
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "c_init.h"
#include "rserv.h"
#include "conf.h"
#include "ucommand.h"
#include "io.h"

static int u_stats(struct client *, struct lconn *, const char **, int);
struct ucommand_handler stats_ucommand = { "stats", u_stats, 0, 0, 1, NULL };

struct _stats_table
{
        const char *type;
        void (*func)(struct lconn *);
};

static void
stats_opers(struct lconn *conn_p)
{
        struct conf_oper *conf_p;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_oper_list.head)
        {
                conf_p = ptr->data;

                sendto_one(conn_p, "Oper %s %s@%s %s %s",
                           conf_p->name, conf_p->username, conf_p->host,
			   conf_p->server ? conf_p->server : "-",
			   conf_oper_flags(conf_p->flags));
        }
}

static void
stats_servers(struct lconn *conn_p)
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
stats_uplink(struct lconn *conn_p)
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
stats_uptime(struct lconn *conn_p)
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

static int
u_stats(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
        int i;

	if(parc < 1)
	{
		char buf[BUFSIZE];

		buf[0] = '\0';

		for(i = 0; stats_table[i].type[0] != '\0'; i++)
		{
			strlcat(buf, stats_table[i].type, sizeof(buf));
			strlcat(buf, " ", sizeof(buf));
		}

		sendto_one(conn_p, "Stats types: %s", buf);

		return 0;
	}

        for(i = 0; stats_table[i].type[0] != '\0'; i++)
        {
                if(!strcasecmp(stats_table[i].type, parv[0]))
                {
                        (stats_table[i].func)(conn_p);
                        return 0;
                }
        }

        sendto_one(conn_p, "Unknown stats type: %s", parv[0]);
	return 0;
};
        
