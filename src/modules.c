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
#include <dirent.h>
#include <limits.h>
#include <unistd.h>

#include "modules.h"
#include "s_log.h"
#include "config.h"

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

/* load all modules from MPATH */
void
load_all_modules(void)
{
  char           *system_module_dir_name = MODPATH;
  DIR            *system_module_dir = NULL;
  struct dirent  *ldirent = NULL;
  char           *old_dir = getcwd(NULL, 0);
  char            module_fq_name[PATH_MAX + 1];

  if (chdir(system_module_dir_name) == -1) {
    log(L_WARN, "Could not load modules from %s: %s",
	system_module_dir_name, strerror(errno));
    exit(0);
  }

  system_module_dir = opendir(".");
  if (system_module_dir == NULL) {
    log(L_WARN, "Could not load modules from %s: %s",
	system_module_dir_name, strerror(errno));
    return;
  }

  while ((ldirent = readdir(system_module_dir)) != NULL) {
    if (ldirent->d_name[strlen(ldirent->d_name) - 3] == '.' &&
        ldirent->d_name[strlen(ldirent->d_name) - 2] == 's' &&
        ldirent->d_name[strlen(ldirent->d_name) - 1] == 'o') {
      snprintf(module_fq_name, sizeof(module_fq_name),
        "%s/%s",  system_module_dir_name,
        ldirent->d_name);
      load_module(module_fq_name);
    }
  }

  closedir(system_module_dir);
  chdir(old_dir);
  free(old_dir);
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
    const char *err = dlerror();
    sendto_realops("Error loading module %s: %s", path, err);
    log(L_WARN, "Error loading module %s: %s", path, err);
    free(mod_basename);
    return;
  }

  initfunc = dlsym(tmpptr, "_modinit");
  if (!initfunc) {
    sendto_realops("Module %s has no _init() function", mod_basename);
    log(L_WARN, "Module %s has no _init() function", mod_basename);
    dlclose(tmpptr);
    free(mod_basename);
    return;
  }

  modlist = realloc(modlist, sizeof(struct module) * (num_mods + 1));
  modlist[num_mods] = malloc(sizeof(struct module));
  modlist[num_mods]->address = tmpptr;
  modlist[num_mods]->name = malloc(strlen(mod_basename) + 1);
  strcpy(modlist[num_mods]->name, mod_basename);
  num_mods++;

  initfunc();

  sendto_realops("Module %s loaded at 0x%x", mod_basename, tmpptr);
  log(L_WARN, "Module %s loaded at 0x%x", mod_basename, tmpptr);
  free(mod_basename);
}
