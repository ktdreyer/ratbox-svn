/*
 *  ircd-ratbox: A slightly useful ircd.
 *  config.h: The ircd compile-time-configurable header.
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

#ifndef INCLUDED_config_h
#define INCLUDED_config_h

#include "setup.h"


/*
 * IRCD-HYBRID-7 COMPILE TIME CONFIGURATION OPTIONS
 *
 * Most of the items which used to be configurable in here have
 * been moved into the new improved ircd.conf file.
 * If you can't find something, look in doc/example.conf
 *
 * -davidt
 */

/*
 * File Descriptor limit
 *
 * These limits are ultra-low to guarantee the server will work properly
 * in as many environments as possible.  They can probably be increased
 * significantly, if you know what you are doing.
 *
 * Note that HARD_FDLIMIT_ is modified with the maxclients setting in
 * configure.  You should only touch this if you know what you are doing!!!
 *
 * If you are using select for the IO loop, you may also need to increase
 * the value of FD_SETSIZE by editting the Makefile.  However, using
 * --enable-kqueue, --enable-devpoll, or --enable-poll if at all possible,
 * is highly recommended.
 *
 * VMS should be able to use any suitable value here, other operating
 * systems may require kernel patches, configuration tweaks, or ulimit
 * adjustments in order to exceed certain limits (e.g. 1024, 4096 fds).
 */
#define HARD_FDLIMIT_    MAX_CLIENTS + 60 + 20

/* XXX - MAX_BUFFER is mostly ignored. */
/*
 * Maximum number of connections to allow.
 *
 * MAX_BUFFER  is the number of fds to reserve, e.g. for clients
 *             exempt from limit.
 *
 * 10 file descriptors are reserved for logging, DNS lookups, etc.,
 * so MAX_CLIENTS + MAX_BUFFER + 10 must not exceed HARD_FDLIMIT.
 * NOTE: MAX_CLIENTS is set with configure now
 */
#define MAX_BUFFER      60

#ifndef __vms
/* 
 * Directory paths and filenames for UNIX systems.
 * IRCD_PREFIX is set using ./configure --prefix, see INSTALL.
 * The other defaults should be fine.
 *
 * NOTE: CHANGING THESE WILL NOT ALTER THE DIRECTORY THAT FILES WILL
 *       BE INSTALLED TO.  IF YOU CHANGE THESE, DO NOT USE MAKE INSTALL,
 *       BUT COPY THE FILES MANUALLY TO WHERE YOU WANT THEM.
 *
 * IRCD_PREFIX = prefix for all directories,
 * DPATH       = root directory of installation,
 * BINPATH     = directory for binary files,
 * ETCPATH     = directory for configuration files,
 * LOGPATH     = directory for logfiles,
 * MODPATH     = directory for modules,
 * AUTOMODPATH = directory for autoloaded modules
 */

/* dirs */
#define DPATH   IRCD_PREFIX
#define BINPATH IRCD_PREFIX "/bin/"
#define MODPATH IRCD_PREFIX "/modules/"
#define AUTOMODPATH IRCD_PREFIX "/modules/autoload/"
#define ETCPATH IRCD_PREFIX "/etc"
#define LOGPATH IRCD_PREFIX "/logs"
#define UHPATH   IRCD_PREFIX "/help/users"
#define HPATH  IRCD_PREFIX "/help/opers"

/* files */
#define SPATH    BINPATH "/ircd"	/* ircd executable */
#define SLPATH   BINPATH "/servlink"	/* servlink executable */
#define CPATH    ETCPATH "/ircd.conf"	/* ircd.conf file */
#define KPATH    ETCPATH "/kline.conf"	/* kline file */
#define DLPATH   ETCPATH "/dline.conf"	/* dline file */
#define XPATH	 ETCPATH "/xline.conf"	/* xline file */
#define RESVPATH ETCPATH "/resv.conf"	/* resv file */
#define GPATH    LOGPATH "/gline.log"	/* gline logfile */
#define RPATH    ETCPATH "/ircd.rsa"	/* ircd rsa private keyfile */
#define MPATH    ETCPATH "/ircd.motd"	/* MOTD file */
#define LPATH    LOGPATH "/ircd.log"	/* ircd logfile */
#define PPATH    ETCPATH "/ircd.pid"	/* pid file */
#define OPATH    ETCPATH "/opers.motd"	/* oper MOTD file */
#define LIPATH   ETCPATH "/links.txt"	/* cached links file */

#else /* !__vms */

