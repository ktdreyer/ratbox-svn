/*
 * ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 * s_newconf.c - code for dealing with conf stuff like k/d/x lines
 *
 * Copyright (C) 2002-2003 Lee Hardy <lee@leeh.co.uk>
 * Copyright (C) 2002-2003 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 */

#include "stdinc.h"
#include "ircd_defs.h"
#include "common.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "tools.h"
#include "client.h"
#include "memory.h"
#include "s_serv.h"
#include "send.h"
#include "balloc.h"
#include "event.h"
#include "hash.h"

static BlockHeap *xline_heap = NULL;
static BlockHeap *shared_heap = NULL;

dlink_list xline_list;
dlink_list xline_hash_list;
dlink_list shared_list;
dlink_list encap_list;

/* conf_heap_gc()
 *
 * inputs       -
 * outputs      -
 * side effects - garbage collection of blockheaps
 */
static void
conf_heap_gc(void *unused)
{
	BlockHeapGarbageCollect(xline_heap);
	BlockHeapGarbageCollect(shared_heap);
}

/* init_conf()
 *
 * inputs       -
 * outputs      -
 * side effects - loads some things for this file
 */
void
init_conf(void)
{
	xline_heap = BlockHeapCreate(sizeof(struct xline), XLINE_HEAP_SIZE);
	shared_heap = BlockHeapCreate(sizeof(struct shared), SHARED_HEAP_SIZE);
	eventAddIsh("conf_heap_gc", conf_heap_gc, NULL, 600);
}

/* make_xline()
 *
 * inputs       - gecos, reason, type
 * outputs      -
 * side effects - creates an xline based on information
 */
struct xline *
make_xline(const char *gecos, const char *reason, int type)
{
	struct xline *xconf;

	xconf = BlockHeapAlloc(xline_heap);
	memset(xconf, 0, sizeof(struct xline));

	if(!EmptyString(gecos))
		DupString(xconf->gecos, gecos);

	if(!EmptyString(reason))
		DupString(xconf->reason, reason);

	xconf->type = type;
	xconf->flags = 0;
	return xconf;
}

/* add_xline()
 *
 * inputs	- pointer to xline
 * outputs	-
 * side effects - adds the xline to either the hash, or dlink
 */
void
add_xline(struct xline *xconf)
{
	/* no wildcards, add to the hash */
	if((strchr(xconf->gecos, '?') == NULL) && 
	   (strchr(xconf->gecos, '*') == NULL))
	{
		xconf->flags = XLINE_PLAIN;
		add_to_xline_hash(xconf->gecos, xconf);
		dlinkAddAlloc(xconf, &xline_hash_list);
	}

	/* otherwise it goes in the dlink */
	else
	{
		xconf->flags = XLINE_WILD;
		dlinkAddAlloc(xconf, &xline_list);
	}
}
	
/* free_xline()
 *
 * inputs       - pointer to xline to free
 * outputs      -
 * side effects - xline is freed
 */
void
free_xline(struct xline *xconf)
{
	assert(xconf != NULL);
	if(xconf == NULL)
		return;

	MyFree(xconf->gecos);
	MyFree(xconf->reason);

	BlockHeapFree(xline_heap, xconf);
}

/* clear_xlines()
 *
 * inputs       -
 * outputs      -
 * side effects - nukes list of xlines
 */
void
clear_xlines(void)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	struct xline *xconf;

	/* this is more optimised for clearing the whole list */
	DLINK_FOREACH_SAFE(ptr, next_ptr, xline_list.head)
	{
		free_xline(ptr->data);
		free_dlink_node(ptr);
	}

	xline_list.head = xline_list.tail = NULL;
	xline_list.length = 0;

	DLINK_FOREACH_SAFE(ptr, next_ptr, xline_hash_list.head)
	{
		xconf = ptr->data;

		del_from_xline_hash(xconf->gecos, xconf);
		free_xline(ptr->data);
		free_dlink_node(ptr);
	}

	xline_hash_list.head = xline_hash_list.tail = NULL;
	xline_hash_list.length = 0;
}

