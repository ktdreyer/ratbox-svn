/************************************************************************
 *   IRC - Internet Relay Chat, include/defaults.h
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

#ifndef INCLUDED_defaults_h
#define INCLUDED_defaults_h

/* this file is included (only) at the end of config.h, to supply default
 * values for things which are now configurable at runtime.
 */

/*
 * First, set other fd limits based on values from user
 */
#ifndef HARD_FDLIMIT_
error HARD_FDLIMIT_ undefined
#endif

#define HARD_FDLIMIT    (HARD_FDLIMIT_ - 10)
#define MAXCONNECTIONS  HARD_FDLIMIT
#define MASTER_MAX      (HARD_FDLIMIT - MAX_BUFFER)

/* class {} default values */
#define DEFAULT_SENDQ 9000000           /* default max SendQ */
#define PORTNUM 6667                    /* default outgoing portnum */
#define DEFAULT_PINGFREQUENCY    120    /* Default ping frequency */
#define DEFAULT_CONNECTFREQUENCY 600    /* Default connect frequency */

#define TS_MAX_DELTA_MIN      10        /* min value for ts_max_delta */
#define TS_MAX_DELTA_DEFAULT  600       /* default for ts_max_delta */
#define TS_WARN_DELTA_MIN     10        /* min value for ts_warn_delta */
#define TS_WARN_DELTA_DEFAULT 30        /* default for ts_warn_delta */

/* ServerInfo default values */
#define NETWORK_NAME_DEFAULT "EFnet"             /* default for network_name */
#define NETWORK_DESC_DEFAULT "Eris Free Network" /* default for network_desc */

/* General defaults */
#define MAXIMUM_LINKS_DEFAULT 1         /* default for maximum_links */

#define CLIENT_FLOOD_DEFAULT 20         /* default for client_flood */
#define CLIENT_FLOOD_MAX     2000
#define CLIENT_FLOOD_MIN     10

#define LINKS_DELAY_DEFAULT  300

#define MAX_TARGETS_DEFAULT 4           /* default for max_targets */

#define INIT_LOG_LEVEL L_NOTICE         /* default for log_level */

#define MIN_JOIN_LEAVE_TIME  60
#define MAX_JOIN_LEAVE_COUNT  25
#define OPER_SPAM_COUNTDOWN   5 
#define JOIN_LEAVE_COUNT_EXPIRE_TIME 120

#if defined(DEBUGMODE) || defined(DNS_DEBUG)
#  define Debug(x) debug x
#  define LOGFILE LPATH
#else
#  define Debug(x) ;
#  define LOGFILE "/dev/null"
#endif

#define MIN_SPAM_NUM 5
#define MIN_SPAM_TIME 60

#if defined( WANT_GETTEXT ) && defined( HAVE_GETTEXT ) && defined( MSGPATH )
#define USE_GETTEXT 1
#define _(a)       (gettext(a))
#else
#undef USE_GETTEXT
#define _(a)       (a)
#endif

#define CONFIG_H_LEVEL_7

#endif /* INCLUDED_defaults_h */
