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
#include "send.h"
#include "handlers.h"
#include "numeric.h"
#include "ircd_defs.h"
#include "irc_string.h"

static char unknown_ver[] = "<unknown>";

struct module **modlist = NULL;

#define MODS_INCREMENT 10
int num_mods = 0;
int max_mods = MODS_INCREMENT;
static void increase_modlist(void);
static int findmodule_byname (char *name);

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


static int findmodule_byname (char *name)
{
  int i;

  for (i = 0; i < num_mods; i++) 
    {
      if (!irccmp (modlist[i]->name, name))
	return i;
    }
  return -1;
}

/*
 * unload_one_module 
 *
 * inputs	- name of module to unload
 * output	- 0 if successful, -1 if error
 * side effects	- module is unloaded
 */
int unload_one_module (char *name)
{
  int index;
  void (*deinitfunc)(void) = NULL;

  if ((index = findmodule_byname (name)) == -1) 
    return -1;

  deinitfunc = (void (*)(void))dlsym (modlist[index]->address, "_moddeinit");

  if( deinitfunc != NULL )
    {
      deinitfunc ();
    }

  MyFree(modlist[index]->name);
  memcpy( &modlist[index], &modlist[index+1],
	  sizeof(struct module) * ((num_mods-1) - index) );

  if(num_mods != 0)
    num_mods--;

  log (L_INFO, "Module %s unloaded", name);
  sendto_realops_flags(FLAGS_ALL,"Module %s unloaded", name);
  return 0;
}

struct Message modload_msgtab = {
  MSG_MODLOAD, 0, 2, MFLG_SLOW, 0,
  {m_unregistered, m_ignore, m_ignore, mo_modload}
};

struct Message modunload_msgtab = {
  MSG_MODUNLOAD, 0, 2, MFLG_SLOW, 0,
  {m_unregistered, m_ignore, m_ignore, mo_modunload}
};

struct Message modlist_msgtab = {
  MSG_MODLIST, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_ignore, m_ignore, mo_modlist}
};

struct Message hash_msgtab = {
  MSG_HASH, 0, 2, MFLG_SLOW, 0,
  {m_unregistered, m_ignore, m_ignore, mo_hash}
};

