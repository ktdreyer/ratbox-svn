/*
 *  ircd-ratbox: A slightly useful ircd.
 *  client.h: The ircd client header.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
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

#ifndef INCLUDED_client_h
#define INCLUDED_client_h

#if !defined(CONFIG_RATBOX_LEVEL_2)
#error Incorrect config.h for this revision of ircd.
#endif

#define PASSWDLEN       128

/*
 * pre declare structs
 */
struct ConfItem;
struct Whowas;
struct DNSReply;
struct Listener;
struct Client;
struct LocalUser;

struct exit_client_hook
{
	struct Client *client_p;
	char exit_message[TOPICLEN];
};


/*
 * status macros.
 */
#define STAT_CONNECTING         0x01
#define STAT_HANDSHAKE          0x02
#define STAT_ME                 0x04
#define STAT_UNKNOWN            0x08
#define STAT_REJECT		0x10
#define STAT_SERVER             0x20
#define STAT_CLIENT             0x40


#define IsRegisteredUser(x)     ((x)->status == STAT_CLIENT)
#define IsRegistered(x)         (((x)->status  > STAT_UNKNOWN) && ((x)->status != STAT_REJECT))
#define IsConnecting(x)         ((x)->status == STAT_CONNECTING)
#define IsHandshake(x)          ((x)->status == STAT_HANDSHAKE)
#define IsMe(x)                 ((x)->status == STAT_ME)
#define IsUnknown(x)            ((x)->status == STAT_UNKNOWN)
#define IsServer(x)             ((x)->status == STAT_SERVER)
#define IsClient(x)             ((x)->status == STAT_CLIENT)
#define IsReject(x)		((x)->status == STAT_REJECT)

#define IsAnyServer(x)          (IsServer(x) || IsHandshake(x) || IsConnecting(x))

#define IsOper(x)		((x)->umodes & UMODE_OPER)
#define IsAdmin(x)		((x)->umodes & UMODE_ADMIN)

#define SetReject(x)		{(x)->status = STAT_REJECT; \
				 (x)->handler = UNREGISTERED_HANDLER; }

#define SetConnecting(x)        {(x)->status = STAT_CONNECTING; \
				 (x)->handler = UNREGISTERED_HANDLER; }

#define SetHandshake(x)         {(x)->status = STAT_HANDSHAKE; \
				 (x)->handler = UNREGISTERED_HANDLER; }

#define SetMe(x)                {(x)->status = STAT_ME; \
				 (x)->handler = UNREGISTERED_HANDLER; }

#define SetUnknown(x)           {(x)->status = STAT_UNKNOWN; \
				 (x)->handler = UNREGISTERED_HANDLER; }

#define SetServer(x)            {(x)->status = STAT_SERVER; \
				 (x)->handler = SERVER_HANDLER; }

#define SetClient(x)            {(x)->status = STAT_CLIENT; \
				 (x)->handler = IsOper((x)) ? \
					OPER_HANDLER : CLIENT_HANDLER; }
#define SetRemoteClient(x)	{(x)->status = STAT_CLIENT; \
				 (x)->handler = RCLIENT_HANDLER; }

#define STAT_CLIENT_PARSE (STAT_UNKNOWN | STAT_CLIENT)
#define STAT_SERVER_PARSE (STAT_CONNECTING | STAT_HANDSHAKE | STAT_SERVER)

#define PARSE_AS_CLIENT(x)      ((x)->status & STAT_CLIENT_PARSE)
#define PARSE_AS_SERVER(x)      ((x)->status & STAT_SERVER_PARSE)


/*
 * ts stuff
 */
#define TS_CURRENT	6

#ifdef TS6_ONLY
#define TS_MIN          6
#else
#define TS_MIN          3
#endif

#define TS_DOESTS       0x10000000
#define DoesTS(x)       ((x)->tsinfo & TS_DOESTS)

#define has_id(source)	((source)->id[0] != '\0')
#define use_id(source)	((source)->id[0] != '\0' ? (source)->id : (source)->name)

/* if target is TS6, use id if it has one, else name */
#define get_id(source, target) ((IsServer(target->from) && has_id(target->from)) ? \
				use_id(source) : (source)->name)

/* housekeeping flags */

