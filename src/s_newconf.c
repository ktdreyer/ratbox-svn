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
#include "hostmask.h"

dlink_list shared_conf_list;
dlink_list cluster_conf_list;
dlink_list oper_conf_list;

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

	DLINK_FOREACH_SAFE(ptr, next_ptr, shared_conf_list.head)
	{
		free_shared_conf(ptr->data);
		dlinkDestroy(ptr, &shared_conf_list);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, cluster_conf_list.head)
	{
		free_shared_conf(ptr->data);
		dlinkDestroy(ptr, &cluster_conf_list);
	}
}

int
find_shared_conf(const char *username, const char *host, 
		const char *server, int flags)
{
	struct shared_conf *shared_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, shared_conf_list.head)
	{
		shared_p = ptr->data;

		if((shared_p->flags & flags) == 0)
			continue;

		if(match(shared_p->username, username) &&
		   match(shared_p->host, host) &&
		   match(shared_p->server, server))
			return YES;

	}

	DLINK_FOREACH(ptr, cluster_conf_list.head)
	{
		shared_p = ptr->data;

		if((shared_p->flags & flags) == 0)
			continue;

		if(match(shared_p->server, server))
			return YES;
	}

	return NO;
}

struct oper_conf *
make_oper_conf(void)
{
	struct oper_conf *oper_p = MyMalloc(sizeof(struct oper_conf));
	return oper_p;
}

void
free_oper_conf(struct oper_conf *oper_p)
{
	s_assert(oper_p != NULL);
	if(oper_p == NULL)
		return;

	MyFree(oper_p->username);
	MyFree(oper_p->host);
	MyFree(oper_p->name);

#ifdef HAVE_LIBCRYPTO
	MyFree(oper_p->rsa_pubkey_file);

	if(oper_p->rsa_pubkey)
		RSA_free(oper_p->rsa_pubkey);
#endif

	MyFree(oper_p);
}

void
clear_oper_conf(void)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, oper_conf_list.head)
	{
		free_oper_conf(ptr->data);
		dlinkDestroy(ptr, &oper_conf_list);
	}
}

struct oper_conf *
find_oper_conf(const char *username, const char *host, const char *locip, const char *name)
{
	struct oper_conf *oper_p;
	struct sockaddr_storage ip, cip;
	char addr[HOSTLEN+1];
	int bits, cbits;
	dlink_node *ptr;

	parse_netmask(locip, &cip, &cbits);

	DLINK_FOREACH(ptr, oper_conf_list.head)
	{
		oper_p = ptr->data;

		/* name/username doesnt match.. */
		if(irccmp(oper_p->name, name) || !match(oper_p->username, username))
			continue;

		strlcpy(addr, oper_p->host, sizeof(addr));

		if(parse_netmask(addr, &ip, &bits) != HM_HOST)
		{
			if(ip.ss_family != cip.ss_family)
				continue;

			if(!comp_with_mask_sock(&ip, &cip, bits))
				continue;
		}
		else if(!match(oper_p->host, host))
			continue;

		return oper_p;
	}

	return NULL;
}

struct oper_flags
{
	int flag;
	char has;
	char hasnt;
};
static struct oper_flags oper_flagtable[] =
{
	{ OPER_GLINE,		'G', 'g' },
	{ OPER_KLINE,		'K', 'k' },
	{ OPER_XLINE,		'X', 'x' },
	{ OPER_GLOBKILL,	'O', 'o' },
	{ OPER_LOCKILL,		'C', 'c' },
	{ OPER_REMOTE,		'R', 'r' },
	{ OPER_UNKLINE,		'U', 'u' },
	{ OPER_REHASH,		'H', 'h' },
	{ OPER_DIE,		'D', 'd' },
	{ OPER_ADMIN,		'A', 'a' },
	{ OPER_NICKS,		'N', 'n' },
	{ OPER_OPERWALL,	'L', 'l' },
	{ 0,			'\0', '\0' }
};

const char *
get_oper_privs(int flags)
{
	static char buf[14];
	char *p;
	int i;

	p = buf;

	for(i = 0; oper_flagtable[i].flag; i++)
	{
		if(flags & oper_flagtable[i].flag)
			*p++ = oper_flagtable[i].has;
		else
			*p++ = oper_flagtable[i].hasnt;
	}

	*p = '\0';

	return buf;
}

