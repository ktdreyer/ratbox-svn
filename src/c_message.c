/* src/c_message.c
 *  Contains code for directing received privmsgs at services.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "service.h"
#include "client.h"
#include "command.h"
#include "c_init.h"
#include "log.h"

static void c_message(struct client *, char *parv[], int parc);

struct scommand_handler privmsg_command = { "PRIVMSG", c_message, 0 };
/* struct scommand_handler notice_command = { "NOTICE", c_message, 0 }; */

static void
c_message(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;
	char *p;

	if(parc < 3 || EmptyString(parv[2]))
		return;

	/* username@server messaged? */
	if((p = strchr(parv[1], '@')) != NULL)
	{
		dlink_node *ptr;

		*p = '\0';

		/* walk list manually hunting for this username.. */
		DLINK_FOREACH(ptr, service_list.head)
		{
			target_p = ptr->data;

			if(!irccmp(parv[1], target_p->service->username))
			{
				(target_p->service->func)(client_p, parv[2]);
				break;
			}
		}

		return;
	}

	/* hunt for the nick.. */
	if((target_p = find_service(parv[1])) != NULL)
		target_p->service->func(client_p, parv[2]);
}

