/* src/c_error.c
 *  Contains code for handling "ERROR" command
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "c_init.h"
#include "scommand.h"
#include "client.h"
#include "log.h"
#include "io.h"

static void c_error(struct client *, char *parv[], int parc);
struct scommand_handler error_command = { "ERROR", c_error, FLAGS_UNKNOWN };

static void
c_error(struct client *client_p, char *parv[], int parc)
{
        if(parc < 2 || EmptyString(parv[1]))
                return;

        slog_send("Connection to server %s error: (%s)",
                  server_p->name, parv[1]);

        (server_p->io_close)(server_p);
}