#define FLAGS_PINGSENT     0x0001	/* Unreplied ping sent */
#define FLAGS_DEAD	   0x0002	/* Local socket is dead--Exiting soon */
#define FLAGS_KILLED       0x0004	/* Prevents "QUIT" from being sent for this */
#define FLAGS_CLOSING      0x0020	/* set when closing to suppress errors */
#define FLAGS_GOTID        0x0080	/* successful ident lookup achieved */
#define FLAGS_NEEDID       0x0100	/* I-lines say must use ident return */
#define FLAGS_NORMALEX     0x0400	/* Client exited normally */
#define FLAGS_SENDQEX      0x0800	/* Sendq exceeded */
#define FLAGS_SERVLINK     0x10000	/* servlink has servlink process */
#define FLAGS_MARK	   0x20000	/* marked client */
#define FLAGS_HIDDEN       0x40000	/* hidden server */
#define FLAGS_EOB          0x80000	/* EOB */
#define FLAGS_MYCONNECT	   0x100000	/* MyConnect */
#define FLAGS_IOERROR      0x200000	/* IO error */
#define FLAGS_SERVICE	   0x400000
#define FLAGS_TGCHANGE     0x800000	/* we're allowed to clear something */
/* umodes, settable flags */

#define UMODE_SERVNOTICE   0x0001	/* server notices such as kill */
#define UMODE_CCONN        0x0002	/* Client Connections */
#define UMODE_REJ          0x0004	/* Bot Rejections */
#define UMODE_SKILL        0x0008	/* Server Killed */
#define UMODE_FULL         0x0010	/* Full messages */
#define UMODE_SPY          0x0020	/* see STATS / LINKS */
#define UMODE_DEBUG        0x0040	/* 'debugging' info */
#define UMODE_NCHANGE      0x0080	/* Nick change notice */
#define UMODE_WALLOP       0x0100	/* send wallops to them */
#define UMODE_OPERWALL     0x0200	/* Operwalls */
#define UMODE_INVISIBLE    0x0400	/* makes user invisible */
#define UMODE_BOTS         0x0800	/* shows bots */
#define UMODE_EXTERNAL     0x1000	/* show servers introduced and splitting */
#define UMODE_CALLERID     0x2000	/* block unless caller id's */
#define UMODE_UNAUTH       0x4000	/* show unauth connects here */
#define UMODE_LOCOPS       0x8000	/* show locops */
#define UMODE_OPERSPY	   0x10000
#define UMODE_CCONNEXT     0x20000	/* extended client connections */
#define UMODE_SERVICE      0x40000
#define UMODE_DEAF	   0x80000

/* user information flags, only settable by remote mode or local oper */
#define UMODE_OPER         0x100000	/* Operator */
#define UMODE_ADMIN        0x200000	/* Admin on server */

#define UMODE_ALL	   UMODE_SERVNOTICE

/* overflow flags */
/* EARLIER FLAGS ARE IN s_newconf.h */
#define FLAGS2_EXEMPTGLINE      0x0100000
#define FLAGS2_EXEMPTKLINE      0x0200000
#define FLAGS2_EXEMPTFLOOD      0x0400000
#define FLAGS2_NOLIMIT          0x0800000
#define FLAGS2_IDLE_LINED       0x1000000
#define FLAGS2_CLICAP		0x2000000
#define FLAGS2_PING_COOKIE      0x4000000
#define FLAGS2_IP_SPOOFING      0x8000000
#define FLAGS2_FLOODDONE        0x10000000
#define FLAGS2_EXEMPTSPAMBOT	0x20000000
#define FLAGS2_EXEMPTSHIDE	0x40000000
#define FLAGS2_EXEMPTJUPE	0x80000000

#define SEND_UMODES  (UMODE_INVISIBLE | UMODE_OPER | UMODE_WALLOP | \
                      UMODE_ADMIN | UMODE_SERVICE | UMODE_DEAF)
#define DEFAULT_OPER_UMODES (UMODE_SERVNOTICE | UMODE_OPERWALL | \
                             UMODE_WALLOP | UMODE_LOCOPS)
#define ALL_UMODES   (SEND_UMODES | UMODE_SERVNOTICE | UMODE_CCONN | \
                      UMODE_REJ | UMODE_SKILL | UMODE_FULL | UMODE_SPY | \
                      UMODE_NCHANGE | UMODE_OPERWALL | UMODE_DEBUG | \
                      UMODE_BOTS | UMODE_EXTERNAL | UMODE_LOCOPS | \
 		      UMODE_ADMIN | UMODE_UNAUTH | UMODE_CALLERID | \
		      UMODE_OPERSPY | UMODE_CCONNEXT | UMODE_SERVICE | \
		      UMODE_DEAF)

