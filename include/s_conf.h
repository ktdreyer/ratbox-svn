/*
 *  ircd-ratbox: A slightly useful ircd.
 *  s_conf.h: A header for the configuration functions.
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

#ifndef INCLUDED_s_conf_h
#define INCLUDED_s_conf_h
#include "setup.h"



#ifdef HAVE_LIBCRYPTO
#include <openssl/rsa.h>
#endif

#include "ircd_defs.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "patricia.h"

struct Client;
struct DNSReply;
struct hostent;

/* used by new parser */
/* yacc/lex love globals!!! */

struct ip_value
{
	struct sockaddr_storage ip;
	int ip_mask;
	int type;
};

extern FBFILE *conf_fbfile_in;
extern char conf_line_in[256];
extern struct ConfItem *yy_aconf;

struct ConfItem
{
	struct ConfItem *next;	/* list node pointer */
	unsigned int status;	/* If CONF_ILLEGAL, delete when no clients */
	unsigned int flags;
	int clients;		/* Number of *LOCAL* clients using this */
	struct sockaddr_storage my_ipnum;	/* ip to bind to for outgoing connect */
	struct sockaddr_storage ipnum;	/* ip to connect to */
	char *name;		/* IRC name, nick, server name, or original u@h */
	char *host;		/* host part of user@host */
	char *passwd;		/* doubles as kline reason *ugh* */
	char *spasswd;		/* Password to send. */
	char *user;		/* user part of user@host */
	int port;
	time_t hold;		/* Hold action until this time (calendar time) */
	char *className;	/* Name of class */
	struct Class *c_class;	/* Class of connection */
	struct DNSQuery *dns_query;
	int aftype;
	patricia_node_t *pnode;	/* Our patricia node */
#ifdef HAVE_LIBCRYPTO
	char *rsa_public_key_file;
	RSA *rsa_public_key;
#endif
};

#define CONF_ILLEGAL            0x80000000
#define CONF_QUARANTINED_NICK   0x0001
#define CONF_CLIENT             0x0002
#define CONF_SERVER             0x0004
#define CONF_OPERATOR           0x0010
#define CONF_KILL               0x0040

#define CONF_CLASS              0x0400
#define CONF_LEAF               0x0800
#define CONF_LISTEN_PORT        0x1000
#define CONF_HUB                0x2000
#define CONF_EXEMPTKLINE        0x4000
#define CONF_NOLIMIT            0x8000
#define CONF_GLINE             0x10000
#define CONF_DLINE             0x20000
#define CONF_EXEMPTDLINE      0x100000

#define CONF_SERVER_MASK       CONF_SERVER
#define CONF_CLIENT_MASK       (CONF_CLIENT | CONF_OPERATOR | CONF_SERVER_MASK)

#define IsIllegal(x)    ((x)->status & CONF_ILLEGAL)

/* aConfItem->flags */

/* Generic flags... */
/* access flags... */
#define CONF_FLAGS_DO_IDENTD            0x00000001
#define CONF_FLAGS_LIMIT_IP             0x00000002
#define CONF_FLAGS_NO_TILDE             0x00000004
#define CONF_FLAGS_NEED_IDENTD          0x00000008
#define CONF_FLAGS_PASS_IDENTD          0x00000010
#define CONF_FLAGS_NOMATCH_IP           0x00000020
#define CONF_FLAGS_EXEMPTKLINE          0x00000040
#define CONF_FLAGS_NOLIMIT              0x00000080
#define CONF_FLAGS_IDLE_LINED           0x00000100
#define CONF_FLAGS_SPOOF_IP             0x00000200
#define CONF_FLAGS_SPOOF_NOTICE		0x00000400
#define CONF_FLAGS_REDIR                0x00000800
#define CONF_FLAGS_EXEMPTGLINE          0x00001000
#define CONF_FLAGS_RESTRICTED           0x00002000
#define CONF_FLAGS_EXEMPTFLOOD          0x00004000
#define CONF_FLAGS_EXEMPTSPAMBOT	0x00008000
#define CONF_FLAGS_EXEMPTSHIDE		0x00010000
/* server flags */
#define CONF_FLAGS_ALLOW_AUTO_CONN      0x00040000
#define CONF_FLAGS_LAZY_LINK            0x00080000
#define CONF_FLAGS_ENCRYPTED            0x00100000
#define CONF_FLAGS_COMPRESSED           0x00200000
#define CONF_FLAGS_TEMPORARY            0x00400000
#define CONF_FLAGS_TB			0x00800000
#define CONF_FLAGS_VHOSTED		0x01000000


