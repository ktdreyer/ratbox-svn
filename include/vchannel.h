/************************************************************************
 *
 *   IRC - Internet Relay Chat, include/vchannel.h
 *   Copyright (C) 1990 Jarkko Oikarinen
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
 */

#ifndef INCLUDED_vchannel_h
#define INCLUDED_vchannel_h

#ifndef INCLUDED_channel_h
#include "channel.h"
#endif

#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"        /* buffer sizes */
#endif

#ifndef INCLUDED_sys_types_h
#include <sys/types.h>        /* time_t */
#define INCLUDED_sys_types_h
#endif

struct Client;
struct Channel;

extern void    add_vchan_to_client_cache(struct Client *sptr,
					 struct Channel *vchan,
					 struct Channel *base_chan);

#endif  /* INCLUDED_vchannel_h */

