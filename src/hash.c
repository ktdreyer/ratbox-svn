/*
 *  ircd-ratbox: A slightly useful ircd.
 *  hash.c: Maintains hashtables.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "ircd_defs.h"
#include "tools.h"
#include "s_conf.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "fdlist.h"
#include "fileio.h"
#include "memory.h"
#include "msg.h"
#include "s_newconf.h"
#include "cache.h"

/* New hash code */
/*
 * Contributed by James L. Davis
 */

static dlink_list *clientTable;
static dlink_list *channelTable;
static dlink_list *idTable;
static dlink_list *resvTable;
static dlink_list *hostTable;
static dlink_list *xlineTable;
static dlink_list *helpTable;

/*
 * look in whowas.c for the missing ...[WW_MAX]; entry
 */

/*
 * Hashing.
 *
 *   The server uses a chained hash table to provide quick and efficient
 * hash table maintenance (providing the hash function works evenly over
 * the input range).  The hash table is thus not susceptible to problems
 * of filling all the buckets or the need to rehash.
 *    It is expected that the hash table would look something like this
 * during use:
 *                   +-----+    +-----+    +-----+   +-----+
 *                ---| 224 |----| 225 |----| 226 |---| 227 |---
 *                   +-----+    +-----+    +-----+   +-----+
 *                      |          |          |
 *                   +-----+    +-----+    +-----+
 *                   |  A  |    |  C  |    |  D  |
 *                   +-----+    +-----+    +-----+
 *                      |
 *                   +-----+
 *                   |  B  |
 *                   +-----+
 *
 * A - GOPbot, B - chang, C - hanuaway, D - *.mu.OZ.AU
 *
 * The order shown above is just one instant of the server. 
 */

/* init_hash()
 *
 * clears the various hashtables
 */
void
init_hash(void)
{
	clientTable = MyMalloc(sizeof(dlink_list) * U_MAX);
	idTable = MyMalloc(sizeof(dlink_list) * U_MAX);
	channelTable = MyMalloc(sizeof(dlink_list) * CH_MAX);
	hostTable = MyMalloc(sizeof(dlink_list) * HOST_MAX);
	resvTable = MyMalloc(sizeof(dlink_list) * R_MAX);
	xlineTable = MyMalloc(sizeof(dlink_list) * R_MAX);
	helpTable = MyMalloc(sizeof(dlink_list) * HELP_MAX);
}

/* hash_nick()
 *
 * hashes a nickname, first converting to lowercase
 */
static unsigned int
hash_nick(const char *name)
{
	unsigned int h = 0;

	while (*name)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*name++));
	}

	return (h & (U_MAX - 1));
}

/* hash_id()
 *
 * hashes an id, case is kept
 */
static unsigned int
hash_id(const char *name)
{
	unsigned int h = 0;

	while (*name)
	{
		h = (h << 4) - (h + (unsigned char) ToUpper(*name++));
	}

	return (h & (U_MAX - 1));
}

/* hash_channel()
 *
 * hashes a channel name, based on first 30 chars only for efficiency
 */
static unsigned int
hash_channel(const char *name)
{
	int i = 30;
	unsigned int h = 0;

	while (*name && --i)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*name++));
	}

	return (h & (CH_MAX - 1));
}

/* hash_hostname()
 *
 * hashes a hostname, based on first 30 chars only, as thats likely to
 * be more dynamic than rest.
 */
static unsigned int
hash_hostname(const char *name)
{
	int i = 30;
	unsigned int h = 0;

	while (*name && --i)
		h = (h << 4) - (h + (unsigned char) ToLower(*name++));

	return (h & (HOST_MAX - 1));
}

/* hash_resv()
 *
 * hashes a resv channel name, based on first 30 chars only
 */
static unsigned int
hash_resv(const char *name)
{
	int i = 30;
	unsigned int h = 0;

	while (*name && --i)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*name++));
	}

	return (h & (R_MAX - 1));
}

/* hash_xline()
 *
 * hashes an xline, first converting to lowercase as xlines are
 * case insensitive
 */
static unsigned int
hash_xline(const char *name)
{
	unsigned int h = 0;

	while(*name)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*name++));
	}

	return (h & (R_MAX - 1));
}

static unsigned int
hash_help(const char *name)
{
	unsigned int h = 0;

	while(*name)
	{
		h += (unsigned int) (ToLower(*name++) & 0xDF);
	}

	return (h % HELP_MAX);
}

/* add_to_id_hash()
 *
 * adds an entry to the id hash table
 */
void
add_to_id_hash(const char *name, struct Client *client_p)
{
	unsigned int hashv;

	if(EmptyString(name) || (client_p == NULL))
		return;

	hashv = hash_id(name);
	dlinkAddAlloc(client_p, &idTable[hashv]);
}

