/*
 * ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 * s_newconf.c - code for dealing with conf stuff
 *
 * Copyright (C) 2004 Lee Hardy <lee@leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
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

dlink_list shared_list;
dlink_list cluster_list;

struct shared_conf *
make_shared_conf(void)
{
	struct shared_conf *shared_p = MyMalloc(sizeof(struct shared_conf));
	return shared_p;
}

void
free_shared_conf(struct shared_conf *shared_p)
{
	s_assert(shared_p != NULL);
	if(shared_p == NULL)
		return;

	MyFree(shared_p->username);
	MyFree(shared_p->host);
	MyFree(shared_p->server);
	MyFree(shared_p);
}

void
clear_shared_conf(void)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, shared_list.head)
	{
		free_shared_conf(ptr->data);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, cluster_list.head)
	{
		free_shared_conf(ptr->data);
	}

	shared_list.head = shared_list.tail = NULL;
	cluster_list.head = cluster_list.tail = NULL;
	shared_list.length = cluster_list.length = 0;
}

int
find_shared_conf(const char *username, const char *host, 
		const char *server, int flags)
{
	struct shared_conf *shared_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, shared_list.head)
	{
		shared_p = ptr->data;

		if((shared_p->flags & flags) == 0)
			continue;

		if(match(shared_p->username, username) &&
		   match(shared_p->host, host) &&
		   match(shared_p->server, server))
			return YES;

	}

	DLINK_FOREACH(ptr, cluster_list.head)
	{
		shared_p = ptr->data;

		if((shared_p->flags & flags) == 0)
			continue;

		if(match(shared_p->server, server))
			return YES;
	}

	return NO;
}

