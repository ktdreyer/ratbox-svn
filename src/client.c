/* src/client.c
 *   Contains code for handling remote clients.
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "client.h"
#include "channel.h"
#include "scommand.h"
#include "io.h"
#include "log.h"
#include "service.h"
#include "balloc.h"
#include "event.h"

static dlink_list name_table[MAX_NAME_HASH];
dlink_list host_table[MAX_NAME_HASH];

dlink_list user_list;
dlink_list server_list;
dlink_list exited_list;

static BlockHeap *client_heap;
static BlockHeap *user_heap;
static BlockHeap *server_heap;
static BlockHeap *host_heap;
static BlockHeap *uhost_heap;

static void c_kill(struct client *, const char *parv[], int parc);
static void c_nick(struct client *, const char *parv[], int parc);
static void c_quit(struct client *, const char *parv[], int parc);
static void c_server(struct client *, const char *parv[], int parc);
static void c_squit(struct client *, const char *parv[], int parc);

static struct scommand_handler kill_command = { "KILL", c_kill, 0, DLINK_EMPTY };
static struct scommand_handler nick_command = { "NICK", c_nick, 0, DLINK_EMPTY };
static struct scommand_handler quit_command = { "QUIT", c_quit, 0, DLINK_EMPTY };
static struct scommand_handler server_command = { "SERVER", c_server, FLAGS_UNKNOWN, DLINK_EMPTY};
static struct scommand_handler squit_command = { "SQUIT", c_squit, 0, DLINK_EMPTY };

static void cleanup_hosts(void *unused);

/* init_client()
 *   initialises various things
 */
void
init_client(void)
{
        client_heap = BlockHeapCreate(sizeof(struct client), HEAP_CLIENT);
        user_heap = BlockHeapCreate(sizeof(struct user), HEAP_USER);
        server_heap = BlockHeapCreate(sizeof(struct server), HEAP_SERVER);
	host_heap = BlockHeapCreate(sizeof(struct host_entry), HEAP_CLIENT);
	uhost_heap = BlockHeapCreate(sizeof(struct uhost_entry), HEAP_CLIENT);

	add_scommand_handler(&kill_command);
	add_scommand_handler(&nick_command);
	add_scommand_handler(&quit_command);
	add_scommand_handler(&server_command);
	add_scommand_handler(&squit_command);

#ifdef EXTENDED_HOSTHASH
	eventAdd("cleanup_hosts", cleanup_hosts, NULL, 21600);
#endif
}

/* hash_name()
 *   hashes a nickname
 *
 * inputs       - nickname to hash
 * outputs      - hash value of nickname
 */
unsigned int
hash_name(const char *p)
{
	unsigned int h = 0;

	while(*p)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*p++));
	}

	return(h & (MAX_NAME_HASH-1));
}

/* add_client()
 *   adds a client to the hashtable
 *
 * inputs       - client to add
 * outputs      -
 */
void
add_client(struct client *target_p)
{
	unsigned int hashv = hash_name(target_p->name);
	dlink_add(target_p, &target_p->nameptr, &name_table[hashv]);
}

/* del_client()
 *   removes a client from the hashtable
 *
 * inputs       - client to remove
 * outputs      -
 */
void
del_client(struct client *target_p)
{
	unsigned int hashv = hash_name(target_p->name);
	dlink_delete(&target_p->nameptr, &name_table[hashv]);
}

