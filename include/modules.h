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

struct module {
  char *name;
  void *address;
};

/* load a module */
void load_module(char *path);

/* add a command */
void mod_add_cmd(char *cmd, struct Message *msg);

#endif
