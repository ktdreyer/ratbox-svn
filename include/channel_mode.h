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

#include <sys/types.h>        /* time_t */
#include <sys/time.h>

#ifndef INCLUDED_channel_mode_h
#define INCLUDED_channel_mode_h
#ifndef INCLUDED_config_h
#include "config.h"           /* config settings */
#endif
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"        /* buffer sizes */
#endif

/* If the below define is enabled, we will bounce halfops as follows:
 * If we receive a halfop for a user, we will check their ->from supports
 * CAP_HOPS, if not, we will bounce the mode back as a -h (which will
 * be translated to -o for any back down the line not supporting CAP_HOPS).
 * If it does, we'll send it as +h to that server, and any others supporting
 * CAP_HOPS.  All other servers will see a +o.
 *
 * The first server to find (user_being_halfoped)->from is not capable
 * of CAP_HOPS will also send a notice to the user performing the mode
 * (if it is indeed a user) informing them why.
 */
#undef BOUNCE_BAD_HOPS

#define MODEBUFLEN      200

/* Maximum mode changes allowed per client, per server is different */
#define MAXMODEPARAMS   4

extern void    set_channel_mode(struct Client *, struct Client *, 
                                struct Channel *, int, char **, char *);

extern void sync_channel_oplists(struct Channel *, int);
extern void sync_oplists(struct Channel *, struct Client *, int,
                         const char *);
extern void set_channel_mode_flags( char flags_ptr[4][2],
				    struct Channel *chptr,
				    struct Client *source_p);
extern void init_chcap_usage_counts(void);
extern void set_chcap_usage_counts(struct Client *serv_p);
extern void unset_chcap_usage_counts(struct Client *serv_p);

/*
** Channel Related macros follow
*/

/* can_send results */
#define CAN_SEND_NO	0
#define CAN_SEND_NONOP  1
#define CAN_SEND_OPV	2


/* Channel related flags */

#define CHFL_PEON	0x0000 /* normal member of channel */
#define CHFL_CHANOP     0x0001 /* Channel operator */
#define CHFL_VOICE      0x0002 /* the power to speak */
#define CHFL_DEOPPED    0x0004 /* deopped by us, modes need to be bounced */
#define CHFL_HALFOP     0x0008 /* Channel half op */
#define CHFL_BAN        0x0010 /* ban channel flag */
#define CHFL_EXCEPTION  0x0020 /* exception to ban channel flag */
#define CHFL_INVEX      0x0080

/* Channel Visibility macros */

#define MODE_PEON	CHFL_PEON
#define MODE_CHANOP     CHFL_CHANOP
#define MODE_VOICE      CHFL_VOICE
#define MODE_HALFOP	CHFL_HALFOP
#define MODE_DEOPPED	CHFL_DEOPPED

/* channel modes ONLY */
#define MODE_PRIVATE    0x0008
#define MODE_SECRET     0x0010
#define MODE_MODERATED  0x0020
#define MODE_TOPICLIMIT 0x0040
#define MODE_INVITEONLY 0x0080
#define MODE_NOPRIVMSGS 0x0100
#define MODE_BAN        0x0400
#define MODE_EXCEPTION  0x0800
#define MODE_INVEX	0x2000
#define MODE_HIDEOPS    0x4000

/*
 * mode flags which take another parameter (With PARAmeterS)
 */
#define MODE_WPARAS (MODE_CHANOP|MODE_VOICE|MODE_BAN|\
                     MODE_EXCEPTION|MODE_INVEX)

#define MODE_QUERY     0
#define MODE_ADD       1
#define MODE_DEL       -1

/* name invisible */
#define SecretChannel(x)        ((x) && ((x)->mode.mode & MODE_SECRET))
/* channel not shown but names are */
#define HiddenChannel(x)        ((x) && ((x)->mode.mode & MODE_PRIVATE))
#define PubChannel(x)           ((!x) || ((x)->mode.mode &\
                                 (MODE_PRIVATE | MODE_SECRET)) == 0)

struct ChModeChange
{
 char letter;
 char *arg, *id;
 int caps, nocaps, mems;
};

struct ChModeBounce
{
  char letter;
  char *arg, *id;
  int dir;
};

struct ChResyncOp
{
 struct Client *client_p;
 int whole_chan, dir, sync, send;
 char c;
};

struct ChCapCombo
{
  int count;
  int cap_yes;
  int cap_no;
};

#define CHACCESS_CHANOP 3
#define CHACCESS_HALFOP 2
#define CHACCESS_VOICED 1
#define CHACCESS_PEON   0

#endif  /* INCLUDED_channel_mode_h */