static void
add_host(struct client *target_p)
{
	struct host_entry *hptr;
	struct uhost_entry *uhost_p;
	unsigned int hashv = hash_name(target_p->user->host);
	dlink_node *ptr;
	int found = 0;

	DLINK_FOREACH(ptr, host_table[hashv].head)
	{
		hptr = ptr->data;

		if(!strcasecmp(hptr->host, target_p->user->host))
		{
			found = 1;
			break;
		}
	}

	if(!found)
	{
		hptr = BlockHeapAlloc(host_heap);
		memset(hptr, 0, sizeof(struct host_entry));

#ifdef EXTENDED_HOSTHASH
		strlcpy(hptr->host, target_p->user->host, sizeof(hptr->host));
#else
		hptr->host = target_p->user->host;
#endif

		dlink_add(hptr, &hptr->hashptr, &host_table[hashv]);
	}

	dlink_add(target_p, &target_p->user->hostptr, &hptr->users);

#ifdef EXTENDED_HOSTHASH
	if(dlink_list_length(&hptr->users) > hptr->max_clients)
	{
		hptr->max_clients = dlink_list_length(&hptr->users);
		hptr->maxc_time = CURRENT_TIME;
	}
#endif
	
	found = 0;

	DLINK_FOREACH(ptr, hptr->uhosts.head)
	{
		uhost_p = ptr->data;

		if(!strcasecmp(uhost_p->username, target_p->user->username))
		{
			found = 1;
			break;
		}
	}

	if(!found)
	{
		uhost_p = BlockHeapAlloc(uhost_heap);
		memset(uhost_p, 0, sizeof(struct uhost_entry));

		uhost_p->username = target_p->user->username;
		dlink_add(uhost_p, &uhost_p->node, &hptr->uhosts);
	}

	dlink_add(target_p, &target_p->user->uhostptr, &uhost_p->users);

#ifdef EXTENDED_HOSTHASH
	if(dlink_list_length(&hptr->uhosts) > hptr->max_unique)
	{
		hptr->max_unique = dlink_list_length(&hptr->uhosts);
		hptr->maxu_time = CURRENT_TIME;
	}
#endif
}

static void
del_host(struct client *target_p)
{
	struct host_entry *hptr;
	struct uhost_entry *uhost_p;
	unsigned int hashv = hash_name(target_p->user->host);
	dlink_node *ptr;
	dlink_node *uptr;

	DLINK_FOREACH(ptr, host_table[hashv].head)
	{
		hptr = ptr->data;

		if(strcasecmp(hptr->host, target_p->user->host))
			continue;

		DLINK_FOREACH(uptr, hptr->uhosts.head)
		{
			uhost_p = uptr->data;

			if(strcasecmp(uhost_p->username, target_p->user->username))
				continue;

			dlink_delete(&target_p->user->uhostptr,
					&uhost_p->users);

			if(dlink_list_length(&uhost_p->users))
			{
				struct client *client_p;

				client_p = ((dlink_node *)(uhost_p->users.head))->data;
				uhost_p->username = client_p->user->username;
			}
			else
			{
				dlink_delete(&uhost_p->node, &hptr->uhosts);
				BlockHeapFree(uhost_heap, uhost_p);
			}

			break;
		}

		dlink_delete(&target_p->user->hostptr, &hptr->users);

#ifndef EXTENDED_HOSTHASH
		if(dlink_list_length(&hptr->users))
		{
			struct client *client_p;

			client_p = ((dlink_node *)(hptr->users.head))->data;
			hptr->host = client_p->user->host;
		}
		else
		{
			dlink_delete(&hptr->hashptr, &host_table[hashv]);
			BlockHeapFree(host_heap, hptr);
		}
#else
		hptr->last_used = CURRENT_TIME;
#endif

		break;
	}
}

void
cleanup_hosts(void *unused)
{
	struct host_entry *host_p;
	dlink_node *ptr;
	dlink_node *next_ptr;
	int i;

	for(i = 0; i < MAX_NAME_HASH; i++)
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, host_table[i].head)
		{
			host_p = ptr->data;

			/* not used atm, hasnt been used in a week.. */
			if(!dlink_list_length(&host_p->users) &&
			   (host_p->last_used + 604800) <= CURRENT_TIME)
			{
				dlink_delete(&host_p->hashptr, &host_table[i]);
				BlockHeapFree(host_heap, host_p);
			}
		}
	}
}

struct host_entry *
find_host(const char *host)
{
	struct host_entry *host_p;
	unsigned int hashv = hash_name(host);
	dlink_node *ptr;

	DLINK_FOREACH(ptr, host_table[hashv].head)
	{
		host_p = ptr->data;

		if(!strcasecmp(host_p->host, host))
			return host_p;
	}

	return NULL;
}

/* find_client()
 *   finds a client [user/server/service] from the hashtable
 *
 * inputs       - name of client to find
 * outputs      - struct of client, or NULL if not found
 */
