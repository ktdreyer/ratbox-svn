/*
 *  ircd-ratbox: A slightly useful ircd
 *  iplines.h: its the iplines.h header..what did you expect?
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

#ifndef INCLUDE_iplines_h
#define INCLUDE_iplines_h 1

#include "ircd_defs.h"
#include "s_conf.h"


void init_iplines (void);
struct ConfItem * find_ipdline (struct irc_inaddr *addr );
struct ConfItem * find_ipiline (struct irc_inaddr *addr );
struct ConfItem * find_ipkline (struct irc_inaddr *addr );
struct ConfItem * find_ipgline (struct irc_inaddr *addr );
struct ConfItem * find_generic_line (int type, struct irc_inaddr *addr );
int add_ipline(struct ConfItem *aconf, int type, struct irc_inaddr *addr, int cidr);
void delete_ipline(struct ConfItem *aconf, int type);
void clear_iplines(void);


#endif /* INCLUDE_iplines_h */
