/************************************************************************
 *   IRC - Internet Relay Chat, include/ircd.h
 *   Copyright (C) 1992 Darren Reed
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
 *
 * "ircd.h". - Headers file.
 *
 * $Id$
 *
 */
#ifndef INCLUDED_ircd_h
#define INCLUDED_ircd_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

#ifndef __TOOLS_H__
#include "tools.h"
#endif

struct Client;
struct dlink_list;

struct SetOptions
{
  int maxclients;       /* max clients allowed */
  int autoconn;         /* autoconn enabled for all servers? */

  int idletime;

  int floodcount;	/* Number of messages in 1 second */

  int spam_num;
  int spam_time;

  int hide_server;
  int hide_chanops;
};

struct Counter {
  int     server;         /* servers */
  int     myserver;       /* my servers */
  int     oper;           /* Opers */
  int     chan;           /* Channels */
  int     local;          /* Local Clients */
  int     total;          /* total clients */
  int     invisi;         /* invisible clients */
  int     unknown;        /* unknown connections */
  int     max_loc;        /* MAX local clients */
  int     max_tot;        /* MAX global clients */
  unsigned long totalrestartcount; /* Total client count ever */
};

extern struct SetOptions GlobalSetOptions;  /* defined in ircd.c */

extern char*          debugmode;
extern int            debuglevel;
extern int            debugtty;
extern char*          creation;
extern char*          generation;
extern char*          platform;
extern char*          infotext[];
extern char*          serno;
extern char*          version;
extern const char     serveropts[];
extern int            LRV;
extern int            cold_start;
extern int            dline_in_progress;
extern int            dorehash;
extern int            rehashed;
extern float          currlife;
extern struct Client  me;
extern struct Client* GlobalClientList;
extern struct Client* local[];
extern struct Counter Count;
extern time_t         CurrentTime;
extern time_t         LCF;
extern time_t         nextconnect;

extern dlink_list unknown_list;
extern dlink_list lclient_list;
extern dlink_list serv_list;
extern dlink_list oper_list;

#ifdef REJECT_HOLD
extern int reject_held_fds;
#endif

extern size_t   get_maxrss(void);

/* 1800 == half an hour
 * if clock set back more than this length of time
 * complain
 */
#define MAX_SETBACK_TIME 1800
#endif
