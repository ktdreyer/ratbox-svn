/*
 *  ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 *  iplines.c:  Implements IP based matching and banlists
 *
 *  Copyright (C) 2001-2002 Aaron Sethman <androsyn@ratbox.org> 
 *  Copyright (C) 2001-2002 Hybrid Development Team
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
#include "config.h"
#include "patricia.h"
#include "s_conf.h"
#include "iplines.h"


#ifdef IPV6
static int bitlen = 128;
#else
static int bitlen = 32;
#endif


#define NUM_TYPES 5

enum
{
	elines = 0,
	ilines = 1,
	dlines = 2,
	klines = 3,
	glines = 4
};

static patricia_tree_t *iplines[NUM_TYPES];

static patricia_node_t *
add_line(struct ConfItem *aconf, int type, struct irc_inaddr *addr, int cidr)
{
	patricia_node_t *pnode;
	pnode = make_and_lookup_ip(iplines[type], DEF_FAM, addr, cidr);
	aconf->pnode = pnode;
	pnode->data = aconf;
	return (pnode);
}

static int
change_types(int type)
{
	if(type & CONF_DLINE)
		return (dlines);
	if(type & CONF_EXEMPTDLINE)
		return (elines);
	if(type & CONF_KILL)
		return (klines);
	if(type & CONF_GLINE)
		return (glines);
	if(type & CONF_CLIENT)
		return (ilines);
	return -1;
}


void
init_iplines(void)
{
	int x;
	for (x = 0; x < NUM_TYPES; x++)
	{
		iplines[x] = New_Patricia(bitlen);
	}
}

void
clear_iplines(void)
{
	int x;
	struct ConfItem *aconf;
	patricia_node_t *pnode;

	for (x = 0; x < NUM_TYPES; x++)
	{
		PATRICIA_WALK(iplines[x]->head, pnode)
		{
			aconf = (struct ConfItem *) pnode->data;
			if(aconf->flags & CONF_FLAGS_TEMPORARY)
				continue;
			aconf->status |= CONF_ILLEGAL;
			patricia_remove(iplines[x], aconf->pnode);
			if(!aconf->clients)
				free_conf(aconf);
		}
		PATRICIA_WALK_END;

	}
}

int
add_ipline(struct ConfItem *aconf, int type, struct irc_inaddr *addr, int cidr)
{
	int ourtype;
	patricia_node_t *pnode;

	ourtype = change_types(type);

	if(ourtype == -1)
		return 0;
	pnode = add_line(aconf, ourtype, addr, cidr);
	if(pnode == NULL)
		return 0;

	return 1;
}

void
delete_ipline(struct ConfItem *aconf, int type)
{
	int ourtype;

	ourtype = change_types(type);

	if(ourtype == -1)
		return;

	patricia_remove(iplines[ourtype], aconf->pnode);
	if(!aconf->clients)
	{
		free_conf(aconf);
	}
}


static struct ConfItem *
find_ipline(int type, struct irc_inaddr *addr)
{
	patricia_node_t *pnode;
	pnode = match_ip(iplines[type], addr);
	if(pnode != NULL)
		return (struct ConfItem *) pnode->data;
	return NULL;
}

struct ConfItem *
find_generic_line(int type, struct irc_inaddr *addr)
{
	struct ConfItem *aconf;
	int ourtype = change_types(type);
	if(ourtype == -1)
		return NULL;
	aconf = find_ipline(ourtype, addr);
	return (aconf);
}


struct ConfItem *
find_ipdline(struct irc_inaddr *addr)
{
	struct ConfItem *aconf;
	aconf = find_ipline(elines, addr);
	if(aconf != NULL)
	{
		return aconf;
	}
	return (find_ipline(dlines, addr));
}

struct ConfItem *
find_ipiline(struct irc_inaddr *addr)
{
	return (find_ipline(ilines, addr));
}

struct ConfItem *
find_ipkline(struct irc_inaddr *addr)
{
	return (find_ipline(klines, addr));
}

struct ConfItem *
find_ipgline(struct irc_inaddr *addr)
{
	return (find_ipline(glines, addr));
}


void
report_dlines(struct Client *source_p)
{
	patricia_node_t *pnode;
	struct ConfItem *aconf;
	char *name, *host, *pass, *user, *classname;
	int port;
	PATRICIA_WALK(iplines[dlines]->head, pnode)
	{
		aconf = pnode->data;
		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			continue;
		get_printable_conf(aconf, &name, &host, &pass, &user, &port, &classname);
		sendto_one(source_p, form_str(RPL_STATSDLINE), me.name, source_p->name, 'D', host,
			   pass);
	}
	PATRICIA_WALK_END;

}

void
report_elines(struct Client *source_p)
{
	patricia_node_t *pnode;
	struct ConfItem *aconf;
	int port;
	char *name, *host, *pass, *user, *classname;
	PATRICIA_WALK(iplines[elines]->head, pnode)
	{
		aconf = pnode->data;
		get_printable_conf(aconf, &name, &host, &pass, &user, &port, &classname);
		sendto_one(source_p, form_str(RPL_STATSDLINE), me.name, source_p->name, 'e', host,
			   pass);
	}
	PATRICIA_WALK_END;
}

void
report_ipKlines(struct Client *source_p)
{
	patricia_node_t *pnode;
	struct ConfItem *aconf;
	char *name, *host, *pass, *user, *classname;
	int port;
	PATRICIA_WALK(iplines[klines]->head, pnode)
	{
		aconf = pnode->data;
		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			continue;

		get_printable_conf(aconf, &name, &host, &pass, &user, &port, &classname);
		sendto_one(source_p, form_str(RPL_STATSKLINE), me.name,
			   source_p->name, 'K', host, user, pass);
	}
	PATRICIA_WALK_END;
}


void
report_ipGlines(struct Client *source_p)
{
	patricia_node_t *pnode;
	struct ConfItem *aconf;
	char *name, *host, *pass, *user, *classname;
	int port;
	PATRICIA_WALK(iplines[glines]->head, pnode)
	{
		aconf = pnode->data;
		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			continue;

		get_printable_conf(aconf, &name, &host, &pass, &user, &port, &classname);
		sendto_one(source_p, form_str(RPL_STATSKLINE), me.name,
			   source_p->name, 'G', host, user, pass);
	}
	PATRICIA_WALK_END;
}



void
report_ipIlines(struct Client *source_p)
{
	patricia_node_t *pnode;
	struct ConfItem *aconf;
	char *name, *host, *pass, *user, *classname;
	int port;
	PATRICIA_WALK(iplines[elines]->head, pnode)
	{
		aconf = pnode->data;

		get_printable_conf(aconf, &name, &host, &pass, &user, &port, &classname);
		sendto_one(source_p, form_str(RPL_STATSILINE), me.name,
			   source_p->name, (IsConfRestricted(aconf)) ? 'i' : 'I', name,
			   show_iline_prefix(source_p, aconf, user),
#ifdef HIDE_SPOOF_IPS
			   IsConfDoSpoofIp(aconf) ? "255.255.255.255" :
#endif
			   host, port, classname);


	}
	PATRICIA_WALK_END;
}
