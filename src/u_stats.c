/* $Id$ */
#include "stdinc.h"
#include "stats.h"
#include "c_init.h"
#include "rserv.h"
#include "conf.h"
#include "ucommand.h"
#include "io.h"

static void u_stats(struct connection_entry *, char *parv[], int parc);
struct ucommand_handler stats_ucommand = { "stats", u_stats, 0 };

const char *
get_duration(time_t seconds)
{
        static char buf[BUFSIZE];
        int days, hours, minutes;

        days = (int) (seconds / 86400);
        seconds %= 86400;
        hours = (int) (seconds / 3600);
        hours %= 3600;
        minutes = (int) (seconds / 60);
        seconds %= 60;

        snprintf(buf, sizeof(buf), "%d day%s, %d:%02d:%02ld",
                 days, (days == 1) ? "" : "s", hours,
                 minutes, seconds);

        return buf;
}

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

                sendto_connection(conn_p, "Oper %s %s@%s",
                                  conf_p->name, conf_p->username,
                                  conf_p->host);
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

                sendto_connection(conn_p, "Server %s/%d %s",
                                  conf_p->name, abs(conf_p->defport),
                                  (conf_p->defport > 0) ? "A" : "");
        }
}

static void
stats_uplink(struct connection_entry *conn_p)
{
        if(server_p != NULL)
                sendto_connection(conn_p, "Currently connected to %s Idle: %d "
                                  "Sendq: %d Connected: %s",
                                  server_p->name,
                                  (CURRENT_TIME - server_p->last_time), 0,
                                  get_duration(CURRENT_TIME -
                                               server_p->first_time));
        else
                sendto_connection(conn_p, "Currently disconnected");
}

static void
stats_uptime(struct connection_entry *conn_p)
{
        sendto_connection(conn_p, "Up %s",
                          get_duration(CURRENT_TIME - config_file.first_time));
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
                sendto_connection(conn_p, "Usage: .stats <type>");
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

        sendto_connection(conn_p, "Unknown stats type: %s", parv[1]);
};
        