/*
 * find_xline
 *
 * inputs       - pointer to char string to find
 * output       - NULL or pointer to found xline
 * side effects - looks for a match on name field
 */
struct xline *
find_xline(const char *gecos)
{
	struct xline *xconf;
	dlink_node *ptr;

	/* first hunt the hash.. */
	xconf = hash_find_xline(gecos);

	if(xconf != NULL)
		return xconf;

	/* then hunt the less efficient dlink list */
	DLINK_FOREACH(ptr, xline_list.head)
	{
		xconf = ptr->data;

		if(match_esc(xconf->gecos, gecos))
			return xconf;
	}

	return NULL;
}

/* make_shared()
 *
 * inputs       -
 * outputs      -
 * side effects - creates a shared block
 */
struct shared *
make_shared(void)
{
	struct shared *uconf;

	uconf = BlockHeapAlloc(shared_heap);
	memset(uconf, 0, sizeof(struct shared));

	return uconf;
}

/* free_shared()
 *
 * inputs       - shared block to free
 * outputs      -
 * side effects - shared block is freed.
 */
void
free_shared(struct shared *uconf)
{
	assert(uconf != NULL);
	if(uconf == NULL)
		return;

	MyFree(uconf->username);
	MyFree(uconf->host);
	MyFree(uconf->servername);
	BlockHeapFree(shared_heap, uconf);
}

/* clear_shared()
 *
 * inputs       -
 * outputs      -
 * side effects - shared blocks are nuked
 */
void
clear_shared(void)
{
	struct shared *uconf;
	dlink_node *ptr;
	dlink_node *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, shared_list.head)
	{
		uconf = ptr->data;

		free_shared(uconf);
		dlinkDestroy(ptr, &shared_list);
	}
}

/* find_shared()
 *
 * inputs       - username, hostname, servername, type to find shared for
 * outputs      - YES if one found, else NO
 * side effects -
 */
int
find_shared(const char *username, const char *host, const char *servername, int flags)
{
	struct shared *uconf;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, shared_list.head)
	{
		uconf = ptr->data;

		if((uconf->flags & flags) == 0)
			continue;

		if((EmptyString(uconf->servername)
		    || match(uconf->servername, servername))
		   && (EmptyString(uconf->username)
		       || match(uconf->username, username))
		   && (EmptyString(uconf->host) || match(uconf->host, host)))
			return YES;
	}

	return NO;
}

/* add_encap()
 *
 * inputs	- token, handler, flags
 * outputs	- 0 on failure, 1 success
 * side effects - adds an encap entry for the token to the list
 */
int
add_encap(const char *name, void *handler, int flags)
{
	struct encap *enptr;

	if(find_encap(name) != NULL)
		return 0;

	enptr = (struct encap *) MyMalloc(sizeof(struct encap));

	DupString(enptr->name, name);
	enptr->handler = handler;
	enptr->flags = flags;

	dlinkAddAlloc(enptr, &encap_list);
	return 1;
}

/* del_encap()
 *
 * inputs	- token
 * outputs	- 0 on failure, 1 success
 * side effects - remove the given entry from the encap list
 */
int
del_encap(const char *name)
{
	struct encap *enptr;
	dlink_node *ptr;
	dlink_node *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, encap_list.head)
	{
		enptr = ptr->data;

		if(irccmp(enptr->name, name) == 0)
		{
			if(enptr->flags & ENCAP_PERM)
				return 0;

			MyFree(enptr->name);
			MyFree(enptr);
			dlinkDestroy(ptr, &encap_list);
			return 1;
		}
	}

	return 0;
}

/* find_encap()
 *
 * inputs	- token to find
 * outputs	- 0 on failure, 1 if found
 * side effects -
 */
struct encap *
find_encap(const char *name)
{
	struct encap *enptr;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, encap_list.head)
	{
		enptr = ptr->data;

		if(irccmp(enptr->name, name) == 0)
			return enptr;
	}

	return NULL;
}
