/*
 *  ircd-ratbox: A slightly useful ircd.
 *  modules.c: A module loader.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"


#include "modules.h"
#include "s_log.h"
#include "ircd.h"
#include "client.h"
#include "send.h"
#include "s_conf.h"
#include "handlers.h"
#include "numeric.h"
#include "parse.h"
#include "ircd_defs.h"
#include "irc_string.h"
#include "memory.h"
#include "tools.h"

/* -TimeMr14C:
 * I have moved the dl* function definitions and
 * the two functions (load_a_module / unload_a_module) to the
 * file dynlink.c 
 * And also made the necessary changes to those functions
 * to comply with shl_load and friends.
 * In this file, to keep consistency with the makefile, 
 * I added the ability to load *.sl files, too.
 * 27/02/2002
 */

#ifndef STATIC_MODULES

struct module **modlist = NULL;

static const char *core_module_table[] = {
	"m_die.s",
	"m_kick.s",
	"m_kill.s",
	"m_message.s",
	"m_mode.s",
	"m_nick.s",
	"m_part.s",
	"m_quit.s",
	"m_server.s",
	"m_sjoin.s",
	"m_squit.s",
	NULL
};

#define MODS_INCREMENT 10
int num_mods = 0;
int max_mods = MODS_INCREMENT;

static dlink_list mod_paths;

static int mo_modload(struct Client *, struct Client *, int, const char **);
static int mo_modlist(struct Client *, struct Client *, int, const char **);
static int mo_modreload(struct Client *, struct Client *, int, const char **);
static int mo_modunload(struct Client *, struct Client *, int, const char **);
static int mo_modrestart(struct Client *, struct Client *, int, const char **);

struct Message modload_msgtab = {
	"MODLOAD", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_not_oper, m_ignore, mo_modload}
};

struct Message modunload_msgtab = {
	"MODUNLOAD", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_not_oper, m_ignore, mo_modunload}
};

struct Message modreload_msgtab = {
	"MODRELOAD", 0, 0, 2, 0, MFLG_SLOW, 0,
	{m_unregistered, m_not_oper, m_ignore, mo_modreload}
};

struct Message modlist_msgtab = {
	"MODLIST", 0, 0, 0, 0, MFLG_SLOW, 0,
	{m_unregistered, m_not_oper, m_ignore, mo_modlist}
};

struct Message modrestart_msgtab = {
	"MODRESTART", 0, 0, 0, 0, MFLG_SLOW, 0,
	{m_unregistered, m_not_oper, m_ignore, mo_modrestart}
};

extern struct Message error_msgtab;

#ifdef FL_DEBUG
extern struct Message hash_msgtab;
#endif

void
modules_init(void)
{
	mod_add_cmd(&modload_msgtab);
	mod_add_cmd(&modunload_msgtab);
	mod_add_cmd(&modreload_msgtab);
	mod_add_cmd(&modlist_msgtab);
	mod_add_cmd(&modrestart_msgtab);
	mod_add_cmd(&error_msgtab);
#ifdef FL_DEBUG
	mod_add_cmd(&hash_msgtab);
#endif
}

/* mod_find_path()
 *
 * input	- path
 * output	- none
 * side effects - returns a module path from path
 */
static struct module_path *
mod_find_path(const char *path)
{
	dlink_node *ptr;
	struct module_path *mpath;

	DLINK_FOREACH(ptr, mod_paths.head)
	{
		mpath = ptr->data;

		if(!strcmp(path, mpath->path))
			return mpath;
	}

	return NULL;
}

/* mod_add_path
 *
 * input	- path
 * ouput	- 
 * side effects - adds path to list
 */
void
mod_add_path(const char *path)
{
	struct module_path *pathst;

	if(mod_find_path(path))
		return;

	pathst = MyMalloc(sizeof(struct module_path));

	strcpy(pathst->path, path);
	dlinkAddAlloc(pathst, &mod_paths);
}

/* mod_clear_paths()
 *
 * input	-
 * output	-
 * side effects - clear the lists of paths
 */
void
mod_clear_paths(void)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, mod_paths.head)
	{
		MyFree(ptr->data);
		free_dlink_node(ptr);
	}

	mod_paths.head = mod_paths.tail = NULL;
	mod_paths.length = 0;
}

/* irc_basename
 *
 * input	-
 * output	-
 * side effects -
 */