/* Macros for struct ConfItem */
#define IsLimitIp(x)            ((x)->flags & CONF_FLAGS_LIMIT_IP)
#define IsNoTilde(x)            ((x)->flags & CONF_FLAGS_NO_TILDE)
#define IsNeedIdentd(x)         ((x)->flags & CONF_FLAGS_NEED_IDENTD)
#define IsPassIdentd(x)         ((x)->flags & CONF_FLAGS_PASS_IDENTD)
#define IsNoMatchIp(x)          ((x)->flags & CONF_FLAGS_NOMATCH_IP)
#define IsConfExemptKline(x)    ((x)->flags & CONF_FLAGS_EXEMPTKLINE)
#define IsConfExemptLimits(x)   ((x)->flags & CONF_FLAGS_NOLIMIT)
#define IsConfExemptGline(x)    ((x)->flags & CONF_FLAGS_EXEMPTGLINE)
#define IsConfExemptFlood(x)    ((x)->flags & CONF_FLAGS_EXEMPTFLOOD)
#define IsConfExemptSpambot(x)	((x)->flags & CONF_FLAGS_EXEMPTSPAMBOT)
#define IsConfExemptShide(x)	((x)->flags & CONF_FLAGS_EXEMPTSHIDE)
#define IsConfIdlelined(x)      ((x)->flags & CONF_FLAGS_IDLE_LINED)
#define IsConfDoIdentd(x)       ((x)->flags & CONF_FLAGS_DO_IDENTD)
#define IsConfDoSpoofIp(x)      ((x)->flags & CONF_FLAGS_SPOOF_IP)
#define IsConfSpoofNotice(x)    ((x)->flags & CONF_FLAGS_SPOOF_NOTICE)
#define IsConfRestricted(x)     ((x)->flags & CONF_FLAGS_RESTRICTED)
#define IsConfEncrypted(x)      ((x)->flags & CONF_FLAGS_ENCRYPTED)
#define IsConfCompressed(x)     ((x)->flags & CONF_FLAGS_COMPRESSED)
#define IsConfVhosted(x)	((x)->flags & CONF_FLAGS_VHOSTED)
#define IsConfTburst(x)		((x)->flags & CONF_FLAGS_TB)

/* flag definitions for opers now in client.h */

struct config_file_entry
{
	const char *dpath;	/* DPATH if set from command line */
	const char *configfile;
	const char *klinefile;
	const char *dlinefile;
	const char *xlinefile;
	const char *resvfile;

	char *logpath;
	char *operlog;
	char *glinelog;

	char *servlink_path;
	char *egdpool_path;

	char default_operstring[REALLEN];
	char default_adminstring[REALLEN];

	char fname_userlog[MAXPATHLEN];
	char fname_fuserlog[MAXPATHLEN];
	char fname_operlog[MAXPATHLEN];
	char fname_foperlog[MAXPATHLEN];
	char fname_serverlog[MAXPATHLEN];
	char fname_glinelog[MAXPATHLEN];
	char fname_klinelog[MAXPATHLEN];
	char fname_operspylog[MAXPATHLEN];
	char fname_ioerrorlog[MAXPATHLEN];

	unsigned char compression_level;
	int dot_in_ip6_addr;
	int dots_in_ident;
	int failed_oper_notice;
	int anti_nick_flood;
	int anti_spam_exit_message_time;
	int max_accept;
	int max_nick_time;
	int max_nick_changes;
	int ts_max_delta;
	int ts_warn_delta;
	int kline_with_reason;
	int kline_with_connection_closed;
	int kline_delay;
	int warn_no_nline;
	int non_redundant_klines;
	int stats_c_oper_only;
	int stats_y_oper_only;
	int stats_h_oper_only;
	int stats_o_oper_only;
	int stats_k_oper_only;
	int stats_i_oper_only;
	int stats_P_oper_only;
	int map_oper_only;
	int operspy_admin_only;
	int pace_wait;
	int pace_wait_simple;
	int short_motd;
	int no_oper_flood;
	int glines;
	int gline_time;
	int gline_min_cidr;
	int gline_min_cidr6;
	int idletime;
	int hide_server;
	int client_exit;
	int oper_only_umodes;
	int oper_umodes;
	int max_targets;
	int caller_id_wait;
	int min_nonwildcard;
	int min_nonwildcard_simple;
	int default_floodcount;
	int client_flood;
	int use_egd;
	int ping_cookie;
	int tkline_expire_notices;
	int use_whois_actually;
	int disable_auth;
	int connect_timeout;
	int burst_away;
	int htm_messages;
	int htm_interval;
	int htm_trigger;
	int reject_ban_time;
	int reject_after_count;
	int reject_duration;
#ifdef IPV6
	int fallback_to_ip6_int;
#endif
};

struct config_channel_entry
{
	int use_except;
	int use_invex;
	int use_knock;
	int knock_delay;
	int knock_delay_channel;
	int max_bans;
	int max_chans_per_user;
	int no_create_on_split;
	int no_join_on_split;
	int quiet_on_ban;
	int default_split_server_count;
	int default_split_user_count;
	int default_split_delay;
	int no_oper_resvs;
	int burst_topicwho;
};

struct config_server_hide
{
	int flatten_links;
	int links_delay;
	int links_disabled;
	int hidden;
	int disable_hidden;
};

