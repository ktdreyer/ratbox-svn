/************************************************************************
 *
 *   IRC - Internet Relay Chat, include/channel.h
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

#ifndef INCLUDED_channel_h
#define INCLUDED_channel_h
#ifndef INCLUDED_config_h
#include "config.h"           /* config settings */
#endif
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"        /* buffer sizes */
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>        /* time_t */
#define INCLUDED_sys_types_h
#endif

struct SLink;
struct Client;


/* mode structure for channels */

struct Mode
{
  unsigned int  mode;
  int   limit;
  char  key[KEYLEN + 1];
};

/* channel structure */

struct Channel
{
  struct Channel* nextch;
  struct Channel* prevch;
  struct Channel* hnextch;
  struct Mode     mode;
  char            topic[TOPICLEN + 1];
  char           *topic_info;
  time_t          topic_time;
  int             users;      /* user count */
  int             opcount;    /* number of chanops */
  /* Only needed for lazy links and hubs */
  unsigned long   lazyLinkChannelExists;
  /* Only needed for lazy links and leafs */
  int             locusers;
  time_t          locusers_last;
  time_t          last_knock;           /* don't allow knock to flood */
  struct Channel* next_vchan;           /* Link list of sub channels */
  struct Channel* prev_vchan;           /* Link list of sub channels */
  struct SLink*   members;
  struct SLink*   invites;
  struct SLink*   banlist;
  struct SLink*   exceptlist;
  struct SLink*   denylist;
  struct SLink*   invexlist;
  int             num_bed;          /* number of bans+exceptions+denies */
  time_t          channelts;
  char            locally_created;  /* used to flag a locally created channel*/
  char            keep_their_modes; /* used only on mode after sjoin */
  time_t          fludblock;
  struct fludbot* fluders;
  char            chname[1];
};

extern  struct  Channel *GlobalChannelList;

void cleanup_channels(void *);

#define CREATE 1        /* whether a channel should be
                           created or just tested for existance */

#define MODEBUFLEN      200

#define NullChn ((struct Channel *)0)

#define ChannelExists(n)        (hash_find_channel(n, NullChn) != NullChn)

/* Maximum mode changes allowed per client, per server is different */
#define MAXMODEPARAMS   4

extern struct SLink    *find_user_link (struct SLink *, struct Client *);
extern struct SLink*   find_channel_link(struct SLink *,
					 struct Channel *chptr); 
extern void    add_user_to_channel(struct Channel *chptr,
				   struct Client *who, int flags);
extern void    remove_user_from_channel(struct Channel *chptr,
					struct Client *who, int flag);

extern int     can_send (struct Channel *chptr, struct Client *who);
extern int     is_banned (struct Channel *chptr, struct Client *who);

extern int     is_chan_op (struct Channel *chptr,struct Client *who);
extern int     user_channel_mode (struct Channel *chptr, struct Client *who);

extern void    send_channel_modes (struct Client *, struct Channel *);
extern int     check_channel_name(const char* name);
extern void    channel_modes(struct Channel *chptr, struct Client *who,
			     char *, char *);
extern void    set_channel_mode(struct Client *, struct Client *, 
                                struct Channel *, int, char **, char *);
extern struct  Channel* get_channel(struct Client *,char*,int );
extern void    clear_bans_exceptions_denies(struct Client *,struct Channel *);

extern void channel_member_names( struct Client *sptr, struct Channel *chptr,
				  char *name_of_channel);
extern char *channel_pub_or_secret(struct Channel *chptr);
extern char *channel_chanop_or_voice(int flags);

extern void add_invite(struct Channel *chptr, struct Client *who);
extern void del_invite(struct Channel *chptr, struct Client *who);

extern int list_continue(struct Client *sptr);
extern void list_one_channel(struct Client *sptr,struct Channel *chptr);

/* this should eliminate a lot of ifdef's in the main code... -orabidoo */

#define BANSTR(l)  ((l)->value.banptr->banstr)


/*
** Channel Related macros follow
*/

/* Channel related flags */

#define CHFL_CHANOP     0x0001 /* Channel operator */
#define CHFL_VOICE      0x0002 /* the power to speak */
#define CHFL_DEOPPED    0x0004 /* deopped by us, modes need to be bounced */
#define CHFL_BAN        0x0008 /* ban channel flag */
#define CHFL_EXCEPTION  0x0010 /* exception to ban channel flag */
#define CHFL_DENY       0x0020 /* regular expression deny flag */
#define CHFL_INVEX	0x0040 /* invite exception */

/* Channel Visibility macros */

#define MODE_CHANOP     CHFL_CHANOP
#define MODE_VOICE      CHFL_VOICE
#define MODE_DEOPPED    CHFL_DEOPPED
#define MODE_PRIVATE    0x0008
#define MODE_SECRET     0x0010
#define MODE_MODERATED  0x0020
#define MODE_TOPICLIMIT 0x0040
#define MODE_INVITEONLY 0x0080
#define MODE_NOPRIVMSGS 0x0100
#define MODE_KEY        0x0200
#define MODE_BAN        0x0400
#define MODE_EXCEPTION  0x0800
#define MODE_DENY       0x1000
#define MODE_INVEX	0x2000

#define MODE_LIMIT      0x4000  /* was 0x2000 */
#define MODE_FLAGS      0x4fff  /* was 0x2fff */

/*
 * mode flags which take another parameter (With PARAmeterS)
 */
#define MODE_WPARAS (MODE_CHANOP|MODE_VOICE|MODE_BAN|\
                     MODE_EXCEPTION|MODE_DENY|MODE_KEY|MODE_LIMIT|MODE_INVEX)

/*
 * Undefined here, these are used in conjunction with the above modes in
 * the source.
#define MODE_QUERY     0x10000000
#define MODE_DEL       0x40000000
#define MODE_ADD       0x80000000
 */

/* used in SetMode() in channel.c and m_umode() in s_msg.c */

#define MODE_NULL      0
#define MODE_QUERY     0x10000000
#define MODE_ADD       0x40000000
#define MODE_DEL       0x20000000

#define HoldChannel(x)          (!(x))
/* name invisible */
#define SecretChannel(x)        ((x) && ((x)->mode.mode & MODE_SECRET))
/* channel not shown but names are */
#define HiddenChannel(x)        ((x) && ((x)->mode.mode & MODE_PRIVATE))
/* channel visible */
#define ShowChannel(v,c)        (PubChannel(c) || IsMember((v),(c)))
#define PubChannel(x)           ((!x) || ((x)->mode.mode &\
                                 (MODE_PRIVATE | MODE_SECRET)) == 0)

#define IsMember(blah,chan) ((blah && blah->user && \
                find_channel_link((blah->user)->channel, chan)) ? 1 : 0)

#define IsChannelName(name) ((name) && (*(name) == '#' || *(name) == '&'))

/*
 * Move BAN_INFO information out of the SLink struct
 * its _only_ used for bans, no use wasting the memory for it
 * in any other type of link. Keep in mind, doing this that
 * it makes it slower as more Malloc's/Free's have to be done, 
 * on the plus side bans are a smaller percentage of SLink usage.
 * Over all, the hybrid coding team came to the conclusion
 * it was worth the effort.
 *
 *  - Dianora
 */
typedef struct Ban      /* also used for exceptions -orabidoo */
{
  char *banstr;
  char *who;
  time_t when;
} aBan;

#endif  /* INCLUDED_channel_h */

