/*
 *  ircd-ratbox: A slightly useful ircd
 *  confmatch.h: its the confmask.h header..what did you expect?
 *
 *  Copyright (C) 2002 Aaron Sethman <androsyn@ratbox.org>
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

#ifndef INCLUDE_confmask_h
#define INCLUDE_confmask_h 1

#include "client.h"

/* Hashtable stuff... */
#define ATABLE_SIZE 0x1000

void init_confmatch(void);
void add_conf_by_address (const char *address, int type, const char *username, struct ConfItem *aconf);
void delete_one_address_conf(struct ConfItem *aconf);
void clear_out_address_conf(void);

struct ConfItem * find_conf (struct Client *client_p , int type );
struct ConfItem * find_dline (struct irc_inaddr * );
struct ConfItem * find_address_conf (const char *host, const char *user, struct irc_inaddr *ip);
struct ConfItem * find_conf_by_address (const char *hostname, struct irc_inaddr *addr, int type, const char *username);
struct ConfItem * find_kline(struct Client *client_p);
struct ConfItem * find_gline(struct Client *client_p);
char * show_iline_prefix(struct Client *sptr, struct ConfItem *aconf, char *name);
void report_ilines(struct Client *);
void report_glines(struct Client *);
void report_klines(struct Client *);
 	              
int parse_netmask(const char *address, struct irc_inaddr *addr, int *bits);



#endif /* INCLUDE_confmatch_h */
