/* src/scommand.c
 *   Contains code for handling of commands received from server.
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "scommand.h"
#include "rserv.h"
#include "tools.h"
#include "io.h"
#include "conf.h"
#include "client.h"
#include "serno.h"
#include "service.h"
#include "log.h"
#include "hook.h"
#include "s_userserv.h"

static dlink_list scommand_table[MAX_SCOMMAND_HASH];

static void c_admin(struct client *, const char *parv[], int parc);
static void c_capab(struct client *, const char *parv[], int parc);
static void c_encap(struct client *, const char *parv[], int parc);
static void c_pass(struct client *, const char *parv[], int parc);
static void c_ping(struct client *, const char *parv[], int parc);
static void c_pong(struct client *, const char *parv[], int parc);
static void c_stats(struct client *, const char *parv[], int parc);
static void c_trace(struct client *, const char *parv[], int parc);
static void c_version(struct client *, const char *parv[], int parc);
static void c_whois(struct client *, const char *parv[], int parc);

static struct scommand_handler admin_command = { "ADMIN", c_admin, 0, DLINK_EMPTY };
static struct scommand_handler capab_command = { "CAPAB", c_capab, FLAGS_UNKNOWN, DLINK_EMPTY };
static struct scommand_handler encap_command = { "ENCAP", c_encap, 0, DLINK_EMPTY };
static struct scommand_handler pass_command = { "PASS", c_pass, FLAGS_UNKNOWN, DLINK_EMPTY };
static struct scommand_handler ping_command = { "PING", c_ping, 0, DLINK_EMPTY };
static struct scommand_handler pong_command = { "PONG", c_pong, 0, DLINK_EMPTY };
static struct scommand_handler stats_command = { "STATS", c_stats, 0, DLINK_EMPTY };
static struct scommand_handler trace_command = { "TRACE", c_trace, 0, DLINK_EMPTY };
static struct scommand_handler version_command = { "VERSION", c_version, 0, DLINK_EMPTY };
static struct scommand_handler whois_command = { "WHOIS", c_whois, 0, DLINK_EMPTY };

void
init_scommand(void)
{
	add_scommand_handler(&admin_command);
	add_scommand_handler(&capab_command);
	add_scommand_handler(&encap_command);
	add_scommand_handler(&pass_command);
	add_scommand_handler(&ping_command);
	add_scommand_handler(&pong_command);
	add_scommand_handler(&stats_command);
	add_scommand_handler(&trace_command);
	add_scommand_handler(&version_command);
	add_scommand_handler(&whois_command);
}

static int
hash_command(const char *p)
{
	unsigned int hash_val = 0;

	while(*p)
	{
		hash_val += ((int) (*p) & 0xDF);
		p++;
	}

	return(hash_val % MAX_SCOMMAND_HASH);
}

static void
handle_scommand_unknown(const char *command, const char *parv[], int parc)
{
	struct scommand_handler *handler;
	dlink_node *ptr;
	unsigned int hashv = hash_command(command);
	
	DLINK_FOREACH(ptr, scommand_table[hashv].head)
	{
		handler = ptr->data;
		if(!strcasecmp(command, handler->cmd))
		{
			if(handler->flags & FLAGS_UNKNOWN)
				handler->func(NULL, parv, parc);
			return;
		}
	}
}

static void
handle_scommand_client(struct client *client_p, const char *command, 
			const char *parv[], int parc)
{
	struct scommand_handler *handler;
	scommand_func hook;
	dlink_node *ptr;
	dlink_node *hptr;
	unsigned int hashv = hash_command(command);
	
	DLINK_FOREACH(ptr, scommand_table[hashv].head)
	{
		handler = ptr->data;
		if(!strcasecmp(command, handler->cmd))
		{
			handler->func(client_p, parv, parc);

			DLINK_FOREACH(hptr, handler->hooks.head)
			{
				hook = hptr->data;
				(*hook)(client_p, parv, parc);
			}

			break;
		}
	}
}

void
handle_scommand(const char *command, const char *parv[], int parc)
{
	struct client *client_p;

	client_p = find_client(parv[0]);

	if(client_p != NULL)
		handle_scommand_client(client_p, command, parv, parc);

        /* we can only accept commands from an unknown entity, when we
         * dont actually have a server..
         */
	else if(server_p->client_p == NULL)
		handle_scommand_unknown(command, parv, parc);

        else
                mlog("unknown prefix %s for command %s", parv[0], command);
}

void
add_scommand_handler(struct scommand_handler *chandler)
{
	unsigned int hashv;

	if(chandler == NULL || EmptyString(chandler->cmd))
		return;

	hashv = hash_command(chandler->cmd);
	dlink_add_alloc(chandler, &scommand_table[hashv]);
}

