/* src/c_mode.c
 *  Contains code for handling "MODE" command.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */

#include "stdinc.h"
#include "c_init.h"
#include "client.h"
#include "command.h"
#include "log.h"

static void c_mode(struct client *, char *parv[], int parc);
struct scommand_handler mode_command = { "MODE", c_mode };

static void
c_mode(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	/* user setting mode:
	 * :<user> MODE <user> +<modes>
	 */
	if(!IsChanPrefix(parv[1][0]))
	{
		if(parc < 3 || EmptyString(parv[2]))
			return;

		if((target_p = find_user(parv[1])) == NULL)
			return;

		if(target_p != client_p)
			return;

		slog("OLD UMODE: %s", umode_to_string(target_p->user->umode));
		target_p->user->umode = string_to_umode(parv[2], target_p->user->umode);
		slog("NEW UMODE: %s", umode_to_string(target_p->user->umode));
		return;
	}
}
