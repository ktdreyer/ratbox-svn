/************************************************************************
 *   IRC - Internet Relay Chat, src/modules.c
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
 *
 * This is based on modules.c, but for OSes like HP-UX which use shl_open
 * instead of dlopen
 *
 * This is completely and totally untested, so if you got an HP-UX box. Give
 * it a try. Also you'll need to link against -lld instead of -ldl 
 * --Aaron
 */

#include <dl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "modules.h"
#include "s_log.h"
#include "config.h"
#include "ircd.h"
#include "client.h"
#include "send.h"
#include "handlers.h"
#include "numeric.h"
#include "parse.h"
#include "ircd_defs.h"
#include "irc_string.h"
#include "memory.h"
#include "tools.h"
#include "list.h"

#ifndef RTLD_NOW
#define RTLD_NOW RTLD_LAZY /* openbsd deficiency */
#endif

static char unknown_ver[] = "<unknown>";

struct module **modlist = NULL;

#define MODS_INCREMENT 10
int num_mods = 0;
int max_mods = MODS_INCREMENT;
static void increase_modlist(void);

static dlink_list mod_paths;

static void mo_modload(struct Client*, struct Client*, int, char**);
static void mo_modlist(struct Client*, struct Client*, int, char**);
static void mo_modreload(struct Client*, struct Client*, int, char**);
static void mo_modunload(struct Client*, struct Client*, int, char**);
static void mo_modrestart(struct Client*, struct Client*, int, char**);

struct Message modload_msgtab = {
 "MODLOAD", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_modload}
};

struct Message modunload_msgtab = {
 "MODUNLOAD", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_modunload}
};

struct Message modreload_msgtab = {
  "MODRELOAD", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_modreload}
};

struct Message modlist_msgtab = {
 "MODLIST", 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_modlist}
};

struct Message modrestart_msgtab = {
 "MODRESTART", 0, 0, 0, MFLG_SLOW, 0,
 {m_unregistered, m_not_oper, m_ignore, mo_modrestart}
};

void
modules_init(void)
{
	mod_add_cmd(&modload_msgtab);
	mod_add_cmd(&modunload_msgtab);
        mod_add_cmd(&modreload_msgtab);
	mod_add_cmd(&modlist_msgtab);
	mod_add_cmd(&modrestart_msgtab);
}

static struct module_path *
mod_find_path(char *path)
{
  dlink_node *pathst = mod_paths.head;
  struct module_path *mpath;
  
  if (!pathst)
    return NULL;

  for (; pathst; pathst = pathst->next) {
	  mpath = (struct module_path *)pathst->data;
	  
	  if (!strcmp(path, mpath->path))
		  return mpath;
  }
  
  return NULL;
}

void
mod_add_path(char *path)
{
  struct module_path *pathst;
  dlink_node *node;
  
  if (mod_find_path(path))
    return;

  pathst = MyMalloc (sizeof (struct module_path));
  node = make_dlink_node();
  
  strcpy(pathst->path, path);
  dlinkAdd(pathst, node, &mod_paths);
}


char *
irc_basename(char *path)
{
  char *mod_basename = MyMalloc (strlen (path) + 1);
  char *s;

  if (!(s = strrchr (path, '/')))
    s = path;
  else
    s++;

  (void)strcpy (mod_basename, s);
  return mod_basename;
}


int 
findmodule_byname (char *name)
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
 *		- 1 to say modules unloaded, 0 to not
 * output	- 0 if successful, -1 if error
 * side effects	- module is unloaded
 */
int unload_one_module (char *name, int check)
{
  int modindex;
  void (*deinitfunc)(void) = NULL;

  if ((modindex = findmodule_byname (name)) == -1) 
    return -1;

  if(shl_findsym(modlist[modindex]->address, "_moddeinit", TYPE_UNDEFINED, &deinitfunc) == 0)
    deinitfunc ();
  else
    if(shl_findsym(modlist[modindex]->address, "__moddeinit", TYPE_UNDEFINED. &deinitfunc) == 0)
    	deinitfunc ();	

  shl_unload(modlist[modindex]->address);

  MyFree(modlist[modindex]->name);
  memcpy( &modlist[modindex], &modlist[modindex+1],
          sizeof(struct module) * ((num_mods-1) - modindex) );

  if(num_mods != 0)
    num_mods--;

  if(check == 1)
    {
      ilog (L_INFO, "Module %s unloaded", name);
      sendto_realops_flags(FLAGS_ALL, L_ALL,"Module %s unloaded", name);
    }

  return 0;
}