struct client *
find_client(const char *name)
{
	struct client *target_p;
	dlink_node *ptr;
	unsigned int hashv = hash_name(name);

	DLINK_FOREACH(ptr, name_table[hashv].head)
	{
		target_p = ptr->data;

		if(!irccmp(target_p->name, name))
			return target_p;
	}

	return NULL;
}

/* find_user()
 *   finds a user from the hashtable
 *
 * inputs       - name of user to find
 * outputs      - struct client of user, or NULL if not found
 */
struct client *
find_user(const char *name)
{
	struct client *target_p = find_client(name);

	if(target_p != NULL && IsUser(target_p))
		return target_p;

	return NULL;
}

/* find_server()
 *   finds a server from the hashtable
 *
 * inputs       - name of server to find
 * outputs      - struct client of server, or NULL if not found
 */
struct client *
find_server(const char *name)
{
	struct client *target_p = find_client(name);

	if(target_p != NULL && IsServer(target_p))
		return target_p;

	return NULL;
}

/* find_service()
 *   finds a service from the hashtable
 *
 * inputs       - name of service to find
 * outputs      - struct client of service, or NULL if not found
 */
struct client *
find_service(const char *name)
{
	struct client *target_p = find_client(name);

	if(target_p != NULL && IsService(target_p))
		return target_p;

	return NULL;
}

/* exit_user()
 *   exits a user, removing them from channels and lists
 *
 * inputs       - client to exit
 * outputs      -
 */
static void
exit_user(struct client *target_p)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(IsDead(target_p))
		return;

	SetDead(target_p);

	del_host(target_p);

	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->user->channels.head)
	{
		del_chmember(ptr->data);
	}

	dlink_move_node(&target_p->listnode, &user_list, &exited_list);
	dlink_delete(&target_p->upnode, &target_p->uplink->server->users);
}

/* exit_server()
 *   exits a server, removing their dependencies
 *
 * inputs       - client to exit
 * outputs      -
 */
static void
exit_server(struct client *target_p)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(IsDead(target_p))
		return;

	SetDead(target_p);

        /* first exit each of this servers users */
	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->server->users.head)
	{
		exit_client(ptr->data);
	}

        /* then exit each of their servers.. */
	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->server->servers.head)
	{
		exit_client(ptr->data);
	}

	dlink_move_node(&target_p->listnode, &server_list, &exited_list);

	/* if it has an uplink, remove it from its uplinks list */
	if(target_p->uplink != NULL)
		dlink_delete(&target_p->upnode, &target_p->uplink->server->servers);
}

/* exit_client()
 *   exits a generic client, calling functions specific for that client
 *
 * inputs       - client to exit
 * outputs      -
 */
void
exit_client(struct client *target_p)
{
        s_assert(!IsService(target_p));

        if(IsService(target_p))
                return;

	if(IsServer(target_p))
		exit_server(target_p);
	else if(IsUser(target_p))
		exit_user(target_p);

	del_client(target_p);
}

/* free_client()
 *   frees the memory in use by a client
 *
 * inputs       - client to free
 * outputs      -
 */
void
free_client(struct client *target_p)
{
        if(target_p->user != NULL)
                BlockHeapFree(user_heap, target_p->user);

	if(target_p->server != NULL)
	        BlockHeapFree(server_heap, target_p->server);

	BlockHeapFree(client_heap, target_p);
};

/* string_to_umode()
 *   Converts a given string into a usermode
 *
 * inputs       - string to convert, current usermodes
 * outputs      - new usermode
 */
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

		p++;
	}

	return umode;
}

/* umode_to_string()
 *   converts a usermode into string form
 *
 * inputs       - usermode to convert
 * outputs      - usermode in string form
 */
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

/* c_nick()
 *   the NICK handler
 */
