/* src/c_error.c
 *   Contains code for handling "ERROR" command
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "c_init.h"
#include "scommand.h"
#include "client.h"
#include "log.h"
#include "io.h"

static void c_error(struct client *, const char *parv[], int parc);
struct scommand_handler error_command = { "ERROR", c_error, FLAGS_UNKNOWN, DLINK_EMPTY };

static void
c_error(struct client *client_p, const char *parv[], int parc)
{
        if(parc < 1 || EmptyString(parv[0]))
                return;

        mlog("Connection to server %s error: (%s)",
             server_p->name, parv[0]);
        sendto_all(UMODE_SERVER, "Connection to server %s error: (%s)",
                   server_p->name, parv[0]);

        (server_p->io_close)(server_p);
}