/* *PATH - directory locations and filenames for VMS.
 *
 * Non VMS systems see below.
 *
 * IRCD_PREFIX = prefix for all directories,
 * DPATH       = root directory of installation,
 * BINPATH     = directory for binary files,
 * ETCPATH     = directory for configuration files,
 * LOGPATH     = directory for logfiles,
 * MODPATH     = directory for autoloaded modules (disabled in VMS),
 */

/* IRCD_PREFIX not needed in VMS -larne */
/*#define IRCD_PREFIX     "IRCD$BASEDIR:"*/
#define DPATH           "IRCD$BASEDIR:"
#define BINPATH         "IRCD$BINDIR:"
#define ETCPATH         "IRCD$CONFDIR:"
#define LOGPATH         "IRCD$LOGDIR:"

#undef  MODPATH
#undef  AUTOMODPATH

#define SPATH    BINPATH "IRCD.EXE"	/* server executable */
#define SLPATH   BINPATH "SERVLINK.EXE"	/* servlink executable */
#define CPATH    ETCPATH "IRCD.CONF"	/* config file */
#define KPATH    ETCPATH "KLINE.CONF"	/* kline file */
#define DLPATH   ETCPATH "DLINE.CONF"	/* dline file */
#define XPATH	 ETCPATH "XLINE.CONF"	/* xline file */
#define RESVPATH ETCPATH "RESV.CONF"	/* resv file */
#define GPATH    LOGPATH "GLINE.LOG"	/* gline logfile */
#define RPATH    ETCPATH "IRCD.RSA"	/* RSA private key file */
#define MPATH    ETCPATH "IRCD.MOTD"	/* MOTD filename */
#define LPATH    LOGPATH "IRCD.LOG"	/* logfile */
#define PPATH    ETCPATH "IRCD.PID"	/* pid file */
#define HPATH    ETCPATH "OPERS.TXT"	/* oper help file */
#define UHPATH   ETCPATH "USERS.TXT"	/* user help file */
#define OPATH    ETCPATH "OPERS.MOTD"	/* oper MOTD file */
#define LIPATH   ETCPATH "LINKS.TXT"	/* cached LINKS file */

#endif /* !__vms */

/* Ignore bogus timestamps from other servers. Yes this will desync
 * the network, but it will allow chanops to resync with a valid non TS 0
 *
 * This should be enabled network wide, or not at all.
 */
#undef  IGNORE_BOGUS_TS

/* HIDE_SERVERS_IPS
 *
 * If this is undefined, opers will be unable to see servers ips and will be
 * shown a masked ip, admins will be shown the real ip.
 *
 * If this is defined, nobody can see a servers ip.
 */
#define  HIDE_SERVERS_IPS

/* HIDE_SPOOF_IPS
 *
 * If this is undefined, opers will be allowed to see the real IP of spoofed
 * users in /trace etc.  If this is defined they will be shown a masked IP.
 */
#define HIDE_SPOOF_IPS

/* TS5_ONLY
 *
 * If this is defined only TS5 servers may link to the network.  See
 * doc/ts5.txt for more information.  If your network has old servers
 * (hyb5, hyb6.0, +CSr) or hybserv you should NOT define this.
 */
#define TS5_ONLY

/* USE_ASCII_CASEMAP
 *
 * Under rfc1459, the characters {}|~ are the lowercase of []\^ so a
 * nick of [foo] is the same as {foo} and a channel #[] is the same as
 * #{}.  If this is defined they will no longer be treated as lowercase
 * of each other, so [foo] and {foo} could be two seperate people.
 *
 * Note: this must be the same network wide or you will have problems.
 *       Your locale(1) must also be set to "C".
 */
#undef USE_ASCII_CASEMAP

/* USE_LOGFILE - log errors and such to LPATH
 * If you wish to have the server send 'vital' messages about server
 * to a logfile, define USE_LOGFILE.
 */
#define USE_LOGFILE

/* USE_SYSLOG - log errors and such to syslog()
 * If you wish to have the server send 'vital' messages about server
 * through syslog, define USE_SYSLOG. Only system errors and events critical
 * to the server are logged although if this is defined with FNAME_USERLOG,
 * syslog() is used instead of the above file. It is not recommended that
 * this option is used unless you tell the system administrator beforehand
 * and obtain their permission to send messages to the system log files.
 */
#undef  USE_SYSLOG

#ifdef  USE_SYSLOG
/* SYSLOG_KILL SYSLOG_SQUIT SYSLOG_CONNECT SYSLOG_USERS SYSLOG_OPER
 * If you use syslog above, you may want to turn some (none) of the
 * spurious log messages for KILL,SQUIT,etc off.
 */