void
c_nick(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;
	struct client *uplink_p;
	time_t newts;

        s_assert((parc == 3) || (parc == 9));

        if(parc != 9 && parc != 3)
                return;

        /* new client being introduced */
	if(parc == 9)
	{
		target_p = find_client(parv[1]);
		uplink_p = find_server(parv[7]);
		newts = atol(parv[2]);

                /* something already exists with this nick */
		if(target_p != NULL)
		{
                        s_assert(!IsServer(target_p));

                        if(IsServer(target_p))
                                return;

			if(IsUser(target_p))
			{
                                /* our uplink shouldve dealt with this. */
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
					die("service fight");

				return;
			}
		}

		target_p = BlockHeapAlloc(client_heap);
		memset(target_p, 0, sizeof(struct client));

		target_p->user = BlockHeapAlloc(user_heap);
		memset(target_p->user, 0, sizeof(struct user));

		target_p->uplink = uplink_p;

		strlcpy(target_p->name, parv[1], sizeof(target_p->name));
		strlcpy(target_p->user->username, parv[5], 
			sizeof(target_p->user->username));
		strlcpy(target_p->user->host, parv[6], 
                        sizeof(target_p->user->host));
                strlcpy(target_p->info, parv[8], sizeof(target_p->info));

		target_p->user->servername = uplink_p->name;
		target_p->user->tsinfo = newts;
		target_p->user->umode = string_to_umode(parv[4], 0);

		add_client(target_p);
		add_host(target_p);
		dlink_add(target_p, &target_p->listnode, &user_list);
		dlink_add(target_p, &target_p->upnode, &uplink_p->server->users);
	}

        /* client changing nicks */
	else if(parc == 3)
	{
		s_assert(IsUser(client_p));

                if(!IsUser(client_p))
                        return;

		del_client(client_p);
		strlcpy(client_p->name, parv[1], sizeof(client_p->name));
		add_client(client_p);

		client_p->user->tsinfo = atol(parv[2]);
	}
}

/* c_quit()
 *   the QUIT handler
 */
void
c_quit(struct client *client_p, const char *parv[], int parc)
{
	if(!IsUser(client_p))
	{
		slog("PROTO: QUIT received from server %s", client_p->name);
		return;
	}

	exit_client(client_p);
}

/* c_kill()
 *   the KILL handler
 */
void
c_kill(struct client *client_p, const char *parv[], int parc)
{
	static time_t first_kill = 0;
	static int num_kill = 0;
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
		if(IsUser(client_p))
			slog("service %s killed by %s!%s@%s{%s}",
				target_p->name, client_p->name, 
				client_p->user->username, client_p->user->host,
				client_p->user->servername);
		else
			slog("service %s killed by %s",
				target_p->name, client_p->name);

		/* no kill in the last 20 seconds, reset. */
		if((first_kill + 20) < CURRENT_TIME)
		{
			first_kill = CURRENT_TIME;
			num_kill = 1;
		}
                /* 20 kills in 20 seconds.. service fight. */
		else if(num_kill > 20)
			die("service kill fight!");

		num_kill++;
		introduce_service(target_p);
		return;
	}

        /* its a user, just exit them */
	exit_client(target_p);
}

/* c_server()
 *   the SERVER handler
 */
void
c_server(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;
	static char default_gecos[] = "(Unknown Location)";

	if(parc < 4)
		return;

        /* our uplink introducing themselves */
        if(client_p == NULL)
        {
                if(irccmp(server_p->name, parv[1]))
                {
                        slog("Connection to server %s failed: "
                             "(Servername mismatch)",
                             server_p->name);
                        (server_p->io_close)(server_p);
                        return;
                }

                server_p->flags &= ~CONN_HANDSHAKE;
                server_p->first_time = CURRENT_TIME;
        }

	target_p = BlockHeapAlloc(client_heap);
	memset(target_p, 0, sizeof(struct client));

	target_p->server = BlockHeapAlloc(server_heap);
	memset(target_p->server, 0, sizeof(struct server));

	strlcpy(target_p->name, parv[1], sizeof(target_p->name));
	strlcpy(target_p->info, EmptyString(parv[3]) ? default_gecos : parv[3],
		sizeof(target_p->info));

	target_p->server->hops = atoi(parv[2]);

	/* this server has an uplink */
	if(client_p != NULL)
	{
		target_p->uplink = client_p;
		dlink_add(target_p, &target_p->upnode, 
                          &client_p->server->servers);
	}
	/* its connected to us */
	else
		server_p->client_p = target_p;

	add_client(target_p);
	dlink_add(target_p, &target_p->listnode, &server_list);
}

/* c_squit()
 *   the SQUIT handler
 */
void
c_squit(struct client *client_p, const char *parv[], int parc)
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
