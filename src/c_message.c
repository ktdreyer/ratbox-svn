/* src/c_message.c
 *   Contains code for directing received privmsgs at services.
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "service.h"
#include "client.h"
#include "conf.h"
#include "scommand.h"
#include "c_init.h"
#include "log.h"
#include "io.h"

static void c_message(struct client *, const char *parv[], int parc);

struct scommand_handler privmsg_command = { "PRIVMSG", c_message, 0, DLINK_EMPTY };

static void
c_message(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p = NULL;
	struct client *tmp_p;
	char *target;
	char *text;
	char *p;

	if(parc < 3 || EmptyString(parv[2]))
		return;

	target = LOCAL_COPY(parv[1]);

	/* username@server messaged? */
	if((p = strchr(target, '@')) != NULL)
	{
		dlink_node *ptr;

		*p = '\0';

		/* walk list manually hunting for this username.. */
		DLINK_FOREACH(ptr, service_list.head)
		{
			tmp_p = ptr->data;

			if(!irccmp(target, tmp_p->service->username))
			{
				target_p = tmp_p;
				break;
			}
		}
	}
	/* hunt for the nick.. */
	else
		target_p = find_service(target);

	if(target_p == NULL)
		return;

	/* ctcp.. doesnt matter who its addressed to. */
	if(parv[2][0] == '\001')
	{
		struct conf_oper *oper_p = find_conf_oper(client_p->user->username,
							  client_p->user->host,
							  client_p->user->servername);

		if(oper_p == NULL || !ConfOperDcc(oper_p))
		{
			sendto_server(":%s NOTICE %s :No access.",
					MYNAME, client_p->name);
			return;
		}

		/* request for us to dcc them.. */
		if(!strncasecmp(parv[2], "\001CHAT\001", 6))
		{
			connect_from_client(client_p, oper_p, target_p->name);
			return;
		}

		/* dcc request.. \001DCC CHAT chat <HOST> <IP>\001 */
		else if(!strncasecmp(parv[2], "\001DCC CHAT ", 10))
		{
			/* skip the first bit.. */
			char *host;
			char *cport;
			int port;

			p = LOCAL_COPY(parv[2]);
			p += 10;

			/* skip the 'chat' */
			if((host = strchr(p, ' ')) == NULL)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc parameters",
						MYNAME, client_p->name);
				return;
			}

			*host++ = '\0';

			/* <host> <port>\001 */
			if((cport = strchr(host, ' ')) == NULL)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc parameters",
						MYNAME, client_p->name);
				return;
			}

			*cport++ = '\0';

			/* another space? hmm. */
			if(strchr(cport, ' ') != NULL)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc parameters",
						MYNAME, client_p->name);
				return;
			}

			if((p = strchr(cport, '\001')) == NULL)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc parameters",
						MYNAME, client_p->name);
				return;
			}

			*p = '\0';

			if((port = atoi(cport)) <= 1024)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc port",
						MYNAME, client_p->name);
				return;
			}

			connect_to_client(client_p, oper_p, host, port);
		}

		return;
	}

	text = LOCAL_COPY(parv[2]);
	handle_service(target_p, client_p, text);
}