#define CLICAP_MULTI_PREFIX	0x0001

/*
 * flags macros.
 */
#define IsPerson(x)             (IsClient(x) && (x)->user)
#define HasServlink(x)          ((x)->flags &  FLAGS_SERVLINK)
#define SetServlink(x)          ((x)->flags |= FLAGS_SERVLINK)
#define MyConnect(x)		((x)->flags & FLAGS_MYCONNECT)
#define SetMyConnect(x)		((x)->flags |= FLAGS_MYCONNECT)
#define ClearMyConnect(x)	((x)->flags &= ~FLAGS_MYCONNECT)

#define MyClient(x)             (MyConnect(x) && IsClient(x))
#define SetMark(x)		((x)->flags |= FLAGS_MARK)
#define ClearMark(x)		((x)->flags &= ~FLAGS_MARK)
#define IsMarked(x)		((x)->flags & FLAGS_MARK)
#define SetHidden(x)		((x)->flags |= FLAGS_HIDDEN)
#define ClearHidden(x)		((x)->flags &= ~FLAGS_HIDDEN)
#define IsHidden(x)		((x)->flags & FLAGS_HIDDEN)
#define ClearEob(x)		((x)->flags &= ~FLAGS_EOB)
#define SetEob(x)		((x)->flags |= FLAGS_EOB)
#define HasSentEob(x)		((x)->flags & FLAGS_EOB)
#define IsDead(x)          	((x)->flags &  FLAGS_DEAD)
#define SetDead(x)         	((x)->flags |= FLAGS_DEAD)
#define IsClosing(x)		((x)->flags & FLAGS_CLOSING)
#define SetClosing(x)		((x)->flags |= FLAGS_CLOSING)
#define IsIOError(x)		((x)->flags & FLAGS_IOERROR)
#define SetIOError(x)		((x)->flags |= FLAGS_IOERROR)
#define IsAnyDead(x)		(IsIOError(x) || IsDead(x) || IsClosing(x))
#define IsTGChange(x)		((x)->flags & FLAGS_TGCHANGE)
#define SetTGChange(x)		((x)->flags |= FLAGS_TGCHANGE)
#define ClearTGChange(x)	((x)->flags &= ~FLAGS_TGCHANGE)

/* oper flags */
#define MyOper(x)               (MyConnect(x) && IsOper(x))

#define SetOper(x)              {(x)->umodes |= UMODE_OPER; \
				 if (MyClient((x))) (x)->handler = OPER_HANDLER;}

#define ClearOper(x)            {(x)->umodes &= ~(UMODE_OPER|UMODE_ADMIN); \
				 if (MyClient((x)) && !IsOper((x)) && !IsServer((x))) \
				  (x)->handler = CLIENT_HANDLER; }

#define IsPrivileged(x)         (IsOper(x) || IsServer(x))

/* umode flags */
#define IsInvisible(x)          ((x)->umodes & UMODE_INVISIBLE)
#define SetInvisible(x)         ((x)->umodes |= UMODE_INVISIBLE)
#define ClearInvisible(x)       ((x)->umodes &= ~UMODE_INVISIBLE)
#define SendWallops(x)          ((x)->umodes & UMODE_WALLOP)
#define ClearWallops(x)         ((x)->umodes &= ~UMODE_WALLOP)
#define SendLocops(x)           ((x)->umodes & UMODE_LOCOPS)
#define SendServNotice(x)       ((x)->umodes & UMODE_SERVNOTICE)
#define SendOperwall(x)         ((x)->umodes & UMODE_OPERWALL)
#define SendCConnNotice(x)      ((x)->umodes & UMODE_CCONN)
#define SendRejNotice(x)        ((x)->umodes & UMODE_REJ)
#define SendSkillNotice(x)      ((x)->umodes & UMODE_SKILL)
#define SendFullNotice(x)       ((x)->umodes & UMODE_FULL)
#define SendSpyNotice(x)        ((x)->umodes & UMODE_SPY)
#define SendDebugNotice(x)      ((x)->umodes & UMODE_DEBUG)
#define SendNickChange(x)       ((x)->umodes & UMODE_NCHANGE)
#define SetWallops(x)           ((x)->umodes |= UMODE_WALLOP)
#define SetCallerId(x)		((x)->umodes |= UMODE_CALLERID)
#define IsSetCallerId(x)	((x)->umodes & UMODE_CALLERID)
#define IsService(x)		((x)->umodes & UMODE_SERVICE)
#define IsDeaf(x)		((x)->umodes & UMODE_DEAF)