void
add_scommand_hook(scommand_func hook, const char *command)
{
	struct scommand_handler *handler;
	dlink_node *ptr;
	unsigned int hashv = hash_command(command);

	DLINK_FOREACH(ptr, scommand_table[hashv].head)
	{
		handler = ptr->data;
		if(!strcasecmp(command, handler->cmd))
		{
			dlink_add_alloc(hook, &handler->hooks);
			return;
		}
	}

	s_assert(0);
}

void
del_scommand_hook(scommand_func hook, const char *command)
{
	struct scommand_handler *handler;
	dlink_node *ptr;
	unsigned int hashv = hash_command(command);

	DLINK_FOREACH(ptr, scommand_table[hashv].head)
	{
		handler = ptr->data;
		if(!strcasecmp(command, handler->cmd))
		{
			dlink_find_destroy(hook, &handler->hooks);
			return;
		}
	}

	s_assert(0);
}

static void
c_admin(struct client *client_p, const char *parv[], int parc)
{
	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(!IsUser(client_p))
		return;

	sendto_server(":%s 256 %s :Administrative info about %s",
		      MYNAME, parv[0], MYNAME);

	if(!EmptyString(config_file.admin1))
		sendto_server(":%s 257 %s :%s",
                              MYNAME, parv[0], config_file.admin1);
	if(!EmptyString(config_file.admin2))
		sendto_server(":%s 258 %s :%s",
                              MYNAME, parv[0], config_file.admin2);
	if(!EmptyString(config_file.admin3))
		sendto_server(":%s 259 %s :%s",
                              MYNAME, parv[0], config_file.admin3);
}

static struct capab_entry
{
	const char *name;
	int flag;
} capab_table[] = {
	{ "SERVICES",	CONN_CAP_SERVICE	},
	{ "\0", 0 }
};

static void
c_capab(struct client *client_p, const char *parv[], int parc)
{
	char buf[BUFSIZE];
	char *data, *p;
	int i;

	if(parc < 2)
		return;

	/* unregistered server only */
	if(client_p != NULL)
		return;

	strlcpy(buf, parv[1], sizeof(buf));

	if((p = strchr(buf, ' ')))
		*p++ = '\0';

	for(data = buf; data; )
	{
		for(i = 0; capab_table[i].flag; i++)
		{
			if(!irccmp(capab_table[i].name, data))
			{
				server_p->flags |= capab_table[i].flag;
				break;
			}
		}

		data = p;
		if(p && (p = strchr(data, ' ')))
			*p++ = '\0';
	}
}

static void
c_encap(struct client *client_p, const char *parv[], int parc)
{
	if(parc < 3)
		return;

	if(!match(parv[1], MYNAME))
		return;

	if(!irccmp(parv[2], "LOGIN"))
	{
		/* this is only accepted from users */
		if(EmptyString(parv[3]) || !IsUser(client_p))
			return;

		hook_call(HOOK_BURST_LOGIN, client_p, (void *) parv[3]);
	}
}

static void
c_pass(struct client *client_p, const char *parv[], int parc)
{
	if(parc < 3)
		return;

	/* we shouldnt get this when the link is established.. */
	if(client_p != NULL)
		return;

	/* password is wrong */
	if(strcmp(server_p->pass, parv[1]))
	{
		mlog("Connection to server %s failed: "
			"(Password mismatch)",
			server_p->name);
		(server_p->io_close)(server_p);
		return;
	}

	if(strcasecmp(parv[2], "TS"))
	{
		mlog("Connection to server %s failed: "
			"(Protocol mismatch)",
			server_p->name);
		(server_p->io_close)(server_p);
		return;
	}

	SetConnTS(server_p);
}

static void
c_ping(struct client *client_p, const char *parv[], int parc)
{
	if(parc < 2 || EmptyString(parv[1]))
		return;

	sendto_server(":%s PONG %s :%s", MYNAME, MYNAME, parv[0]);
}

static void
c_pong(struct client *client_p, const char *parv[], int parc)
{
        if(parc < 2 || EmptyString(parv[1]))
                return;

        if(!(server_p->flags & FLAGS_EOB))
        {
                mlog("Connection to server %s completed", server_p->name);
                sendto_all(UMODE_SERVER, "Connection to server %s completed",
                           server_p->name);
                server_p->flags |= FLAGS_EOB;

		introduce_services_channels();
		hook_call(HOOK_FINISHED_BURSTING, NULL, NULL);
        }
}