/* add_to_client_hash()
 *
 * adds an entry (client/server) to the client hash table
 */
void
add_to_client_hash(const char *name, struct Client *client_p)
{
	unsigned int hashv;

	s_assert(name != NULL);
	s_assert(client_p != NULL);
	if(EmptyString(name) || (client_p == NULL))
		return;

	hashv = hash_nick(name);
	dlinkAddAlloc(client_p, &clientTable[hashv]);
}

/* add_to_hostname_hash()
 *
 * adds a client entry to the hostname hash table
 */
void
add_to_hostname_hash(const char *hostname, struct Client *client_p)
{
	unsigned int hashv;

	s_assert(hostname != NULL);
	s_assert(client_p != NULL);
	if(EmptyString(hostname) || (client_p == NULL))
		return;

	hashv = hash_hostname(hostname);
	dlinkAddAlloc(client_p, &hostTable[hashv]);
}

/* add_to_resv_hash()
 *
 * adds a resv channel entry to the resv hash table
 */
void
add_to_resv_hash(const char *name, struct rxconf *resv_p)
{
	unsigned int hashv;

	s_assert(!EmptyString(name));
	s_assert(resv_p != NULL);
	if(EmptyString(name) || resv_p == NULL)
		return;

	hashv = hash_resv(name);
	dlinkAddAlloc(resv_p, &resvTable[hashv]);
}

/* add_to_xline_hash()
 *
 * adds an xline to the xline hash table
 */
void
add_to_xline_hash(const char *name, struct rxconf *xconf)
{
	unsigned int hashv;

	if(EmptyString(name) || xconf == NULL)
		return;

	hashv = hash_xline(name);
	dlinkAddAlloc(xconf, &xlineTable[hashv]);
};

void
add_to_help_hash(const char *name, struct cachefile *hptr)
{
	unsigned int hashv;

	if(EmptyString(name) || hptr == NULL)
		return;

	hashv = hash_help(name);
	dlinkAddAlloc(hptr, &helpTable[hashv]);
}

/* del_from_id_hash()
 *
 * removes an id from the id hash table
 */
void
del_from_id_hash(const char *id, struct Client *client_p)
{
	unsigned int hashv;
	dlink_node *ptr;
	dlink_node *next_ptr;

	s_assert(id != NULL);
	s_assert(client_p != NULL);
	if(EmptyString(id) || client_p == NULL)
		return;

	hashv = hash_id(id);

	DLINK_FOREACH_SAFE(ptr, next_ptr, idTable[hashv].head)
	{
		if(client_p == ptr->data)
		{
			dlinkDestroy(ptr, &idTable[hashv]);
			return;
		}
	}
}

/* del_from_client_hash()
 *
 * removes a client/server from the client hash table
 */
void
del_from_client_hash(const char *name, struct Client *client_p)
{
	unsigned int hashv;
	dlink_node *ptr;
	dlink_node *next_ptr;

	/* no s_asserts, this can happen when removing a client that
	 * is unregistered.
	 */
	if(EmptyString(name) || client_p == NULL)
		return;

	hashv = hash_nick(name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, clientTable[hashv].head)
	{
		if(client_p == ptr->data)
		{
			dlinkDestroy(ptr, &clientTable[hashv]);

			return;
		}
	}
}

/* del_from_channel_hash()
 * 
 * removes a channel from the channel hash table
 */
void
del_from_channel_hash(const char *name, struct Channel *chptr)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	unsigned int hashv;

	s_assert(name != NULL);
	s_assert(chptr != NULL);

	if(EmptyString(name) || chptr == NULL)
		return;

	hashv = hash_channel(name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, channelTable[hashv].head)
	{
		if(chptr == ptr->data)
		{
			dlinkDestroy(ptr, &channelTable[hashv]);
			return;
		}
	}
}

/* del_from_hostname_hash()
 *
 * removes a client entry from the hostname hash table
 */
void
del_from_hostname_hash(const char *hostname, struct Client *client_p)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	unsigned int hashv;

	if(hostname == NULL || client_p == NULL)
		return;

	hashv = hash_hostname(hostname);

	DLINK_FOREACH_SAFE(ptr, next_ptr, hostTable[hashv].head)
	{
		if(client_p == ptr->data)
		{
			dlinkDestroy(ptr, &hostTable[hashv]);
			return;
		}
	}
}

/* del_from_resv_hash()
 *
 * removes a resv entry from the resv hash table
 */