struct server_info
{
	char *name;
	char sid[3];
	char *description;
	char *network_name;
	char *network_desc;
	int hub;
	int use_ts6;
	struct sockaddr_in ip;
#ifdef IPV6
	struct sockaddr_in6 ip6;
#endif
	int specific_ipv4_vhost;
#ifdef IPV6
	int specific_ipv6_vhost;
#endif
};

struct admin_info
{
	char *name;
	char *description;
	char *email;
};

/* bleh. have to become global. */
extern int scount;

/* All variables are GLOBAL */
extern struct ConfItem *ConfigItemList;	/* conf list head */
extern int specific_ipv4_vhost;	/* used in s_bsd.c */
extern int specific_ipv6_vhost;
extern struct config_file_entry ConfigFileEntry;	/* defined in ircd.c */
extern struct config_channel_entry ConfigChannel;	/* defined in channel.c */
extern struct config_server_hide ConfigServerHide;	/* defined in s_conf.c */
extern struct server_info ServerInfo;	/* defined in ircd.c */
extern struct admin_info AdminInfo;	/* defined in ircd.c */
/* End GLOBAL section */

#define TEMP_MIN	1
#define TEMP_HOUR	2
#define TEMP_DAY	3
#define TEMP_WEEK	4

extern dlink_list tkline_min;
extern dlink_list tkline_hour;
extern dlink_list tkline_day;
extern dlink_list tkline_week;

extern dlink_list tdline_min;
extern dlink_list tdline_hour;
extern dlink_list tdline_day;
extern dlink_list tdline_week;

extern struct ConfItem *make_conf(void);
extern void free_conf(struct ConfItem *);

extern void read_conf_files(int cold);

extern int attach_conf(struct Client *, struct ConfItem *);
extern int attach_connect_block(struct Client *client, const char *name, const char *host);
extern int check_client(struct Client *client_p, struct Client *source_p, const char *);

extern int detach_conf(struct Client *);

extern struct ConfItem *find_conf_exact(const char *name, const char *user,
					const char *host, int statmask);
extern struct ConfItem *find_conf_by_name(const char *name, int status);
extern struct ConfItem *find_conf_by_host(const char *host, int status);
extern struct ConfItem *find_kill(struct Client *);
extern int conf_connect_allowed(struct sockaddr_storage *addr, int);
extern char *oper_flags_as_string(int);
extern char *oper_privs_as_string(struct Client *, int);

extern struct ConfItem *find_tkline(const char *, const char *, struct sockaddr_storage *);
extern char *show_iline_prefix(struct Client *, struct ConfItem *, char *);
extern void get_printable_conf(struct ConfItem *,
			       char **, char **, char **, char **, int *, char **);
extern void get_printable_kline(struct Client *, struct ConfItem *,
				char **, char **, char **, char **);
extern void report_configured_links(struct Client *client_p, int mask);

extern void yyerror(const char *);
extern int conf_yy_fatal_error(const char *);
extern int conf_fbgets(char *, int, FBFILE *);

typedef enum
{
	CONF_TYPE,
	KLINE_TYPE,
	DLINE_TYPE,
	RESV_TYPE
}
KlineType;

extern void write_confitem(KlineType, struct Client *, char *, char *,
			   const char *, const char *, const char *, int);
extern void add_temp_kline(struct ConfItem *);
extern void add_temp_dline(struct ConfItem *);
extern void report_temp_klines(struct Client *);
extern void show_temp_klines(struct Client *, dlink_list *);

extern void cleanup_temps_min(void *);
extern void cleanup_temps_hour(void *);
extern void cleanup_temps_day(void *);
extern void cleanup_temps_week(void *);


extern const char *get_conf_name(KlineType);
extern int rehash(int);

extern int conf_add_server(struct ConfItem *, int);
extern void conf_add_class_to_conf(struct ConfItem *);
extern void conf_add_me(struct ConfItem *);
extern void conf_add_class(struct ConfItem *, int);
extern void conf_add_d_conf(struct ConfItem *);
extern void conf_add_conf(struct ConfItem *);
extern void flush_expired_ips(void *);


/* XXX consider moving these into kdparse.h */
extern void parse_k_file(FBFILE * fb);
extern void parse_d_file(FBFILE * fb);
extern void parse_x_file(FBFILE * fb);
extern void parse_resv_file(FBFILE *);
extern char *getfield(char *newline);

extern char *get_oper_name(struct Client *client_p);

extern int yylex(void);

extern unsigned long cidr_to_bitmask[];

extern char conffilebuf[IRCD_BUFSIZE + 1];
extern int lineno;

#define NOT_AUTHORISED  (-1)
#define SOCKET_ERROR    (-2)
#define I_LINE_FULL     (-3)
#define BANNED_CLIENT   (-4)
#define TOO_MANY_LOCAL	(-6)
#define TOO_MANY_GLOBAL (-7)
#define TOO_MANY_IDENT	(-8)

#endif /* INCLUDED_s_conf_h */
