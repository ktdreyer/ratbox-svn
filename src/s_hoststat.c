/* src/s_hoststat.c
 *   Contains the code for the host statistics service.
 *
 * Copyright (C) 2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef HOSTSTAT_SERVICE
#include "service.h"
#include "client.h"
#include "channel.h"
#include "io.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"

#define HOSTSTAT_CLONES_DEFAULT 4

#define HOSTSTAT_ERR_PARAM	1

static struct client *hoststat_p;

static int s_hoststat_clones(struct client *, char *parv[], int parc);
static int s_hoststat_host(struct client *, char *parv[], int parc);
static int s_hoststat_testmask(struct client *, char *parv[], int parc);

static struct service_command hoststat_command[] =
{
	{ "CLONES",	&s_hoststat_clones,	NULL, 0, 1, 0L },
	{ "HOST",	&s_hoststat_host,	NULL, 0, 1, 0L },
	{ "TESTMASK",	&s_hoststat_testmask,	NULL, 1, 1, 0L },
	{ "\0",		NULL,			NULL, 0, 0, 0L }
};

static struct service_error hoststat_message[] =
{
	{ HOSTSTAT_ERR_PARAM,	"Missing parameters."		},
	{ 0,			"\0"				}
};

static struct service_handler hoststat_service = {
	"HOSTSTAT", "HOSTSTAT", "hoststat", "services.hoststat",
	"Host Statistics", 1, 60, 80, 
	hoststat_command, hoststat_message, NULL, NULL
};

void
init_s_hoststat(void)
{
	hoststat_p = add_service(&hoststat_service);
}

static int
s_hoststat_clones(struct client *client_p, char *parv[], int parc)
{
	struct host_entry *host_p;
	struct uhost_entry *uhost_p;
	struct client *target_p;
	dlink_node *ptr;
	dlink_node *uptr;
	dlink_node *nptr;
	int limit = HOSTSTAT_CLONES_DEFAULT;
	int i;

	if(parc > 0)
		limit = atoi(parv[0]);

	if(limit < 3)
		limit = 3;

	sendto_server(":%s NOTICE %s :Please wait...processing...",
			MYNAME, client_p->name);

	for(i = 0; i < MAX_NAME_HASH; i++)
	{
	DLINK_FOREACH(ptr, host_table[i].head)
	{
		host_p = ptr->data;

		/* not even that count on entire host.. */
		if(dlink_list_length(&host_p->users) < limit)
			continue;

		/* iterate over each user@host within the host */
		DLINK_FOREACH(uptr, host_p->uhosts.head)
		{
			uhost_p = uptr->data;

			if(dlink_list_length(&uhost_p->users) < limit)
				continue;

			sendto_server(":%s NOTICE %s :%s@%s - %d users",
					MYNAME, client_p->name,
					uhost_p->username, host_p->host,
					dlink_list_length(&uhost_p->users));

			/* now print out each user.. */
			DLINK_FOREACH(nptr, uhost_p->users.head)
			{
				target_p = nptr->data;

				sendto_server(":%s NOTICE %s :   %s on %s",
						MYNAME, client_p->name,
						target_p->name,
						target_p->user->servername);
			}
		}
	}
	}

	sendto_server(":%s NOTICE %s :-- end of list --",
			MYNAME, client_p->name);
	return 1;
}

static int
s_hoststat_host(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;
	struct host_entry *host_p;
	dlink_node *ptr;
	int oper_count = 0;

	if(parc < 1)
	{
		service_error(hoststat_p, client_p, HOSTSTAT_ERR_PARAM);
		return 1;
	}

	if((host_p = find_host(parv[0])) == NULL)
	{
		sendto_server(":%s NOTICE %s :No stats for the hostname, "
				"%s, found.",
				MYNAME, client_p->name, parv[0]);
		return 1;
	}

	DLINK_FOREACH(ptr, host_p->users.head)
	{
		target_p = ptr->data;

		if(is_oper(target_p))
			oper_count++;
	}

	sendto_server(":%s NOTICE %s :Host: %s",
			MYNAME, client_p->name, parv[0]);
	sendto_server(":%s NOTICE %s :  Cur. clients: %d (%d unique)",
			MYNAME, client_p->name, 
			dlink_list_length(&host_p->users),
			dlink_list_length(&host_p->uhosts));

	if(oper_count)
		sendto_server(":%s NOTICE %s :  Cur. opers  : %d",
				MYNAME, client_p->name, oper_count);

#ifdef EXTENDED_HOSTHASH
	sendto_server(":%s NOTICE %s :  Max. clients: %d at %s",
			MYNAME, client_p->name, host_p->max_clients,
			get_time(host_p->maxc_time));

	sendto_server(":%s NOTICE %s :  Max. unique : %d at %s",
			MYNAME, client_p->name, host_p->max_unique,
			get_time(host_p->maxu_time));
#endif

	return 1;
}

static int
s_hoststat_testmask(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;
	const char *username = parv[0];
	char *host;
	dlink_node *ptr;
	int count = 0;

	if(parc < 1 || EmptyString(parv[0]))
	{
		service_error(hoststat_p, client_p, HOSTSTAT_ERR_PARAM);
		return 1;
	}

	if((host = strchr(parv[0], '@')) == NULL)
	{
		return 1;
	}

	*host++ = '\0';

	DLINK_FOREACH(ptr, user_list.head)
	{
		target_p = ptr->data;

		if(match(username, target_p->user->username) &&
		   match(host, target_p->user->host))
			count++;
	}

	sendto_server(":%s NOTICE %s :%d clients match %s@%s",
			MYNAME, client_p->name, count, username, host);
	return 1;
}

#endif
