/************************************************************************
 *   IRC - Internet Relay Chat, include/resv.h
 *   Copyright (C) 1991 Hybrid Development Team
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
 *   $Id$
 */
#ifndef INCLUDED_resv_h
#define INCLUDED_resv_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

struct Resv
{
  struct Resv *next;
  struct Resv *prev;
  struct Resv *hnext;

  char	name[CHANNELLEN];
  char	*reason;
  int	type;
  int	conf;
};

#define RESV_NICK 0
#define RESV_CHANNEL 1

extern struct Resv *ResvList;

extern struct Resv *create_resv(char *, char *, int, int);
extern int delete_resv(struct Resv *);
extern int clear_conf_resv();
extern int find_resv(char *name, int type);

#endif  /* INCLUDED_hash_h */



