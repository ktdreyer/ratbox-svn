/* src/client.c
 *  Contains code for handling remote clients.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "client.h"
#include "channel.h"
#include "command.h"
#include "io.h"
#include "log.h"
#include "service.h"

static dlink_list name_table[MAX_NAME_HASH];

dlink_list user_list;
dlink_list server_list;
dlink_list exited_list;

static void c_kill(struct client *, char *parv[], int parc);
static void c_nick(struct client *, char *parv[], int parc);
static void c_quit(struct client *, char *parv[], int parc);
static void c_server(struct client *, char *parv[], int parc);
static void c_squit(struct client *, char *parv[], int parc);

static struct scommand_handler kill_command = { "KILL", c_kill, 0 };
static struct scommand_handler nick_command = { "NICK", c_nick, 0 };
static struct scommand_handler quit_command = { "QUIT", c_quit, 0 };
static struct scommand_handler server_command = { "SERVER", c_server, FLAGS_UNKNOWN };
static struct scommand_handler squit_command = { "SQUIT", c_squit, 0 };

void
init_client(void)
{
	add_scommand_handler(&kill_command);
	add_scommand_handler(&nick_command);
	add_scommand_handler(&quit_command);
	add_scommand_handler(&server_command);
	add_scommand_handler(&squit_command);
}

static unsigned int
hash_nick(const char *p)
{
	unsigned int h = 0;

	while(*p)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*p++));
	}

	return(h & (MAX_NAME_HASH-1));
}

void
add_client(struct client *target_p)
{
	unsigned int hashv = hash_nick(target_p->name);
	dlink_add(target_p, &target_p->nameptr, &name_table[hashv]);
}

void
del_client(struct client *target_p)
{
	unsigned int hashv = hash_nick(target_p->name);
	dlink_delete(&target_p->nameptr, &name_table[hashv]);
}

struct client *
find_client(const char *name)
{
	struct client *target_p;
	dlink_node *ptr;
	unsigned int hashv = hash_nick(name);

	DLINK_FOREACH(ptr, name_table[hashv].head)
	{
		target_p = ptr->data;

		if(!irccmp(target_p->name, name))
			return target_p;
	}

	return NULL;
}

struct client *
find_user(const char *name)
{
	struct client *target_p = find_client(name);

	if(IsUser(target_p))
		return target_p;

	return NULL;
}

struct client *
find_server(const char *name)
{
	struct client *target_p = find_client(name);

	if(IsServer(target_p))
		return target_p;

	return NULL;
}

struct client *
find_service(const char *name)
{
	struct client *target_p = find_client(name);

	if(IsService(target_p))
		return target_p;

	return NULL;
}

static void
exit_user(struct client *target_p)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(IsDead(target_p))
		return;

	SetDead(target_p);

	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->user->channels.head)
	{
		del_chmember(ptr->data);
	}

	dlink_move_node(&target_p->listnode, &user_list, &exited_list);
	dlink_delete(&target_p->upnode, &target_p->uplink->server->users);
}

static void
exit_server(struct client *target_p)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(IsDead(target_p))
		return;

	SetDead(target_p);

	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->server->users.head)
	{
		exit_client(ptr->data);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->server->servers.head)
	{
		exit_client(ptr->data);
	}

	dlink_move_node(&target_p->listnode, &server_list, &exited_list);

	/* if it has an uplink, remove it from its uplinks list */
	if(target_p->uplink != NULL)
		dlink_delete(&target_p->upnode, &target_p->uplink->server->servers);
}

void
exit_client(struct client *target_p)
{
	if(IsServer(target_p))
		exit_server(target_p);
	else if(IsUser(target_p))
		exit_user(target_p);
	else if(IsService(target_p))
	{
		slog("EEK: Tried to exit one of my own services. damn.");
		return;
	}

	del_client(target_p);
}

void
free_client(struct client *target_p)
{
	my_free(target_p->user);
	my_free(target_p->server);
	my_free(target_p);
};

int
string_to_umode(const char *p, int current_umode)
{
	int umode = current_umode;
	int dir = 1;

	while(*p)
	{
		switch(*p)
		{
			case '+':
				dir = 1;
				break;

			case '-':
				dir = 0;
				break;

			case 'a':
				if(dir)
					umode |= CLIENT_ADMIN;
				else
					umode &= ~CLIENT_ADMIN;
				break;

			case 'i':
				if(dir)
					umode |= CLIENT_INVIS;
				else
					umode &= ~CLIENT_INVIS;
				break;

			case 'o':
				if(dir)
					umode |= CLIENT_OPER;
				else
					umode &= ~CLIENT_OPER;
				break;

			default:
				break;
		}

		*p++;
	}

	return umode;
}

