/*
 *  ircd-ratbox: A slightly useful ircd.
 *  ircd_defs.h: A header for ircd global definitions.
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

/*
 * NOTE: NICKLEN and TOPICLEN do not live here anymore. Set it with configure
 * Otherwise there are no user servicable part here. 
 *
 */
 /* ircd_defs.h - Global size definitions for record entries used
  * througout ircd. Please think 3 times before adding anything to this
  * file.
  */
#ifndef INCLUDED_ircd_defs_h
#define INCLUDED_ircd_defs_h

#include "config.h"

#if !defined(CONFIG_RATBOX_LEVEL_1)
#  error Incorrect config.h for this revision of ircd.
#endif

#define HOSTLEN         63	/* Length of hostname.  Updated to         */
				/* comply with RFC1123                     */

#define USERLEN         10
#define REALLEN         50
#define KILLLEN         90
#define CHANNELLEN      200

/* 23+1 for \0 */
#define KEYLEN          24
#define BUFSIZE         512	/* WARNING: *DONT* CHANGE THIS!!!! */
#define MAXRECIPIENTS   20
#define MAXBANLENGTH    1024
#define OPERNICKLEN     NICKLEN*2	/* Length of OPERNICKs. */

#define USERHOST_REPLYLEN       (NICKLEN+HOSTLEN+USERLEN+5)
#define MAX_DATE_STRING 32	/* maximum string length for a date string */

#define HELPLEN         400

/* 
 * message return values 
 */
#define CLIENT_EXITED    -2
#define CLIENT_PARSE_ERROR -1
#define CLIENT_OK	1


#ifdef IPV6

#ifndef AF_INET6
#error "AF_INET6 not defined"
#endif


#define DEF_FAM AF_INET6


#else


#ifndef AF_INET6
#define AF_INET6 10		/* Dummy AF_INET6 declaration */
#endif
#define DEF_FAM AF_INET


#endif




#endif /* INCLUDED_ircd_defs_h */