#undef  SYSLOG_KILL		/* log all operator kills to syslog */
#undef  SYSLOG_SQUIT		/* log all remote squits for all servers to syslog */
#undef  SYSLOG_CONNECT		/* log remote connect messages for other all servs */
#undef  SYSLOG_USERS		/* send userlog stuff to syslog */
#undef  SYSLOG_OPER		/* log all users who successfully become an Op */

/* LOG_FACILITY - facility to use for syslog()
 * Define the facility you want to use for syslog().  Ask your
 * sysadmin which one you should use.
 */
#define LOG_FACILITY LOG_LOCAL4

#endif /* USE_SYSLOG */

/* CLIENT_FLOOD - client excess flood threshold(in messages)
 * The number of messages that we can receive before we disconnect the
 * remote client...
 */
#define CLIENT_FLOOD 20

/* NICKNAMEHISTORYLENGTH - size of WHOWAS array
 * this defines the length of the nickname history.  each time a user changes
 * nickname or signs off, their old nickname is added to the top of the list.
 * NOTE: this is directly related to the amount of memory ircd will use whilst
 *       resident and running - it hardly ever gets swapped to disk!  Memory
 *       will be preallocated for the entire whowas array when ircd is started.
 */
#ifndef SMALL_NET
#define NICKNAMEHISTORYLENGTH 15000
#else
#define NICKNAMEHISTORYLENGTH 1500
#endif
/* HANGONGOODLINK and HANGONGOODLINK
 * Often net breaks for a short time and it's useful to try to
 * establishing the same connection again faster than CONNECTFREQUENCY
 * would allow. But, to keep trying on bad connection, we require
 * that connection has been open for certain minimum time
 * (HANGONGOODLINK) and we give the net few seconds to steady
 * (HANGONRETRYDELAY). This latter has to be long enough that the
 * other end of the connection has time to notice it broke too.
 * 1997/09/18 recommended values by ThemBones for modern EFnet
 */

#define HANGONRETRYDELAY 60	/* Recommended value: 30-60 seconds */
#define HANGONGOODLINK 3600	/* Recommended value: 30-60 minutes */

/* KILLCHASETIMELIMIT -
 * Max time from the nickname change that still causes KILL
 * automatically to switch for the current nick of that user. (seconds)
 */
#define KILLCHASETIMELIMIT 90	/* Recommended value: 90 */

/*
 * If the OS has SOMAXCONN use that value, otherwise
 * Use the value in HYBRID_SOMAXCONN for the listen(); backlog
 * try 5 or 25. 5 for AIX and SUNOS, 25 should work better for other OS's
*/
#define HYBRID_SOMAXCONN 25

/* END OF CONFIGURABLE OPTIONS */

/* 
 * Default pre-allocations for various things...
 */
#ifndef SMALL_NET		/* Normal net */
#define CHANNEL_HEAP_SIZE	1024
#define BAN_HEAP_SIZE		1024
#define CLIENT_HEAP_SIZE	1024
#define LCLIENT_HEAP_SIZE	512
#define LINEBUF_HEAP_SIZE	1024
#define	USER_HEAP_SIZE		1024
#define	DNODE_HEAP_SIZE		2048
#define TOPIC_HEAP_SIZE		1024
#define MEMBER_HEAP_SIZE	1024
#else /* Small Net */
#define CHANNEL_HEAP_SIZE	256
#define BAN_HEAP_SIZE		128
#define CLIENT_HEAP_SIZE	256
#define LCLIENT_HEAP_SIZE	128
#define LINEBUF_HEAP_SIZE	128
#define	USER_HEAP_SIZE		128
#define	DNODE_HEAP_SIZE		256
#define TOPIC_HEAP_SIZE		256
#define MEMBER_HEAP_SIZE	256
#endif

#define RXCONF_HEAP_SIZE        20
#define SHARED_HEAP_SIZE        10
#define HELPFILE_HEAP_SIZE	20
#define HELPLINE_HEAP_SIZE	50

/* DEBUGMODE is used mostly for internal development, it is likely
 * to make your client server very sluggish.
 * You usually shouldn't need this. -Dianora
*/
#undef  DEBUGMODE		/* define DEBUGMODE to enable debugging mode. */

/*
 * this checks for various things that should never happen, but
 * might do due to bugs.  ircd might be slightly more efficient with 
 * these disabled, who knows. keep this enabled during development.
 */

#define CONFIG_RATBOX_LEVEL_2

#include "defaults.h"
#endif /* INCLUDED_config_h */