#define SetNeedId(x)            ((x)->flags |= FLAGS_NEEDID)
#define IsNeedId(x)             (((x)->flags & FLAGS_NEEDID) != 0)

#define SetGotId(x)             ((x)->flags |= FLAGS_GOTID)
#define IsGotId(x)              (((x)->flags & FLAGS_GOTID) != 0)

/*
 * flags2 macros.
 */
#define IsExemptKline(x)        ((x)->flags2 & FLAGS2_EXEMPTKLINE)
#define SetExemptKline(x)       ((x)->flags2 |= FLAGS2_EXEMPTKLINE)
#define IsExemptLimits(x)       ((x)->flags2 & FLAGS2_NOLIMIT)
#define SetExemptLimits(x)      ((x)->flags2 |= FLAGS2_NOLIMIT)
#define IsExemptGline(x)        ((x)->flags2 & FLAGS2_EXEMPTGLINE)
#define SetExemptGline(x)       ((x)->flags2 |= FLAGS2_EXEMPTGLINE)
#define IsExemptFlood(x)        ((x)->flags2 & FLAGS2_EXEMPTFLOOD)
#define SetExemptFlood(x)       ((x)->flags2 |= FLAGS2_EXEMPTFLOOD)
#define IsExemptSpambot(x)	((x)->flags2 & FLAGS2_EXEMPTSPAMBOT)
#define SetExemptSpambot(x)	((x)->flags2 |= FLAGS2_EXEMPTSPAMBOT)
#define IsExemptShide(x)	((x)->flags2 & FLAGS2_EXEMPTSHIDE)
#define SetExemptShide(x)	((x)->flags2 |= FLAGS2_EXEMPTSHIDE)
#define IsExemptJupe(x)		((x)->flags2 & FLAGS2_EXEMPTJUPE)
#define SetExemptJupe(x)	((x)->flags2 |= FLAGS2_EXEMPTJUPE)
#define IsIPSpoof(x)            ((x)->flags2 & FLAGS2_IP_SPOOFING)
#define SetIPSpoof(x)           ((x)->flags2 |= FLAGS2_IP_SPOOFING)

#define SetIdlelined(x)         ((x)->flags2 |= FLAGS2_IDLE_LINED)
#define IsIdlelined(x)          ((x)->flags2 & FLAGS2_IDLE_LINED)

#define IsFloodDone(x)          ((x)->flags2 & FLAGS2_FLOODDONE)
#define SetFloodDone(x)         ((x)->flags2 |= FLAGS2_FLOODDONE)

/*
 * definitions for get_client_name
 */
#define HIDE_IP 0
#define SHOW_IP 1
#define MASK_IP 2

#define NOTIFY_BANNED_DLINE	0
#define NOTIFY_BANNED_GLINE	1
#define NOTIFY_BANNED_KLINE	2

extern void check_banned_lines(void);
extern void check_klines_event(void *unused);
extern void check_klines(void);
void notify_banned_client(struct Client *client_p, struct ConfItem *aconf, int ban);

extern const char *get_client_name(struct Client *client, int show_ip);
extern const char *get_server_name(struct Client *client, int show_ip);
extern const char *log_client_name(struct Client *, int);
extern void init_client(void);
extern struct Client *make_client(struct Client *from);
extern void free_client(struct Client *client);

extern int exit_client(struct Client *, struct Client *, struct Client *, const char *);

extern void error_exit_client(struct Client *, int);



extern void count_local_client_memory(size_t * count, size_t * memory);
extern void count_remote_client_memory(size_t * count, size_t * memory);

extern struct Client *find_chasing(struct Client *, const char *, int *);
extern struct Client *find_person(const char *);
extern struct Client *find_named_person(const char *);
extern struct Client *next_client(struct Client *, const char *);
extern int accept_message(struct Client *source, struct Client *target);
extern void del_from_accept(struct Client *source, struct Client *target);
extern void del_all_accepts(struct Client *client_p);

extern void dead_link(struct Client *client_p);
extern int show_ip(struct Client *source_p, struct Client *target_p);

extern void initUser(void);
extern void free_user(struct User *, struct Client *);
extern struct User *make_user(struct Client *);
extern struct Server *make_server(struct Client *);
extern void close_connection(struct Client *);
extern void init_uid(void);
extern char *generate_uid(void);

extern void flood_endgrace(struct Client *);

#endif /* INCLUDED_client_h */