char *
irc_basename(const char *path)
{
	char *mod_basename = MyMalloc(strlen(path) + 1);
	const char *s;

	if(!(s = strrchr(path, '/')))
		s = path;
	else
		s++;

	(void) strcpy(mod_basename, s);
	return mod_basename;
}

/* findmodule_byname
 *
 * input        -
 * output       -
 * side effects -
 */

int
findmodule_byname(const char *name)
{
	int i;

	for (i = 0; i < num_mods; i++)
	{
		if(!irccmp(modlist[i]->name, name))
			return i;
	}
	return -1;
}

/* load_all_modules()
 *
 * input        -
 * output       -
 * side effects -
 */
void
load_all_modules(int warn)
{
	DIR *system_module_dir = NULL;
	struct dirent *ldirent = NULL;
	char module_fq_name[PATH_MAX + 1];
	int len;

	modules_init();

	modlist = (struct module **) MyMalloc(sizeof(struct module) * (MODS_INCREMENT));

	max_mods = MODS_INCREMENT;

	system_module_dir = opendir(AUTOMODPATH);

	if(system_module_dir == NULL)
	{
		ilog(L_WARN, "Could not load modules from %s: %s", AUTOMODPATH, strerror(errno));
		return;
	}

	while ((ldirent = readdir(system_module_dir)) != NULL)
	{
		len = strlen(ldirent->d_name);

		/* On HPUX, we have *.sl as shared library extension
		 * -TimeMr14C */

		if((len > 3) &&
		   (ldirent->d_name[len - 3] == '.') &&
		   (ldirent->d_name[len - 2] == 's') &&
		   ((ldirent->d_name[len - 1] == 'o') || (ldirent->d_name[len - 1] == 'l')))
		{
			(void) sprintf(module_fq_name, "%s/%s", AUTOMODPATH, ldirent->d_name);
			(void) load_a_module(module_fq_name, warn, 0);
		}
	}

	(void) closedir(system_module_dir);
}

/* load_core_modules()
 *
 * input        -
 * output       -
 * side effects - core modules are loaded, if any fail, kill ircd
 */
void
load_core_modules(int warn)
{
	char module_name[MAXPATHLEN];
	int i, hpux = 0;

#ifdef HAVE_SHL_LOAD
	hpux = 1;
#endif

	for (i = 0; core_module_table[i]; i++)
	{
		sprintf(module_name, "%s/%s%c", MODPATH, core_module_table[i], hpux ? 'l' : 'o');

		if(load_a_module(module_name, warn, 1) == -1)
		{
			ilog(L_CRIT,
			     "Error loading core module %s%c: terminating ircd",
			     core_module_table[i], hpux ? 'l' : 'o');
			exit(0);
		}
	}
}

/* load_one_module()
 *
 * input        -
 * output       -
 * side effects -
 */
int
load_one_module(const char *path, int coremodule)
{
	char modpath[MAXPATHLEN];
	dlink_node *pathst;
	struct module_path *mpath;

	struct stat statbuf;

	DLINK_FOREACH(pathst, mod_paths.head)
	{
		mpath = pathst->data;

		sprintf(modpath, "%s/%s", mpath->path, path);
		if((strstr(modpath, "../") == NULL) && (strstr(modpath, "/..") == NULL))
		{
			if(stat(modpath, &statbuf) == 0)
			{
				if(S_ISREG(statbuf.st_mode))
				{
					/* Regular files only please */
					if(coremodule)
						return load_a_module(modpath, 1, 1);
					else
						return load_a_module(modpath, 1, 0);
				}
			}

		}
	}

	sendto_realops_flags(UMODE_ALL, L_ALL, "Cannot locate module %s", path);
	ilog(L_WARN, "Cannot locate module %s", path);
	return -1;
}


/* load a module .. */
static int
mo_modload(struct Client *client_p, struct Client *source_p, int parc, const char **parv)
{
	char *m_bn;

	if(!IsOperAdmin(source_p))
	{
		sendto_one(source_p, ":%s NOTICE %s :You have no A flag", me.name, parv[0]);
		return 0;
	}

	m_bn = irc_basename(parv[1]);

	if(findmodule_byname(m_bn) != -1)
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Module %s is already loaded",
			   me.name, source_p->name, m_bn);
		MyFree(m_bn);
		return 0;
	}

	load_one_module(parv[1], 0);

	MyFree(m_bn);

	return 0;
}