/* load all modules from MPATH */
void
load_all_modules (int check)
{
  DIR            *system_module_dir = NULL;
  struct dirent  *ldirent = NULL;
  char            module_fq_name[PATH_MAX + 1];

  modules_init();
  
  modlist = (struct module **)MyMalloc ( sizeof (struct module) *
                                         (MODS_INCREMENT));

  max_mods = MODS_INCREMENT;

  system_module_dir = opendir (MODPATH);

  if (system_module_dir == NULL)
  {
    ilog (L_WARN, "Could not load modules from %s: %s",
         MODPATH, strerror (errno));
    return;
  }

  while ((ldirent = readdir (system_module_dir)) != NULL)
  {
    if (ldirent->d_name [strlen (ldirent->d_name) - 3] == '.' &&
        ldirent->d_name [strlen (ldirent->d_name) - 2] == 's' &&
        ldirent->d_name [strlen (ldirent->d_name) - 1] == 'o')
    {
      (void)sprintf (module_fq_name, "%s/%s",  MODPATH,
                      ldirent->d_name);
      (void)load_a_module (module_fq_name, check);
    }
  }

  (void)closedir (system_module_dir);
}

int
load_one_module (char *path)
{
	char modpath[MAXPATHLEN];
	dlink_node *pathst;
	struct module_path *mpath;
	
	struct stat statbuf;

	if (strchr(path, '/')) /* absolute path, try it */
		return load_a_module(path, 1);

	for (pathst = mod_paths.head; pathst; pathst = pathst->next)
	{
		mpath = (struct module_path *)pathst->data;
		
		sprintf(modpath, "%s/%s", mpath->path, path);
		if (stat(modpath, &statbuf) == 0)
			return load_a_module(modpath, 1);
	}
	
	sendto_realops_flags (FLAGS_ALL, "Cannot locate module %s", path);
	ilog(L_WARN, "Cannot locate module %s", path);
	return -1;
}
		

/*
 * load_a_module
 *
 * inputs	- path name of module
 * output	- -1 if error 0 if success
 * side effects - loads a module if successful
 */
