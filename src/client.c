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
#include "hook.h"
#include "s_userserv.h"
#include "conf.h"

static dlink_list name_table[MAX_NAME_HASH];
static dlink_list host_table[MAX_HOST_HASH];

dlink_list user_list;
dlink_list oper_list;
dlink_list server_list;
dlink_list exited_list;

static BlockHeap *client_heap;
static BlockHeap *user_heap;
static BlockHeap *server_heap;
static BlockHeap *host_heap;

static void cleanup_host_table(void *);

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

/* init_client()
 *   initialises various things
 */
void
init_client(void)
{
        client_heap = BlockHeapCreate(sizeof(struct client), HEAP_CLIENT);
        user_heap = BlockHeapCreate(sizeof(struct user), HEAP_USER);
        server_heap = BlockHeapCreate(sizeof(struct server), HEAP_SERVER);
	host_heap = BlockHeapCreate(sizeof(struct host_entry), HEAP_HOST);

	eventAdd("cleanup_host_table", cleanup_host_table, NULL, 3600);

	add_scommand_handler(&kill_command);
	add_scommand_handler(&nick_command);
	add_scommand_handler(&quit_command);
	add_scommand_handler(&server_command);
	add_scommand_handler(&squit_command);
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

static unsigned int
hash_host(const char *p)
{
	unsigned int h = 0;

	while(*p)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*p++));
	}

	return (h & (MAX_HOST_HASH - 1));
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

/* cleanup_host_table()
 *   Walks the hostname hash, cleaning out any entries that have expired
 *
 * inputs	- 
 * outputs	- 
 */
static void
cleanup_host_table(void *unused)
{
	struct host_entry *hent;
	dlink_node *ptr, *next_ptr;
	int i;

	HASH_WALK_SAFE(i, MAX_HOST_HASH, ptr, next_ptr, host_table)
	{
		hent = ptr->data;

		if(hent->flood_expire < CURRENT_TIME &&
		   hent->cregister_expire < CURRENT_TIME &&
		   hent->uregister_expire < CURRENT_TIME)
		{
			dlink_delete(&hent->node, &host_table[i]);
			my_free(hent->name);
			BlockHeapFree(host_heap, hent);
		}
	}
	HASH_WALK_END
}

/* find_host()
 *   finds a host entry from the hashtable, adding it if not found
 *
 * inputs	- name of host to find
 * outputs	- host entry for this host
 */
struct host_entry *
find_host(const char *name)
{
	struct host_entry *hent;
	dlink_node *ptr;
	unsigned int hashv = hash_host(name);

	DLINK_FOREACH(ptr, host_table[hashv].head)
	{
		hent = ptr->data;

		if(!irccmp(hent->name, name))
			return hent;
	}

	hent = BlockHeapAlloc(host_heap);
	hent->name = my_strdup(name);
	dlink_add(hent, &hent->node, &host_table[hashv]);

	return hent;
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

#ifdef ENABLE_USERSERV
	if(target_p->user->user_reg)
		dlink_find_destroy(target_p, &target_p->user->user_reg->users);
#endif

	if(target_p->user->oper)
	{
		dlink_find_destroy(target_p, &oper_list);
		deallocate_conf_oper(target_p->user->oper);
	}

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
	{
		my_free(target_p->user->mask);
                BlockHeapFree(user_heap, target_p->user);
	}

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
	static char buf[BUFSIZE];
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
					mlog("PROTO: NICK %s with higher TS introduced causing collision.",
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
		target_p->user = BlockHeapAlloc(user_heap);

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

		snprintf(buf, sizeof(buf), "%s!%s@%s",
			target_p->name, target_p->user->username, 
			target_p->user->host);
		target_p->user->mask = my_strdup(buf);

		add_client(target_p);
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
		mlog("PROTO: QUIT received from server %s", client_p->name);
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
		mlog("PROTO: KILL received for server %s", target_p->name);
		return;
	}

	/* grmbl. */
	if(IsService(target_p))
	{
		if(IsUser(client_p))
			mlog("service %s killed by %s!%s@%s{%s}",
				target_p->name, client_p->name, 
				client_p->user->username, client_p->user->host,
				client_p->user->servername);
		else
			mlog("service %s killed by %s",
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

		/* has to be done because introduce_service() calls
		 * add_client()
		 */
		del_client(target_p);
		introduce_service(target_p);
		introduce_service_channels(target_p);

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
		if(!ConnTS(server_p))
		{
			mlog("Connection to server %s failed: "
				"(Protocol mismatch)",
				server_p->name);
			(server_p->io_close)(server_p);
			return;
		}

                if(irccmp(server_p->name, parv[1]))
                {
                        mlog("Connection to server %s failed: "
                             "(Servername mismatch)",
                             server_p->name);
                        (server_p->io_close)(server_p);
                        return;
                }

                ClearConnHandshake(server_p);
                server_p->first_time = CURRENT_TIME;
        }

	target_p = BlockHeapAlloc(client_heap);
	target_p->server = BlockHeapAlloc(server_heap);

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

	sendto_server(":%s PING %s %s", MYNAME, MYNAME, target_p->name);
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
		/* returns -1 if it handled it */
		if(hook_call(HOOK_SQUIT_UNKNOWN, (void *) parv[1], NULL) == 0)
			mlog("PROTO: SQUIT for unknown server %s", parv[1]);

		return;
	}

	exit_client(target_p);
}