/* unload a module .. */
static int
mo_modunload(struct Client *client_p, struct Client *source_p, int parc, const char **parv)
{
	char *m_bn;
	int modindex;

	if(!IsOperAdmin(source_p))
	{
		sendto_one(source_p, ":%s NOTICE %s :You have no A flag", me.name, parv[0]);
		return 0;
	}

	m_bn = irc_basename(parv[1]);

	if((modindex = findmodule_byname(m_bn)) == -1)
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Module %s is not loaded", me.name, source_p->name, m_bn);
		MyFree(m_bn);
		return 0;
	}

	if(modlist[modindex]->core == 1)
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Module %s is a core module and may not be unloaded",
			   me.name, source_p->name, m_bn);
		MyFree(m_bn);
		return 0;
	}

	if(unload_one_module(m_bn, 1) == -1)
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Module %s is not loaded", me.name, source_p->name, m_bn);
	}
	MyFree(m_bn);
	return 0;
}

/* unload and load in one! */
static int
mo_modreload(struct Client *client_p, struct Client *source_p, int parc, const char **parv)
{
	char *m_bn;
	int modindex;
	int check_core;

	if(!IsOperAdmin(source_p))
	{
		sendto_one(source_p, ":%s NOTICE %s :You have no A flag", me.name, parv[0]);
		return 0;
	}

	m_bn = irc_basename(parv[1]);

	if((modindex = findmodule_byname(m_bn)) == -1)
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Module %s is not loaded", me.name, source_p->name, m_bn);
		MyFree(m_bn);
		return 0;
	}

	check_core = modlist[modindex]->core;

	if(unload_one_module(m_bn, 1) == -1)
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Module %s is not loaded", me.name, source_p->name, m_bn);
		MyFree(m_bn);
		return 0;
	}

	if((load_one_module(parv[1], check_core) == -1) && check_core)
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Error reloading core module: %s: terminating ircd", parv[1]);
		ilog(L_CRIT, "Error loading core module %s: terminating ircd", parv[1]);
		exit(0);
	}

	MyFree(m_bn);
	return 0;
}

/* list modules .. */
static int
mo_modlist(struct Client *client_p, struct Client *source_p, int parc, const char **parv)
{
	int i;

	if(!IsOperAdmin(source_p))
	{
		sendto_one(source_p, ":%s NOTICE %s :You have no A flag", me.name, parv[0]);
		return 0;
	}

	for (i = 0; i < num_mods; i++)
	{
		if(parc > 1)
		{
			if(match(parv[1], modlist[i]->name))
			{
				sendto_one(source_p, form_str(RPL_MODLIST),
					   me.name, source_p->name,
					   modlist[i]->name,
					   modlist[i]->address,
					   modlist[i]->version, 
					   modlist[i]->core ? "(core)" : "");
			}
		}
		else
		{
			sendto_one(source_p, form_str(RPL_MODLIST), 
				   me.name, source_p->name, modlist[i]->name,
				   modlist[i]->address, modlist[i]->version,
				   modlist[i]->core ? "(core)" : "");
		}
	}

	sendto_one(source_p, form_str(RPL_ENDOFMODLIST), me.name, source_p->name);
	return 0;
}

/* unload and reload all modules */
static int
mo_modrestart(struct Client *client_p, struct Client *source_p, int parc, const char **parv)
{
	int modnum;

	if(!IsOperAdmin(source_p))
	{
		sendto_one(source_p, ":%s NOTICE %s :You have no A flag", me.name, parv[0]);
		return 0;
	}

	sendto_one(source_p, ":%s NOTICE %s :Reloading all modules", me.name, parv[0]);

	modnum = num_mods;
	while (num_mods)
		unload_one_module(modlist[0]->name, 0);

	load_all_modules(0);
	load_core_modules(0);
	rehash(0);

	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "Module Restart: %d modules unloaded, %d modules loaded",
			     modnum, num_mods);
	ilog(L_WARN, "Module Restart: %d modules unloaded, %d modules loaded", modnum, num_mods);
	return 0;
}

#else /* STATIC_MODULES */

/* load_all_modules()
 *
 * input        -
 * output       -
 * side effects - all the msgtabs are added for static modules
 */
void
load_all_modules(int warn)
{
#ifdef __vms
	load_core_static_modules();
#endif
	load_static_modules();
}

#endif /* STATIC_MODULES */