int
load_a_module (char *path, int check)
{
  void *tmpptr = NULL;
  char *mod_basename;
  void (*initfunc)(void) = NULL;
  char **verp;
  char *ver;

  mod_basename = irc_basename(path);

  tmpptr = shl_load(path, BIND_IMMEDIATE);
  tmpptr = dlopen (path, RTLD_NOW);


  if (tmpptr == NULL)
  {
      const char *err = strerror(errno);
	  
      sendto_realops_flags (FLAGS_ALL,
                            "Error loading module %s: %s",
                            mod_basename, err);
      ilog (L_WARN, "Error loading module %s: %s", mod_basename, err);
      MyFree (mod_basename);
      return -1;
  }

  if(shl_findsym (tmpptr, "_modinit", TYPE_UNDEFINED, &initfunc) == -1)
  {
    if(shl_findsym(tmpptr, "__modinit", TYPE_UNDEFINE, &initfunc) == -1)
    {
    	sendto_realops_flags (FLAGS_ALL,
        	                  "Module %s has no _modinit() function",
        	                  mod_basename);
    	ilog (L_WARN, "Module %s has no _modinit() function", mod_basename);
    	shl_unload (tmpptr);
    	MyFree (mod_basename);
    	return -1;
    }
  }

  if(shl_findsym(tmpptr, "_version", TYPE_UNDEFINED, &verp) == -1)
  {
    if(shl_findsym(tmpptr, "__version", TYPE_UNDEFINED, &verp) == -1)
	    ver = unknown_ver;
    else
    	    ver = verp;
  } else
    ver = *verp;

  increase_modlist();

  modlist [num_mods] = MyMalloc (sizeof (struct module));
  modlist [num_mods]->address = tmpptr;
  modlist [num_mods]->version = ver;
  DupString(modlist [num_mods]->name, mod_basename );
  num_mods++;

  initfunc ();

  if(check == 1)
    {
      sendto_realops_flags (FLAGS_ALL, "Module %s [version: %s] loaded at 0x%lx",
                        mod_basename, ver, (unsigned long)tmpptr);
       ilog (L_WARN, "Module %s [version: %s] loaded at 0x%x",
            mod_basename, ver, tmpptr);
    }
  MyFree (mod_basename);
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
static void
mo_modload (struct Client *client_p, struct Client *source_p, int parc, char **parv)
{
  char *m_bn;

  if (!IsOperAdmin(source_p))
  {
    sendto_one(source_p, ":%s NOTICE %s :You have no A flag", me.name, parv[0]);
    return;
  }

  m_bn = irc_basename (parv[1]);

  if (findmodule_byname (m_bn) != -1)
  {
    sendto_one (source_p, ":%s NOTICE %s :Module %s is already loaded",
                me.name, source_p->name, m_bn);
    return;
  }

  load_one_module (parv[1]);

  MyFree(m_bn);
}


/* unload a module .. */
static void
mo_modunload (struct Client *client_p, struct Client *source_p, int parc, char **parv)
{
  char *m_bn;

  if (!IsOperAdmin (source_p))
  {
    sendto_one (source_p, ":%s NOTICE %s :You have no A flag",
                me.name, parv[0]);
    return;
  }

  m_bn = irc_basename (parv[1]);

  if (findmodule_byname (m_bn) == -1)
  {
    sendto_one (source_p, ":%s NOTICE %s :Module %s is not loaded",
                me.name, source_p->name, m_bn);
    MyFree (m_bn);
    return;
  }

  if( unload_one_module (m_bn, 1) == -1 )
  {
    sendto_one (source_p, ":%s NOTICE %s :Module %s is not loaded",
                me.name, source_p->name, m_bn);
  }
  MyFree (m_bn);
}

/* unload and load in one! */
static void
mo_modreload (struct Client *client_p, struct Client *source_p, int parc, char **parv)
{
  char *m_bn;

  if (!IsOperAdmin (source_p))
    {
      sendto_one (source_p, ":%s NOTICE %s :You have no A flag",
                  me.name, parv[0]);
      return;
    }

  m_bn = irc_basename (parv[1]);

  if (findmodule_byname (m_bn) == -1)
    {
      sendto_one (source_p, ":%s NOTICE %s :Module %s is not loaded",
                  me.name, source_p->name, m_bn);
      MyFree (m_bn);
      return;
    }

  if( unload_one_module (m_bn, 1) == -1 )
    {
      sendto_one (source_p, ":%s NOTICE %s :Module %s is not loaded",
                  me.name, source_p->name, m_bn);
      MyFree (m_bn);
      return;
    }

  (void)load_one_module (parv[1]);

  MyFree(m_bn);
}

/* list modules .. */
static void
mo_modlist (struct Client *client_p, struct Client *source_p, int parc, char **parv)
{
  int i;

  if (!IsOperAdmin (source_p))
  {
    sendto_one (source_p, ":%s NOTICE %s :You have no A flag",
                me.name, parv[0]);
    return;
  }

  for(i = 0; i < num_mods; i++ )
  {
    if(parc > 1)
    {
      if(match(parv[1],modlist[i]->name))
      {
        sendto_one(source_p, form_str(RPL_MODLIST), me.name, parv[0],
                   modlist[i]->name, modlist[i]->address,
                   modlist[i]->version);
      }
    }
    else
    {
      sendto_one(source_p, form_str(RPL_MODLIST), me.name, parv[0],
                 modlist[i]->name, modlist[i]->address,
                 modlist[i]->version);
    }
  }
  
  sendto_one(source_p, form_str(RPL_ENDOFMODLIST), me.name, parv[0]);
}

/* unload and reload all modules */
static void
mo_modrestart (struct Client *client_p, struct Client *source_p, int parc, char **parv)

{
  int modnum;

  if (!IsOperAdmin (source_p))
  {
    sendto_one (source_p, ":%s NOTICE %s :You have no A flag",
                me.name, parv[0]);
    return;
  }

  sendto_one(source_p, ":%s NOTICE %s :Reloading all modules",
             me.name, parv[0]);

  modnum = num_mods;
  while (num_mods)
     unload_one_module(modlist[0]->name, 0);

  load_all_modules(0);

  sendto_realops_flags (FLAGS_ALL, "Module Restart: %d modules unloaded, %d modules loaded",
			modnum, num_mods);
  ilog(L_WARN, "Module Restart: %d modules unloaded, %d modules loaded",
      modnum, num_mods);
}
