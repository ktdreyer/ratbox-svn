/************************************************************************
 *   IRC - Internet Relay Chat, include/modules.h
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

#ifndef INCLUDED_modules_h
#define INCLUDED_modules_h

#include "ircd_handler.h"
#include "msg.h"
#include "memdebug.h"

struct module {
  char *name;
  char *version;
  void *address;
};

/* load a module */
extern void load_module(char *path);

/* load all modules */
extern void load_all_module(void);

extern void _modinit(void);
extern void _moddeinit(void);

#endif
