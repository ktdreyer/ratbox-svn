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
#include "ircd.h"
#include "client.h"

static char unknown_ver[] = "<unknown>";

struct module **modlist = NULL;
int num_mods = 0;

static char *
basename(char *path)
{
  char *mod_basename = malloc (strlen (path) + 1);
  char *s;
 
  if (!(s = strrchr (path, '/')))
    s = path;
  else
    s++;

  (void)strcpy (mod_basename, s);
  return mod_basename;
}

struct module *
findmodule_byname (char *name)
{
  int i;

  for (i = 0; i < num_mods; i++) 
    {
      if (!irccmp (modlist[i]->name, name))
	return modlist[i];
    }
  return NULL;
}

int
unload_one_module (char *name)
{
  struct module *mod;

  if ((mod = findmodule_byname (name)) == NULL) 
    return -1;

  free (mod->name);
  mod->name = modlist [num_mods - 1]->name;
  mod->version = modlist [num_mods - 1]->version;
  mod->address = modlist [num_mods - 1]->address;

  modlist = (struct module **)realloc (modlist, sizeof (struct module) * (num_mods - 1));
  
  log (L_INFO, "Module %s unloaded", name);
  sendto_realops ("Module %s unloaded", name);
  return 0;
}

/* load all modules from MPATH */
void
load_all_modules (void)
{
  char           *system_module_dir_name = MODPATH;
  DIR            *system_module_dir = NULL;
  struct dirent  *ldirent = NULL;
  char           *old_dir = getcwd (NULL, 0); /* XXX not portable */
  char            module_fq_name[PATH_MAX + 1];

  if (chdir (system_module_dir_name) == -1) {
    log (L_WARN, "Could not load modules from %s: %s",
	system_module_dir_name, strerror (errno));
    return;
  }

  system_module_dir = opendir (".");
  if (system_module_dir == NULL) {
    log (L_WARN, "Could not load modules from %s: %s",
	system_module_dir_name, strerror (errno));
    return;
  }

  while ((ldirent = readdir (system_module_dir)) != NULL) {
    if (ldirent->d_name [strlen (ldirent->d_name) - 3] == '.' &&
        ldirent->d_name [strlen (ldirent->d_name) - 2] == 's' &&
        ldirent->d_name [strlen (ldirent->d_name) - 1] == 'o') {
      (void)snprintf (module_fq_name, sizeof (module_fq_name),
        "%s/%s",  system_module_dir_name,
        ldirent->d_name);
      (void)load_one_module (module_fq_name);
    }
  }

  (void)closedir (system_module_dir);
  (void)chdir (old_dir);
  free (old_dir);
}

int
load_one_module (char *path)
{
  void *tmpptr;
  char *mod_basename, *s;
  void (*initfunc)(void) = NULL;
  char *ver;


  mod_basename = basename(path);

  errno = 0;
  tmpptr = dlopen (path, RTLD_LAZY);

  if (tmpptr == NULL) {
    const char *err = dlerror();

    sendto_realops ("Error loading module %s: %s", mod_basename, err);
    log (L_WARN, "Error loading module %s: %s", mod_basename, err);
    free (mod_basename);
    return -1;
  }

  initfunc = (void (*)(void))dlsym (tmpptr, "_modinit");
  if (!initfunc) {
    sendto_realops ("Module %s has no _modinit() function", mod_basename);
    log (L_WARN, "Module %s has no _modinit() function", mod_basename);
    (void)dlclose (tmpptr);
    free (mod_basename);
    return -1;
  }

  if (!(ver = (char *)dlsym (tmpptr, "_modver")));
    ver = unknown_ver;

  modlist = (struct module **)realloc (modlist, sizeof (struct module) * (num_mods + 1));
  modlist [num_mods] = malloc (sizeof (struct module));
  modlist [num_mods]->address = tmpptr;
  modlist [num_mods]->version = ver;
  modlist [num_mods]->name = malloc (strlen (mod_basename) + 1);
  (void)strcpy (modlist[num_mods]->name, mod_basename);
  num_mods++;

  initfunc ();

  sendto_realops ("Module %s [version: %s] loaded at 0x%x", mod_basename, ver, tmpptr);
  log (L_WARN, "Module %s [version: %s] loaded at 0x%x", mod_basename, ver, tmpptr);
  free (mod_basename);

  return 0;
}

/* load a module .. */
int
mo_modload (struct Client *cptr, struct Client *sptr, int parc, char **parv)
{
  char *m_bn = basename (parv[1]);

  if (!IsSetOperAdmin(sptr)) {
    sendto_one(sptr, ":%s NOTICE %s :You have no M flag", me.name, parv[0]);
    return 0;
  }

  if (findmodule_byname (m_bn)) {
    sendto_one (sptr, ":%s NOTICE %s :Module %s is already loaded", me.name, m_bn);
    return 0;
  }

  (void)load_one_module (parv[1]);
  return 0;
}

/* unload a module .. */
int
mo_modunload (struct Client *cptr, struct Client *sptr, int parc, char **parv)
{
  char *m_bn = basename (parv[1]);

  if (!IsSetOperAdmin (sptr)) {
    sendto_one (sptr, ":%s NOTICE %s :You have no M flag", me.name, parv[0]);
    free (m_bn);
    return 0;
  }

  if (!findmodule_byname (m_bn)) {
    sendto_one (sptr, ":%s NOTICE %s :Module %s is not loaded", me.name, m_bn);
    free (m_bn);
    return 0;
  }

  (void)unload_one_module (parv[1]);
  free (m_bn);
}