void
del_from_resv_hash(const char *name, struct rxconf *rxptr)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	unsigned int hashv;

	s_assert(name != NULL);
	s_assert(rxptr != NULL);
	if(EmptyString(name) || rxptr == NULL)
		return;

	hashv = hash_resv(name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, resvTable[hashv].head)
	{
		if(rxptr == ptr->data)
		{
			dlinkDestroy(ptr, &resvTable[hashv]);
			return;
		}
	}
}

/* del_from_xline_hash()
 *
 * removes an xline from the xline hash table
 */
void
del_from_xline_hash(const char *name, struct rxconf *xconf)
{
	struct rxconf *acptr;
	dlink_node *ptr;
	dlink_node *next_ptr;
	unsigned int hashv;

	if(EmptyString(name) || (xconf == NULL))
		return;

	hashv = hash_xline(name);

	DLINK_FOREACH_SAFE(ptr, next_ptr, xlineTable[hashv].head)
	{
		acptr = ptr->data;

		if(xconf == acptr)
		{
			dlinkDestroy(ptr, &xlineTable[hashv]);
			return;
		}
	}
}

void
clear_help_hash(void)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	int i;

	for(i = 0; i < HELP_MAX; i++)
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, helpTable[i].head)
		{
			free_cachefile(ptr->data);
			free_dlink_node(ptr);
		}

		helpTable[i].head = helpTable[i].tail = NULL;
		helpTable[i].length = 0;
	}
}

/* find_id()
 *
 * finds a client entry from the id hash table
 */
struct Client *
find_id(const char *name)
{
	struct Client *target_p;
	dlink_node *ptr;
	unsigned int hashv;

	if(EmptyString(name))
		return NULL;

	hashv = hash_id(name);

	DLINK_FOREACH(ptr, idTable[hashv].head)
	{
		target_p = ptr->data;

		if(strcmp(name, target_p->id) == 0)
			return target_p;
	}

	return NULL;
}

/* hash_find_masked_server()
 * 
 * Whats happening in this next loop ? Well, it takes a name like
 * foo.bar.edu and proceeds to earch for *.edu and then *.bar.edu.
 * This is for checking full server names against masks although
 * it isnt often done this way in lieu of using matches().
 *
 * Rewrote to do *.bar.edu first, which is the most likely case,
 * also made const correct
 * --Bleep
 */
static struct Client *
hash_find_masked_server(const char *name)
{
	char buf[HOSTLEN + 1];
	char *p = buf;
	char *s;
	struct Client *server;

	if('*' == *name || '.' == *name)
		return NULL;

	/* copy it across to give us a buffer to work on */
	strlcpy(buf, name, sizeof(buf));

	while ((s = strchr(p, '.')) != 0)
	{
		*--s = '*';
		/*
		 * Dont need to check IsServer() here since nicknames cant
		 * have *'s in them anyway.
		 */
		if((server = find_server(s)))
			return server;
		p = s + 2;
	}

	return NULL;
}

/* find_any_client()
 *
 * finds a client/server/masked server entry from the hash
 */
struct Client *
find_any_client(const char *name)
{
	struct Client *target_p;
	dlink_node *ptr;
	unsigned int hashv;

	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	/* hunting for an id, not a nick */
	if(IsDigit(*name))
		return (find_id(name));

	hashv = hash_nick(name);

	DLINK_FOREACH(ptr, clientTable[hashv].head)
	{
		target_p = ptr->data;

		if(irccmp(name, target_p->name) == 0)
			return target_p;
	}

	/* wasnt found, look for a masked server */
	return hash_find_masked_server(name);
}

/* find_client()
 *
 * finds a client/server entry from the client hash table
 */
struct Client *
find_client(const char *name)
{
	struct Client *target_p;
	dlink_node *ptr;
	unsigned int hashv;

	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	/* hunting for an id, not a nick */
	if(IsDigit(*name))
		return (find_id(name));

	hashv = hash_nick(name);

	DLINK_FOREACH(ptr, clientTable[hashv].head)
	{
		target_p = ptr->data;

		if(irccmp(name, target_p->name) == 0)
			return target_p;
	}

	return NULL;
}

/* find_named_client()
 *
 * finds a client/server entry from the client hash table
 */
struct Client *
find_named_client(const char *name)
{
	struct Client *target_p;
	dlink_node *ptr;
	unsigned int hashv;

	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	hashv = hash_nick(name);

	DLINK_FOREACH(ptr, clientTable[hashv].head)
	{
		target_p = ptr->data;

		if(irccmp(name, target_p->name) == 0)
			return target_p;
	}

	return NULL;
}

/* find_server()
 *
 * finds a server from the client hash table
 */
