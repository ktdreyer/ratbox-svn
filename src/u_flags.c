/* $Id$ */
#include "stdinc.h"
#include "ucommand.h"
#include "c_init.h"
#include "io.h"
#include "rserv.h"

#define DIR_ADD 1
#define DIR_DEL 2

static void u_flags(struct connection_entry *conn_p, char *parv[], int parc);
struct ucommand_handler flags_ucommand = { "flags", &u_flags, 0 };

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
