/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  modules.c: A module loader.
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
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

#include "config.h"

#if !defined(STATIC_MODULES) && !defined(HAVE_MACH_O_DYLD_H) && defined(HAVE_DLOPEN)
#include <dlfcn.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#ifdef VMS
# define _XOPEN_SOURCE
#endif

#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

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
#include "list.h"



#ifndef STATIC_MODULES

#ifndef RTLD_NOW
#define RTLD_NOW RTLD_LAZY /* openbsd deficiency */
#ifndef RTLD_LAZY
#define RTLD_NOW 2185 /* built-in dl*(3) don't care */
#endif
#endif

static char unknown_ver[] = "<unknown>";

struct module **modlist = NULL;

static char *core_module_table[] =
{
  "m_die.so",
  "m_kick.so",
  "m_kill.so",
  "m_message.so",
  "m_mode.so",
  "m_nick.so",
  "m_part.so",
  "m_quit.so",
  "m_server.so",
  "m_sjoin.so",
  "m_squit.so",
  NULL
};

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

#ifdef HAVE_MACH_O_DYLD_H
/*
** jmallett's dl*(3) shims for NSModule(3) systems.
*/
#include <mach-o/dyld.h>

#ifndef HAVE_DLOPEN
void undefinedErrorHandler(const char *);
NSModule multipleErrorHandler(NSSymbol, NSModule, NSModule);
void linkEditErrorHandler(NSLinkEditErrors, int,const char *, const char *);
char *dlerror(void);
void *dlopen(char *, int);
int dlclose(void *);
void *dlsym(void *, char *);

static int firstLoad = TRUE;
static int myDlError;
static char *myErrorTable[] =
{ "Loading file as object failed\n",
  "Loading file as object succeeded\n",
  "Not a valid shared object\n",
  "Architecture of object invalid on this architecture\n",
  "Invalid or corrupt image\n",
  "Could not access object\n",
  "NSCreateObjectFileImageFromFile failed\n",
  NULL
};

void undefinedErrorHandler(const char *symbolName)
{
  sendto_realops_flags(FLAGS_ALL, L_ALL, "Undefined symbol: %s", symbolName);
  ilog(L_WARN, "Undefined symbol: %s", symbolName);
  return;
}

NSModule multipleErrorHandler(NSSymbol s, NSModule old, NSModule new)
{
  /* XXX
  ** This results in substantial leaking of memory... Should free one
  ** module, maybe?
  */
  sendto_realops_flags(FLAGS_ALL, L_ALL, "Symbol `%s' found in `%s' and `%s'",
                       NSNameOfSymbol(s), NSNameOfMdoule(old), NSNameOfMdoule(new));
  ilog(L_WARN, "Symbol `%s' found in `%s' and `%s'", NSNameOfSymbol(s),
       NSNameOfMdoule(old), NSNameOfMdoule(new));
  /* We return which module should be considered valid, I believe */
  return new;
}

void linkEditErrorHandler(NSLinkEditErrors errorClass, int errnum,
                          const char *fileName, const char *errorString)
{
  sendto_realops_flags(FLAGS_ALL, L_ALL, "Link editor error: %s for %s",
                       errorString, fileName);
  ilog(L_WARN, "Link editor error: %s for %s", errorString, fileName);
  return;
}

char *dlerror(void)
{
  return myErrorTable[myDlError % 6];
}

void *dlopen(char *filename, int unused)
{
  NSObjectFileImage myImage;
  NSModule myModule;

  if (firstLoad)
    {
      /*
      ** If we are loading our first symbol (huzzah!) we should go ahead
      ** and install link editor error handling!
      */
      NSLinkEditErrorHandlers linkEditorErrorHandlers;

      linkEditorErrorHandlers.undefined = undefinedErrorHandler;
      linkEditorErrorHandlers.multiple = multipleErrorHandler;
      linkEditorErrorHandlers.linkEdit = linkEditErrorHandler;
      NSInstallLinkEditErrorHandlers(&linkEditorErrorHandlers);
      firstLoad = FALSE;
    }
  myDlError = NSCreateObjectFileImageFromFile(filename, &myImage);
  if (myDlError != NSObjectFileImageSuccess)
    {
      return NULL;
    }
  myModule = NSLinkModule(myImage, filename, NSLINKMODULE_OPTION_PRIVATE);
  return (void *)myModule;
}

int dlclose(void *myModule)
{
  NSUnLinkModule(myModule, FALSE);
  return 0;
}

