/************************************************************************
 *   IRC - Internet Relay Chat, include/config.h
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

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "modules.h"
#include "s_log.h"

struct module **modlist = NULL;
int num_mods = 0;

struct module *
find_module(char *name)
{
  int i;

  for (i = 0; i < num_mods; i++) 
    {
      if (!irccmp(modlist[i]->name, name))
	return modlist[i];
    }
  return NULL;
}

void
load_module(char *path)
{
  void *tmpptr;
  char *mod_basename;
  char *s;
  void (*initfunc)(void) = NULL;

  mod_basename = malloc(strlen(path) + 1);
  strcpy(mod_basename, path);
  s = strrchr(mod_basename, '/');

  if (s)
    mod_basename = s + 1;

  errno = 0;
  tmpptr = dlopen(path, RTLD_LAZY);

  if (tmpptr == NULL) {
    char *err = dlerror();
    sendto_realops("Error loading module %s: %s", path, err);
    log(L_WARN, "Error loading module %s: %s", path, err);
    return;
  }

  initfunc = dlsym(tmpptr, "_modinit");
  if (!initfunc) {
    sendto_realops("Module %s has no _init() function", mod_basename);
    log(L_WARN, "Module %s has no _init() function", mod_basename);
    return;
  }

  modlist = realloc(modlist, sizeof(struct module) * (num_mods + 1));
  modlist[num_mods] = malloc(sizeof(struct module));
  modlist[num_mods]->address = tmpptr;
  modlist[num_mods]->name = malloc(strlen(mod_basename) + 1);
  strcpy(modlist[num_mods]->name, mod_basename);
  num_mods++;

  initfunc();
  sendto_realops("Module %s loaded at %x", mod_basename, tmpptr);
  log(L_WARN, "Module %s loaded at %x", mod_basename, tmpptr);
  free(mod_basename);
}