/* load all modules from MPATH */
void
load_all_modules (void)
{
  char           *system_module_dir_name = MODPATH;
  DIR            *system_module_dir = NULL;
  struct dirent  *ldirent = NULL;
  char           *old_dir = getcwd (NULL, 0); /* XXX not portable */
  char            module_fq_name[PATH_MAX + 1];

  modlist = (struct module **)MyMalloc ( sizeof (struct module) *
					     (MODS_INCREMENT));
  max_mods = MODS_INCREMENT;

  mod_add_cmd(&modload_msgtab);
  mod_add_cmd(&modunload_msgtab);
  mod_add_cmd(&modlist_msgtab);
  mod_add_cmd(&hash_msgtab);

  if (chdir (system_module_dir_name) == -1)
    {
      log (L_WARN, "Could not load modules from %s: %s",
	   system_module_dir_name, strerror (errno));
      return;
    }

  system_module_dir = opendir (".");
  if (system_module_dir == NULL)
    {
      log (L_WARN, "Could not load modules from %s: %s",
	   system_module_dir_name, strerror (errno));
      return;
    }

  while ((ldirent = readdir (system_module_dir)) != NULL)
    {
      if (ldirent->d_name [strlen (ldirent->d_name) - 3] == '.' &&
	  ldirent->d_name [strlen (ldirent->d_name) - 2] == 's' &&
	  ldirent->d_name [strlen (ldirent->d_name) - 1] == 'o')
	{
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

/*
 * load_one_module
 *
 * inputs	- path name of module
 * output	- -1 if error 0 if success
 * side effects - loads a module if successful
 */
int
load_one_module (char *path)
{
  void *tmpptr;
  char *mod_basename;
  void (*initfunc)(void) = NULL;
  char **verp;
  char *ver;

  mod_basename = basename(path);

  errno = 0;
  tmpptr = dlopen (path, RTLD_NOW);

  if (tmpptr == NULL)
    {
      const char *err = dlerror();

      sendto_realops_flags (FLAGS_ALL,
			    "Error loading module %s: %s", mod_basename, err);
      log (L_WARN, "Error loading module %s: %s", mod_basename, err);
      free (mod_basename);
      return -1;
    }

  initfunc = (void (*)(void))dlsym (tmpptr, "_modinit");
  if (!initfunc)
    {
      sendto_realops_flags (FLAGS_ALL,
		    "Module %s has no _modinit() function", mod_basename);
      log (L_WARN, "Module %s has no _modinit() function", mod_basename);
      (void)dlclose (tmpptr);
      free (mod_basename);
      return -1;
    }

  if (!(verp = (char **)dlsym (tmpptr, "_version")))
    ver = unknown_ver;
  else
    ver = *verp;

  increase_modlist();

  modlist [num_mods] = malloc (sizeof (struct module));
  modlist [num_mods]->address = tmpptr;
  modlist [num_mods]->version = ver;
  DupString(modlist [num_mods]->name, mod_basename );
  num_mods++;

  initfunc ();

  sendto_realops_flags (FLAGS_ALL, "Module %s [version: %s] loaded at 0x%x",
			mod_basename, ver, tmpptr);
  log (L_WARN, "Module %s [version: %s] loaded at 0x%x",
       mod_basename, ver, tmpptr);
  free (mod_basename);

  return 0;
}

/*
 * increase_modlist
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	- expand the size of modlist if necessary
 */
static void increase_modlist(void)
{
  struct module **new_modlist = NULL;

  if((num_mods + 1) < max_mods)
    return;

  new_modlist = (struct module **)MyMalloc ( sizeof (struct module) *
					     (max_mods + MODS_INCREMENT));
  memcpy((void *)new_modlist,
	 (void *)modlist, sizeof(struct module) * num_mods);

  MyFree(modlist);
  modlist = new_modlist;
  max_mods += MODS_INCREMENT;
}

/* load a module .. */
int
mo_modload (struct Client *cptr, struct Client *sptr, int parc, char **parv)
{
  char *m_bn;

  if (!IsSetOperAdmin(sptr))
    {
      sendto_one(sptr, ":%s NOTICE %s :You have no A flag", me.name, parv[0]);
      return 0;
    }

  m_bn = basename (parv[1]);

  if (findmodule_byname (m_bn) != -1)
    {
      sendto_one (sptr, ":%s NOTICE %s :Module %s is already loaded",
		  me.name, sptr->name, m_bn);
      return 0;
    }

  (void)load_one_module (parv[1]);
  return 0;
}


/* unload a module .. */
int
mo_modunload (struct Client *cptr, struct Client *sptr, int parc, char **parv)
{
  char *m_bn;

  if (!IsSetOperAdmin (sptr))
    {
      sendto_one (sptr, ":%s NOTICE %s :You have no A flag",
		  me.name, parv[0]);
      return 0;
    }

  m_bn = basename (parv[1]);

  if (findmodule_byname (m_bn) == -1)
    {
      sendto_one (sptr, ":%s NOTICE %s :Module %s is not loaded",
		  me.name, sptr->name, m_bn);
      free (m_bn);
      return 0;
    }

  if( unload_one_module (m_bn) == -1 )
    {
      sendto_one (sptr, ":%s NOTICE %s :Module %s is not loaded",
		  me.name, sptr->name, m_bn);
    }
  free (m_bn);
  return 0;
}

/* list modules .. */
int
mo_modlist (struct Client *cptr, struct Client *sptr, int parc, char **parv)
{
  int i;

  if (!IsSetOperAdmin (sptr))
    {
      sendto_one (sptr, ":%s NOTICE %s :You have no A flag",
		  me.name, parv[0]);
      return 0;
    }

  for(i = 0; i < num_mods; i++ )
    {
		sendto_one(sptr, form_str(RPL_MODLIST), me.name, parv[0],
				   modlist[i]->name,
				   modlist[i]->address,
				   modlist[i]->version);
    }
  return 0;
}