const char *
umode_to_string(int umode)
{
	static char buf[5];
	char *p;

	p = buf;

	*p++ = '+';

	if(umode & CLIENT_ADMIN)
		*p++ = 'a';
	if(umode & CLIENT_INVIS)
		*p++ = 'i';
	if(umode & CLIENT_OPER)
		*p++ = 'o';

	*p = '\0';
	return buf;
}

void
c_nick(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;
	struct client *uplink_p;
	time_t newts;

	if(parc != 3 && parc != 9)
	{
		slog("PROTO: NICK command received with invalid params");
		return;
	}

	if(parc == 9)
	{
		target_p = find_client(parv[1]);
		uplink_p = find_server(parv[7]);
		newts = atol(parv[2]);

		if(uplink_p == NULL)
		{
			slog("PROTO: Ghost killed on invalid server %s", parv[7]);
			return;
		}

		if(target_p != NULL)
		{
			if(IsServer(target_p))
			{
				slog("PROTO: NICK introduced a server %s", parv[1]);
				return;
			}
			else if(IsUser(target_p))
			{
				if(target_p->user->tsinfo < newts)
				{
					slog("PROTO: NICK %s with higher TS introduced causing collision.",
					     target_p->name);
					return;
				}

				/* normal nick collision.. exit old */
				exit_client(target_p);
			}
			else if(IsService(target_p))
			{
				/* ugh. anything with a ts this low is
				 * either someone fucking about, or another
				 * service.  we go byebye.
				 */
				if(newts <= 1)
				{
					sendto_server(":%s WALLOPS :Detected a services fight, im gone!");
					die("service fight");
				}

				return;
			}
		}

		target_p = my_malloc(sizeof(struct client));
		target_p->user = my_malloc(sizeof(struct user));

		target_p->uplink = uplink_p;

		strlcpy(target_p->name, parv[1], sizeof(target_p->name));
		strlcpy(target_p->user->username, parv[5], 
			sizeof(target_p->user->username));
		strlcpy(target_p->user->host, parv[6], sizeof(target_p->user->host));

		target_p->user->servername = uplink_p->name;
		target_p->user->tsinfo = newts;
		target_p->user->umode = string_to_umode(parv[4], 0);

		add_client(target_p);
		dlink_add(target_p, &target_p->listnode, &user_list);
		dlink_add(target_p, &target_p->upnode, &uplink_p->server->users);
		slog("HASH: Added client %s uplink %s",
		     target_p->name, target_p->user->servername);
	}
	else
	{
		if(!IsUser(client_p))
			return;

		del_client(client_p);
		strlcpy(client_p->name, parv[1], sizeof(client_p->name));
		add_client(client_p);

		client_p->user->tsinfo = atol(parv[2]);
	}
}

void
c_quit(struct client *client_p, char *parv[], int parc)
{
	if(!IsUser(client_p))
	{
		slog("PROTO: QUIT received from server %s", client_p->name);
		return;
	}

	exit_client(client_p);
}

void
c_kill(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	if((target_p = find_client(parv[1])) == NULL)
		return;

	if(IsServer(target_p))
	{
		slog("PROTO: KILL received for server %s", target_p->name);
		return;
	}

	/* grmbl. */
	if(IsService(target_p))
	{
		introduce_service(target_p);
		return;
	}

	exit_client(target_p);
}

void
c_server(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;
	static char default_gecos[] = "(Unknown Location)";

	if(parc < 4)
		return;

	target_p = my_malloc(sizeof(struct client));
	target_p->server = my_malloc(sizeof(struct server));

	strlcpy(target_p->name, parv[1], sizeof(target_p->name));
	strlcpy(target_p->info, EmptyString(parv[3]) ? default_gecos : parv[3],
		sizeof(target_p->info));

	target_p->server->hops = atoi(parv[2]);

	/* this server has an uplink */
	if(client_p != NULL)
	{
		target_p->uplink = client_p;
		dlink_add(target_p, &target_p->upnode, &client_p->server->servers);
	}
	/* its connected to us */
	else
		server_p->client_p = target_p;

	add_client(target_p);
	dlink_add(target_p, &target_p->listnode, &server_list);

	slog("HASH: Added server %s uplink %s", 
	     target_p->name, target_p->uplink ? target_p->uplink->name : "NULL");
}

void
c_squit(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;

	/* malformed squit, byebye. */
	if(parc < 2 || EmptyString(parv[1]))
	{
		exit_client(server_p->client_p);
		return;
	}

	target_p = find_server(parv[1]);

	if(target_p == NULL)
	{
		slog("PROTO: SQUIT for unknown server %s", parv[1]);
		return;
	}

	exit_client(target_p);
}
