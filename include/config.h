#ifndef INCLUDED_config_h
#define INCLUDED_config_h

/* Paths to various things.
 * IMPORTANT: if you alter the directories these files go to,
 *            you must create those paths yourself.
 */

/* CONF_PATH
 * Path to config file
 */
#define CONF_PATH	PREFIX "/rserv.conf"

/* PID_PATH
 * Path to pid file
 */
#define PID_PATH	PREFIX "/rserv.pid"

/* LOG_PATH
 * Path to logfile
 */
#define LOG_PATH	PREFIX "/zlogfile"


/* CRYPT_PASSWORDS
 * Controls whether passwords in O: in conf file are encrypted
 */
#define CRYPT_PASSWORDS
 
/* RECONNECT_DELAY
 * The duration (in seconds) between reconnections to servers
 */
#define RECONNECT_DELAY 30

/* PING_TIME
 * The duration (in seconds) a server can be idle before being pinged.
 * An idle duration of double this causes a server to be exited.
 */
#define PING_TIME       300

/* SMALL_NETWORK
 * If your network is fairly small, enable this to save some memory.
 */
#define SMALL_NETWORK



/*              ---------------------------             */
/*              END OF CONFIGURABLE OPTIONS             */
/*              ---------------------------             */




#define MAX_FD		1000

#define RSERV_VERSION		"1.0beta"

#ifdef SMALL_NETWORK
#define HEAP_CHANNEL    64
#define HEAP_CHMEMBER   128
#define HEAP_CLIENT     128
#define HEAP_USER       128
#define HEAP_SERVER     16
#else
#define HEAP_CHANNEL    1024
#define HEAP_CHMEMBER   1024
#define HEAP_CLIENT     1024
#define HEAP_USER       1024
#define HEAP_SERVER     32
#endif

#endif
/* $Id$ */