static void
c_trace(struct client *client_p, const char *parv[], int parc)
{
        struct client *service_p;
        dlink_node *ptr;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(!IsUser(client_p))
		return;

        DLINK_FOREACH(ptr, service_list.head)
        {
                service_p = ptr->data;

		if(ServiceDisabled(service_p))
			continue;

                sendto_server(":%s %d %s %s service %s[%s@%s] (127.0.0.1) 0 0",
                              MYNAME, ServiceOpered(service_p) ? 204 : 205,
                              parv[0], ServiceOpered(service_p) ? "Oper" : "User",
                              service_p->name, service_p->service->username,
                              service_p->service->host);
        }

        sendto_server(":%s 206 %s Serv uplink 1S 1C %s *!*@%s 0",
                      MYNAME, parv[0], server_p->name, MYNAME);
        sendto_server(":%s 262 %s %s :End of /TRACE",
                      MYNAME, parv[0], MYNAME);
}
	
static void
c_version(struct client *client_p, const char *parv[], int parc)
{
	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(IsUser(client_p))
		sendto_server(":%s 351 %s ratbox-services-%s(%s). %s A TS",
			      MYNAME, parv[0], RSERV_VERSION,
                              SERIALNUM, MYNAME);
}

static void
c_whois(struct client *client_p, const char *parv[], int parc)
{
        struct client *target_p;

        if(parc < 3 || EmptyString(parv[2]))
                return;

        if(!IsUser(client_p))
                return;

        if((target_p = find_client(parv[2])) == NULL ||
           IsServer(target_p))
        {
                sendto_server(":%s 401 %s %s :No such nick/channel",
                              MYNAME, parv[0], parv[2]);
        }
        else if(IsUser(target_p))
        {
                sendto_server(":%s 311 %s %s %s %s * :%s",
                              MYNAME, parv[0], target_p->name,
                              target_p->user->username, target_p->user->host,
                              target_p->info);
                sendto_server(":%s 312 %s %s %s :%s",
                              MYNAME, parv[0], target_p->name,
                              target_p->user->servername,
                              target_p->uplink->info);

                if(ClientOper(target_p))
                        sendto_server(":%s 313 %s %s :is an IRC Operator",
                                      MYNAME, parv[0], target_p->name);

#ifdef ENABLE_USERSERV
		if(target_p->user->user_reg)
			sendto_server(":%s 330 %s %s %s :is logged in as",
					MYNAME, parv[0], target_p->name,
					target_p->user->user_reg->name);
#endif
        }
        /* must be one of our services.. */
        else
        {
                sendto_server(":%s 311 %s %s %s %s * :%s",
                              MYNAME, parv[0], target_p->name,
                              target_p->service->username,
                              target_p->service->host, target_p->info);
                sendto_server(":%s 312 %s %s %s :%s",
                              MYNAME, parv[0], target_p->name, MYNAME,
                              config_file.gecos);

                if(ServiceOpered(target_p))
                        sendto_server(":%s 313 %s %s :is an IRC Operator",
                                      MYNAME, parv[0], target_p->name);
        }

        sendto_server(":%s 318 %s %s :End of /WHOIS list.",
                      MYNAME, parv[0], target_p->name);
}

static void
c_stats(struct client *client_p, const char *parv[], int parc)
{
	char statchar;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(!IsUser(client_p))
		return;

	statchar = parv[1][0];
	switch(statchar)
	{
		case 'c': case 'C':
		case 'n': case 'N':
		{
			struct conf_server *conf_p;
			dlink_node *ptr;

			DLINK_FOREACH(ptr, conf_server_list.head)
			{
				conf_p = ptr->data;

				sendto_server(":%s 213 %s C *@%s %s %s %d uplink",
					      MYNAME, parv[0], conf_p->name,
                                              (conf_p->defport > 0) ? "A" : "*",
					      conf_p->name, 
                                              abs(conf_p->defport));
			}
		}
			break;

		case 'h': case 'H':
		{
			struct conf_server *conf_p;
			dlink_node *ptr;

			DLINK_FOREACH(ptr, conf_server_list.head)
			{
				conf_p = ptr->data;

				sendto_server(":%s 244 %s H * * %s",
					      MYNAME, parv[0], conf_p->name);
			}
		}
			break;

		case 'u':
			sendto_server(":%s 242 %s :Server Up %s",
				      MYNAME, parv[0],
                                      get_duration(CURRENT_TIME -
                                                   first_time));
			break;

		case 'v': case 'V':
			sendto_server(":%s 249 %s V :%s (AutoConn.!*@*) Idle: "
                                      "%d SendQ: %d Connected %s",
				      MYNAME, parv[0], server_p->name, 
				      (CURRENT_TIME - server_p->last_time), 
                                      get_sendq(server_p),
                                      get_duration(CURRENT_TIME -
                                                   server_p->first_time));
			break;

		default:
			break;
	}

	sendto_server(":%s 219 %s %c :End of /STATS report",
		      MYNAME, parv[0], statchar);
}
