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
#include "scommand.h"
#include "c_init.h"
#include "log.h"
#include "io.h"

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

	/* ctcp.. doesnt matter who its addressed to. */
	if(parv[2][0] == '\001')
	{
		if(!ClientOper(client_p))
			return;

		/* dcc request.. \001DCC CHAT chat <HOST> <IP>\001 */
		if(!strncasecmp(parv[2], "\001DCC CHAT ", 10))
		{
			/* skip the first bit.. */
			char *p = parv[2]+10;
			char *host;
			char *cport;
			int port;

			/* skip the 'chat' */
			if((host = strchr(p, ' ')) == NULL)
				return;

			*host++ = '\0';

			/* <host> <port>\001 */
			if((cport = strchr(host, ' ')) == NULL)
				return;

			*cport++ = '\0';

			/* another space? hmm. */
			if(strchr(cport, ' ') != NULL)
				return;

			if((p = strchr(cport, '\001')) == NULL)
				return;

			*p = '\0';

			if((port = atoi(cport)) <= 1024)
				return;

			connect_to_client(client_p->name, host, port);
		}

		return;
	}

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

