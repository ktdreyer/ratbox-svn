/* src/s_alis.c
 *  Contains the code for ALIS, the Advanced List Service
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "service.h"
#include "client.h"
#include "c_init.h"
#include "log.h"

static void s_alis(struct client *, char *text);

struct service_handler alis_service = {
	"ALIS", "alis", "services.alis", "Advanced List Service", &s_alis
};

static void
s_alis(struct client *client_p, char *text)
{
	slog("someone messaged alis.");
}
