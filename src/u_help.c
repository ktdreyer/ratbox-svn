/* $Id$ */
#include "stdinc.h"
#include "ucommand.h"
#include "c_init.h"
#include "io.h"
#include "rserv.h"

#define MAX_HELP_ROW 8

static void u_help(struct connection_entry *, char *parv[], int parc);
struct ucommand_handler help_ucommand = { "help", u_help, 0 };

static const char *connect_help[] = {
        "Usage: .connect <server> [port]",
        "       Connects to the given server, using optional port",
        NULL
};
static const char *die_help[] = {
        "Usage: .die <servername>",
        "       Terminates services",
        NULL
};
static const char *events_help[] = {
        "Usage: .events",
        "       Lists scheduled events",
        NULL
};
static const char *flags_help[] = {
        "Usage: .flags [[+|-]flag]",
        "       Lists current flags, or alters flag status:",
        "       chat    - 'partyline' messages",
        "       auth    - Oper connections/disconnections from services",
        "       server  - Server connections/disconnections",
        NULL
};
static const char *help_help[] = {
        "Usage: .help [topic]",
        "       Gives command list, or information on a specific command",
        NULL
};
static const char *quit_help[] = {
        "Usage: .quit",
        "       Disconnects you from services",
        NULL
};
static const char *service_help[] = {
        "Usage: .service [service]",
        "       Gives general service information, or information on a ",
        "       specific service if specified",
        NULL
};
static const char *stats_help[] = {
        "Usage: .stats <type>",
        "       Gives information of the specified type:",
        "       opers    - Opers who have access to services",
        "       servers  - Servers to connect to",
        "       uplink   - Information about our uplink",
        "       uptime   - Services uptime",
        NULL
};
static const char *status_help[] = {
        "Usage: .status",
        "       Gives general status information",
        NULL
};

struct _help_table
{
        const char *name;
        const char **help;
};
static struct _help_table help_table[] =
{
        { "connect",    connect_help    },
        { "die",        die_help        },
        { "events",     events_help     },
        { "flags",      flags_help      },
        { "help",       help_help       },
        { "quit",       quit_help       },
        { "service",    service_help    },
        { "stats",      stats_help      },
        { "status",     status_help     },
        { "\0",         NULL            }
};

static void
print_help(struct connection_entry *conn_p, const char **help)
{
        int x = 0;

        while(help[x] != NULL)
        {
                sendto_one(conn_p, "%s", help[x]);
                x++;
        }
}

static void
u_help(struct connection_entry *conn_p, char *parv[], int parc)
{
        int i;

        if(parc < 2 || EmptyString(parv[1]))
        {
                const char *hparv[MAX_HELP_ROW];
                int j = 0;

                sendto_one(conn_p, "Available commands:");

                for(i = 0; help_table[i].name[0] != '\0'; i++)
                {
                        hparv[j] = help_table[i].name;
                        j++;

                        if(j >= MAX_HELP_ROW)
                        {
                                sendto_one(conn_p,
                                           "   %-8s %-8s %-8s %-8s "
                                           "%-8s %-8s %-8s %-8s",
                                           hparv[0], hparv[1], hparv[2],
                                           hparv[3], hparv[4], hparv[5],
                                           hparv[6], hparv[7]);
                                j = 0;
                        }
                }

                if(j)
                {
                        char buf[BUFSIZE];
                        char *p = buf;

                        for(i = 0; i < j; i++)
                        {
                                p += sprintf(p, "%-8s ", hparv[i]);
                        }

                        sendto_one(conn_p, "   %s", buf);
                }

                sendto_one(conn_p, "For more information see .help <command>");
                return;
        }

        for(i = 0; help_table[i].name[0] != '\0'; i++)
        {
                if(!strcasecmp(help_table[i].name, parv[1]))
                {
                        print_help(conn_p, help_table[i].help);
                        return;
                }
        }

        sendto_one(conn_p, "Unknown help topic: %s", parv[2]);
}      