void *dlsym(void *myModule, char *mySymbolName)
{
  NSSymbol mySymbol;

  mySymbol = NSLookupSymbolInModule((NSModule)myModule, mySymbolName);
  return NSAddressOfSymbol(mySymbol);
}
#endif
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
}

/* mod_find_path()
 *
 * input	- path
 * output	- none
 * side effects - returns a module path from path
 */
static struct module_path *
mod_find_path(char *path)
{
  dlink_node *pathst = mod_paths.head;
  struct module_path *mpath;
  
  if (!pathst)
    return NULL;

  for (; pathst; pathst = pathst->next)
  {
    mpath = (struct module_path *)pathst->data;

    if (!strcmp(path, mpath->path))
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

/* mod_clear_paths()
 *
 * input	-
 * output	-
 * side effects - clear the lists of paths
 */
void
mod_clear_paths(void)
{
  struct module_path *pathst;
  dlink_node *node, *next;

  for(node = mod_paths.head; node; node = next)
    {
      next = node->next;
      pathst = (struct module_path *)node->data;
      dlinkDelete(node, &mod_paths);
      free_dlink_node(node);
      MyFree(pathst);
    }
}

/* irc_basename
 *
 * input	-
 * output	-
 * side effects -
 */
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

/* findmodule_byname
 *
 * input        -
 * output       -
 * side effects -
 */

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

/* unload_one_module()
 *
 * inputs	- name of module to unload
 *		- 1 to say modules unloaded, 0 to not
 * output	- 0 if successful, -1 if error
 * side effects	- module is unloaded
 */
int unload_one_module (char *name, int warn)
{
  int modindex;
  void (*deinitfunc)(void) = NULL;

  if ((modindex = findmodule_byname (name)) == -1) 
    return -1;

  if( (deinitfunc = (void (*)(void))(uintptr_t)dlsym (modlist[modindex]->address, 
				  "_moddeinit")) 
		  || (deinitfunc = (void (*)(void))(uintptr_t)dlsym (modlist[modindex]->address, 
				  "__moddeinit")))
  {
    deinitfunc ();
  }

  (void)dlclose(modlist[modindex]->address);

  MyFree(modlist[modindex]->name);
  memcpy( &modlist[modindex], &modlist[modindex+1],
          sizeof(struct module) * ((num_mods-1) - modindex) );

  if(num_mods != 0)
    num_mods--;

  if(warn == 1)
    {
      ilog (L_INFO, "Module %s unloaded", name);
      sendto_realops_flags(FLAGS_ALL, L_ALL,"Module %s unloaded", name);
    }

  return 0;
}

/* load_all_modules()
 *
 * input        -
 * output       -
 * side effects -
 */
void
load_all_modules (int warn)
{
  DIR            *system_module_dir = NULL;
  struct dirent  *ldirent = NULL;
  char            module_fq_name[PATH_MAX + 1];
  int             len;

  modules_init();
  
  modlist = (struct module **)MyMalloc ( sizeof (struct module) *
                                         (MODS_INCREMENT));

  max_mods = MODS_INCREMENT;

  system_module_dir = opendir (AUTOMODPATH);

  if (system_module_dir == NULL)
    {
      ilog (L_WARN, "Could not load modules from %s: %s",
	   AUTOMODPATH, strerror (errno));
      return;
    }

  while ((ldirent = readdir (system_module_dir)) != NULL)
    {
      len = strlen(ldirent->d_name);
      
      if ((len > 3) && 
          (ldirent->d_name[len-3] == '.') &&
          (ldirent->d_name[len-2] == 's') &&
          (ldirent->d_name[len-1] == 'o'))
	{
	  (void)sprintf (module_fq_name, "%s/%s",  AUTOMODPATH,
			 ldirent->d_name);
	  (void)load_a_module (module_fq_name, warn, 0);
	}
    }

  (void)closedir (system_module_dir);
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
  int i;

  for(i = 0; core_module_table[i]; i++)
  {
    sprintf(module_name, "%s/%s",
            MODPATH, core_module_table[i]);
	    
    if(load_a_module(module_name, warn, 1) == -1)
    {
      ilog(L_CRIT, "Error loading core module %s: terminating ircd", 
           core_module_table[i]);
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
load_one_module (char *path)
{
  char modpath[MAXPATHLEN];
  dlink_node *pathst;
  struct module_path *mpath;
	
  struct stat statbuf;

  if (strchr(path, '/')) /* absolute path, try it */
    return load_a_module(path, 1, 0);

  for (pathst = mod_paths.head; pathst; pathst = pathst->next)
    {
      mpath = (struct module_path *)pathst->data;
      
      sprintf(modpath, "%s/%s", mpath->path, path);
      if (stat(modpath, &statbuf) == 0)
	return load_a_module(modpath, 1, 0);
    }
	
  sendto_realops_flags(FLAGS_ALL, L_ALL,
                       "Cannot locate module %s", path);
  ilog(L_WARN, "Cannot locate module %s", path);
  return -1;
}
		

/*
 * load_a_module()
 *
 * inputs	- path name of module, int to notice, int of core
 * output	- -1 if error 0 if success
 * side effects - loads a module if successful
 */
int
load_a_module (char *path, int warn, int core)
{
  void *tmpptr = NULL;
  char *mod_basename;
  void (*initfunc)(void) = NULL;
  char **verp;
  char *ver;

  mod_basename = irc_basename(path);

  tmpptr = dlopen (path, RTLD_NOW);

  if (tmpptr == NULL)
  {
      const char *err = dlerror();
	  
      sendto_realops_flags(FLAGS_ALL, L_ALL,
                            "Error loading module %s: %s",
                            mod_basename, err);
      ilog (L_WARN, "Error loading module %s: %s", mod_basename, err);
      MyFree (mod_basename);
      return -1;
  }

  initfunc = (void (*)(void))(uintptr_t)dlsym (tmpptr, "_modinit");
  if (initfunc == NULL 
		  && (initfunc = (void (*)(void))(uintptr_t)dlsym(tmpptr, "__modinit")) == NULL)
  {
    sendto_realops_flags(FLAGS_ALL, L_ALL,
                          "Module %s has no _modinit() function",
                          mod_basename);
    ilog (L_WARN, "Module %s has no _modinit() function", mod_basename);
    (void)dlclose (tmpptr);
    MyFree (mod_basename);
    return -1;
  }

  verp = (char **)dlsym (tmpptr, "_version");
  if (verp == NULL 
		  && (verp = (char **)dlsym (tmpptr, "__version")) == NULL)
    ver = unknown_ver;
  else
    ver = *verp;

  increase_modlist();

  modlist [num_mods] = MyMalloc (sizeof (struct module));
  modlist [num_mods]->address = tmpptr;
  modlist [num_mods]->version = ver;
  modlist[num_mods]->core = core;
  DupString(modlist [num_mods]->name, mod_basename );
  num_mods++;

  initfunc ();

  if(warn == 1)
    {
      sendto_realops_flags(FLAGS_ALL, L_ALL,
                        "Module %s [version: %s] loaded at 0x%lx",
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
  int modindex;

  if (!IsOperAdmin (source_p))
  {
    sendto_one (source_p, ":%s NOTICE %s :You have no A flag",
                me.name, parv[0]);
    return;
  }

  m_bn = irc_basename (parv[1]);

  if((modindex = findmodule_byname (m_bn)) == -1)
  {
    sendto_one (source_p, ":%s NOTICE %s :Module %s is not loaded",
                me.name, source_p->name, m_bn);
    MyFree (m_bn);
    return;
  }

  if(modlist[modindex]->core == 1)
  {
    sendto_one(source_p, 
               ":%s NOTICE %s :Module %s is a core module and may not be unloaded",
	       me.name, source_p->name, m_bn);
    MyFree(m_bn);
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
  int modindex;
  int check_core;

  if (!IsOperAdmin (source_p))
    {
      sendto_one (source_p, ":%s NOTICE %s :You have no A flag",
                  me.name, parv[0]);
      return;
    }

  m_bn = irc_basename (parv[1]);

  if((modindex = findmodule_byname(m_bn)) == -1)
    {
      sendto_one (source_p, ":%s NOTICE %s :Module %s is not loaded",
                  me.name, source_p->name, m_bn);
      MyFree (m_bn);
      return;
    }

  check_core = modlist[modindex]->core;

  if( unload_one_module (m_bn, 1) == -1 )
    {
      sendto_one (source_p, ":%s NOTICE %s :Module %s is not loaded",
                  me.name, source_p->name, m_bn);
      MyFree (m_bn);
      return;
    }

  if((load_one_module(parv[1]) == -1) && check_core)
  {
    sendto_realops_flags(FLAGS_ALL, L_ALL,
                         "Error reloading core module: %s: terminating ircd",
			 parv[1]);
    ilog(L_CRIT, "Error loading core module %s: terminating ircd", parv[1]);
    exit(0);
  }

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
    if(parc>1)
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
  load_core_modules(0);
  rehash(0);
  
  sendto_realops_flags(FLAGS_ALL, L_ALL,
              "Module Restart: %d modules unloaded, %d modules loaded",
			modnum, num_mods);
  ilog(L_WARN, "Module Restart: %d modules unloaded, %d modules loaded",
      modnum, num_mods);
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
	mod_add_cmd(&error_msgtab);
	mod_add_cmd(&accept_msgtab);
	mod_add_cmd(&admin_msgtab);
	mod_add_cmd(&away_msgtab);
	mod_add_cmd(&capab_msgtab);
	mod_add_cmd(&cburst_msgtab);
	mod_add_cmd(&cjoin_msgtab);
	mod_add_cmd(&client_msgtab);
	mod_add_cmd(&close_msgtab);
	mod_add_cmd(&connect_msgtab);
#ifdef HAVE_LIBCRYPTO
	mod_add_cmd(&challenge_msgtab);
        mod_add_cmd(&cryptlink_msgtab);
#endif
        mod_add_cmd(&die_msgtab);
	mod_add_cmd(&dmem_msgtab);
	mod_add_cmd(&drop_msgtab);
	mod_add_cmd(&eob_msgtab);
	mod_add_cmd(&gline_msgtab);
	mod_add_cmd(&help_msgtab);
	mod_add_cmd(&info_msgtab);
	mod_add_cmd(&invite_msgtab);
	mod_add_cmd(&ison_msgtab);
	mod_add_cmd(&join_msgtab);
	mod_add_cmd(&kick_msgtab);
	mod_add_cmd(&kill_msgtab);
	mod_add_cmd(&kline_msgtab);
	mod_add_cmd(&dline_msgtab);
	mod_add_cmd(&knock_msgtab);
	mod_add_cmd(&knockll_msgtab);
	mod_add_cmd(&links_msgtab);
	mod_add_cmd(&list_msgtab);
	mod_add_cmd(&lljoin_msgtab);
	mod_add_cmd(&llnick_msgtab);
	mod_add_cmd(&locops_msgtab);
	mod_add_cmd(&lusers_msgtab);
	mod_add_cmd(&privmsg_msgtab);
	mod_add_cmd(&notice_msgtab);
	mod_add_cmd(&mode_msgtab);
	mod_add_cmd(&motd_msgtab);
	mod_add_cmd(&names_msgtab);
	mod_add_cmd(&nburst_msgtab);
	mod_add_cmd(&nick_msgtab);
	mod_add_cmd(&oper_msgtab);
	mod_add_cmd(&operwall_msgtab);
	mod_add_cmd(&part_msgtab);
	mod_add_cmd(&pass_msgtab);
	mod_add_cmd(&ping_msgtab);
	mod_add_cmd(&pong_msgtab);
	mod_add_cmd(&post_msgtab);
	mod_add_cmd(&quit_msgtab);
	mod_add_cmd(&rehash_msgtab);
	mod_add_cmd(&restart_msgtab);
	mod_add_cmd(&resv_msgtab);  
	mod_add_cmd(&server_msgtab);
	mod_add_cmd(&set_msgtab);
	mod_add_cmd(&sjoin_msgtab);
	mod_add_cmd(&squit_msgtab);
	mod_add_cmd(&stats_msgtab);
	mod_add_cmd(&svinfo_msgtab);
	mod_add_cmd(&testline_msgtab);
	mod_add_cmd(&time_msgtab);
	mod_add_cmd(&topic_msgtab);
	mod_add_cmd(&trace_msgtab);
	mod_add_cmd(&msgtabs[0]);
	mod_add_cmd(&msgtabs[1]);
	mod_add_cmd(&msgtabs[2]);
	mod_add_cmd(&unresv_msgtab);
	mod_add_cmd(&user_msgtab);
	mod_add_cmd(&userhost_msgtab);
	mod_add_cmd(&users_msgtab);
	mod_add_cmd(&version_msgtab);
	mod_add_cmd(&wallops_msgtab);
	mod_add_cmd(&who_msgtab);
	mod_add_cmd(&whois_msgtab);
	mod_add_cmd(&whowas_msgtab);
}

#endif /* STATIC_MODULES */