struct Client *
find_server(const char *name)
{
	struct Client *target_p;
	dlink_node *ptr;
	unsigned int hashv;

	if(EmptyString(name))
		return NULL;

	hashv = hash_nick(name);

	DLINK_FOREACH(ptr, clientTable[hashv].head)
	{
		target_p = ptr->data;

		if((IsServer(target_p) || IsMe(target_p)) &&
		   irccmp(name, target_p->name) == 0)
				return target_p;
	}

	/* wasnt found, look for a masked server */
	return hash_find_masked_server(name);
}

/* find_hostname()
 *
 * finds a hostname dlink list from the hostname hash table.
 * we return the full dlink list, because you can have multiple
 * entries with the same hostname
 */
dlink_node *
find_hostname(const char *hostname)
{
	unsigned int hashv;

	if(EmptyString(hostname))
		return NULL;

	hashv = hash_hostname(hostname);

	return hostTable[hashv].head;
}

/* find_channel()
 *
 * finds a channel from the channel hash table
 */
struct Channel *
find_channel(const char *name)
{
	struct Channel *chptr;
	dlink_node *ptr;
	unsigned int hashv;

	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	hashv = hash_channel(name);

	DLINK_FOREACH(ptr, channelTable[hashv].head)
	{
		chptr = ptr->data;

		if(irccmp(name, chptr->chname) == 0)
			return chptr;
	}

	return NULL;
}

/*
 * get_or_create_channel
 * inputs       - client pointer
 *              - channel name
 *              - pointer to int flag whether channel was newly created or not
 * output       - returns channel block or NULL if illegal name
 *		- also modifies *isnew
 *
 *  Get Channel block for chname (and allocate a new channel
 *  block, if it didn't exist before).
 */
struct Channel *
get_or_create_channel(struct Client *client_p, const char *chname, int *isnew)
{
	struct Channel *chptr;
	dlink_node *ptr;
	unsigned int hashv;
	int len;
	const char *s = chname;

	if(EmptyString(s))
		return NULL;

	len = strlen(s);
	if(len > CHANNELLEN)
	{
		char *t;
		if(IsServer(client_p))
		{
			sendto_realops_flags(UMODE_DEBUG, L_ALL,
					     "*** Long channel name from %s (%d > %d): %s",
					     client_p->name, len, CHANNELLEN, s);
		}
		len = CHANNELLEN;
		t = LOCAL_COPY(s);
		*(t + CHANNELLEN) = '\0';
		s = t;
	}

	hashv = hash_channel(s);

	DLINK_FOREACH(ptr, channelTable[hashv].head)
	{
		chptr = ptr->data;

		if(irccmp(s, chptr->chname) == 0)
		{
			if(isnew != NULL)
				*isnew = 0;
			return chptr;
		}
	}

	if(isnew != NULL)
		*isnew = 1;

	chptr = allocate_channel();
	strlcpy(chptr->chname, s, sizeof(chptr->chname));

	dlinkAdd(chptr, &chptr->node, &global_channel_list);

	chptr->channelts = CurrentTime;	/* doesn't hurt to set it here */

	dlinkAddAlloc(chptr, &channelTable[hashv]);

	return chptr;
}

/* hash_find_resv()
 *
 * hunts for a resv entry in the resv hash table
 */
struct rxconf *
hash_find_resv(const char *name)
{
	struct rxconf *resv_p;
	dlink_node *ptr;
	unsigned int hashv;

	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	hashv = hash_resv(name);

	DLINK_FOREACH(ptr, resvTable[hashv].head)
	{
		resv_p = ptr->data;

		if(irccmp(name, resv_p->name) == 0)
			return resv_p;
	}

	return NULL;
}

/* hash_find_xline()
 *
 * hunts for an xline entry in the xline hash table
 */
struct rxconf *
hash_find_xline(const char *name)
{
	struct rxconf *xconf;
	dlink_node *ptr;
	unsigned int hashv;

	if(EmptyString(name))
		return NULL;

	hashv = hash_xline(name);

	DLINK_FOREACH(ptr, xlineTable[hashv].head)
	{
		xconf = ptr->data;

		if(irccmp(name, xconf->name) == 0)
			return xconf;
	}

	return NULL;
}

struct cachefile *
hash_find_help(const char *name, int flags)
{
	struct cachefile *hptr;
	dlink_node *ptr;
	unsigned int hashv;

	if(EmptyString(name))
		return NULL;

	hashv = hash_help(name);

	DLINK_FOREACH(ptr, helpTable[hashv].head)
	{
		hptr = ptr->data;

		if((irccmp(name, hptr->name) == 0) &&
		   (hptr->flags & flags))
			return hptr;
	}

	return NULL;
}

