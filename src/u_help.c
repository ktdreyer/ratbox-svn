/* $Id$ */
#include "stdinc.h"
#include "ucommand.h"
#include "c_init.h"
#include "io.h"

#define MAX_HELP_ROW 8

static void u_help(struct connection_entry *, char *parv[], int parc);
struct ucommand_handler help_ucommand = { "help", u_help, 0 };

static void
u_help(struct connection_entry *conn_p, char *parv[], int parc)
{
        sendto_connection(conn_p, "Available commands:");
        list_ucommand(conn_p);
}       
