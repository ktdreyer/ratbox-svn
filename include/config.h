#ifndef INCLUDED_config_h
#define INCLUDED_config_h

/* Paths to various things.
 * IMPORTANT: if you alter the directories these files go to,
 *            you must create those paths yourself.
 */

#define CONF_PATH	PREFIX "/rserv.conf"
#define PID_PATH	PREFIX "/rserv.pid"
#define LOG_PATH	PREFIX "/zlogfile"
#define HELP_PATH       PREFIX "/help/"

/* SMALL_NETWORK
 * If your network is fairly small, enable this to save some memory.
 */
#define SMALL_NETWORK

/* ALIS_SERVICE
 * Controls whether the ALIS (Advanced List Service) is enabled
 */
#define ALIS_SERVICE

/* HOSTSTAT_SERVICE
 * Controls whether the Host Statistics service is enabled
 */
#define HOSTSTAT_SERVICE

/* OPERBOT_SERVICE
 * Controls whether the oper bot service is enabled
 */
#define OPERBOT_SERVICE

/* USER_SERVICE
 * Enables/disables the service for registration/login of usernames.
 * This service is required for CHANNEL_SERVICE
 */
#define USER_SERVICE

/* CHANNEL_SERVICE
 * Enables/disables the channel service.
 */
#define CHANNEL_SERVICE

/* JUPE_SERVICE
 * Enables/disables the jupe service.
 */
#define JUPE_SERVICE

/* EXTENDED_HOSTHASH
 * Extends the host hash to keep entries around so they can be seen even if
 * a host has no current clients, and track maximums.
 * Only useful if HOSTSTAT_SERVICE is enabled.
 */
#define EXTENDED_HOSTHASH

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
#define HEAP_DLINKNODE	128
#else
#define HEAP_CHANNEL    1024
#define HEAP_CHMEMBER   1024
#define HEAP_CLIENT     1024
#define HEAP_USER       1024
#define HEAP_SERVER     32
#define HEAP_DLINKNODE	1024
#endif

#define HEAP_CACHEFILE  16
#define HEAP_CACHELINE  128
#define HEAP_USER_REG	256
#define HEAP_CHAN_REG	128
#define HEAP_MEMBER_REG	256

#ifndef HOSTSTAT_SERVICE
#undef EXTENDED_HOSTHASH
#endif

#if defined(CHANNEL_SERVICE) && !defined(USER_SERVICE)
# error CHANNEL_SERVICE requires USER_SERVICE
#endif

#endif
/* $Id$ */
