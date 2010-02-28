/* $Id$ */
#ifndef INCLUDED_hook_h
#define INCLUDED_hook_h

#define HOOK_DCC_AUTH			0	/* dcc client auths */
#define HOOK_DCC_EXIT			2	/* dcc client exits */

#define HOOK_CHANNEL_JOIN		4	/* someone joining a channel */
#define HOOK_CHANNEL_SJOIN_LOWERTS	6	/* channel SJOIN at lower TS */
#define HOOK_CHANNEL_MODE_OP		8	/* +o on a channel */
#define HOOK_CHANNEL_MODE_VOICE		10	/* +v on a channel */
#define HOOK_CHANNEL_MODE_BAN		12	/* +b on a channel */
#define HOOK_CHANNEL_MODE_SIMPLE	14	/* +ntsimplkS */
#define HOOK_CHANNEL_DESTROY		16	/* about to destroy a chptr */
#define HOOK_CHANNEL_TOPIC		17	/* TOPIC/TB on a channel */

#define HOOK_EOB_UPLINK			18	/* uplink finished bursting */
#define HOOK_EOB_SERVER			20	/* a server finished bursting
						 * called for uplink too after HOOK_EOB_UPLINK
						 */

#define HOOK_CLIENT_CONNECT		22	/* client connects (not burst) */
#define HOOK_CLIENT_CONNECT_BURST	24	/* client connects (burst) */
#define HOOK_CLIENT_NICKCHANGE		26	/* client changes nick */
#define HOOK_CLIENT_EXIT		28	/* client quits */
/* HOOK_CLIENT_EXIT_SPLIT is called prior to HOOK_CLIENT_EXIT, but both are
 * called for a client that splits
 */
#define HOOK_CLIENT_EXIT_SPLIT		29	/* client quits due to a split */

#define HOOK_SERVER_EXIT		30	/* server squits */

#define HOOK_USERSERV_LOGIN		32	/* user logs into userserv */
#define HOOK_USERSERV_LOGIN_BURST	34	/* user logs into userserv (burst) */

#define HOOK_PROTO_SQUIT_UNKNOWN	36	/* squit an unknown server */

#define HOOK_DBSYNC			38	/* services about to terminate */
#define HOOK_LAST_HOOK			40

typedef int (*hook_func)(void *, void *);

extern void hook_add(hook_func func, int hook);
extern int hook_call(int hook, void *arg, void *arg2);

#endif
