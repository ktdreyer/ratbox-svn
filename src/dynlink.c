/*
 *  ircd-ratbox: A slightly useful ircd.
 *  dynlink.c: A module loader.
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
 * $Id$
 *
 */
#include "stdinc.h"
#include "config.h"

#include "modules.h"
#include "s_log.h"
#include "client.h"
#include "send.h"
#include "hook.h"

#ifndef STATIC_MODULES

#ifndef RTLD_NOW
#define RTLD_NOW RTLD_LAZY	/* openbsd deficiency */
#endif

extern struct module **modlist;
extern int num_mods;
extern int max_mods;

static void increase_modlist(void);

#define MODS_INCREMENT 10

static char unknown_ver[] = "<unknown>";

/* This file contains the core functions to use dynamic libraries.
 * -TimeMr14C
 */


#ifdef HAVE_MACH_O_DYLD_H
/*
** jmallett's dl*(3) shims for NSModule(3) systems.
*/
#include <mach-o/dyld.h>

#ifndef HAVE_DLOPEN
#ifndef	RTLD_LAZY
#define RTLD_LAZY 2185		/* built-in dl*(3) don't care */
#endif

void undefinedErrorHandler(const char *);
NSModule multipleErrorHandler(NSSymbol, NSModule, NSModule);
void linkEditErrorHandler(NSLinkEditErrors, int, const char *, const char *);
char *dlerror(void);
void *dlopen(char *, int);
int dlclose(void *);
void *dlsym(void *, char *);

static int firstLoad = TRUE;
static int myDlError;
static char *myErrorTable[] = { "Loading file as object failed\n",
	"Loading file as object succeeded\n",
	"Not a valid shared object\n",
	"Architecture of object invalid on this architecture\n",
	"Invalid or corrupt image\n",
	"Could not access object\n",
	"NSCreateObjectFileImageFromFile failed\n",
	NULL
};

void
undefinedErrorHandler(const char *symbolName)
{
	sendto_realops_flags(UMODE_ALL, L_ALL, "Undefined symbol: %s", symbolName);
	ilog(L_WARN, "Undefined symbol: %s", symbolName);
	return;
}

NSModule
multipleErrorHandler(NSSymbol s, NSModule old, NSModule new)
{
	/* XXX
	 ** This results in substantial leaking of memory... Should free one
	 ** module, maybe?
	 */
	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "Symbol `%s' found in `%s' and `%s'",
			     NSNameOfSymbol(s), NSNameOfModule(old), NSNameOfModule(new));
	ilog(L_WARN, "Symbol `%s' found in `%s' and `%s'",
	     NSNameOfSymbol(s), NSNameOfModule(old), NSNameOfModule(new));
	/* We return which module should be considered valid, I believe */
	return new;
}

void
linkEditErrorHandler(NSLinkEditErrors errorClass, int errnum,
		     const char *fileName, const char *errorString)
{
	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "Link editor error: %s for %s", errorString, fileName);
	ilog(L_WARN, "Link editor error: %s for %s", errorString, fileName);
	return;
}

char *
dlerror(void)
{
	return myDlError == NSObjectFileImageSuccess ? NULL : myErrorTable[myDlError % 7];
}

void *
dlopen(char *filename, int unused)
{
	NSObjectFileImage myImage;
	NSModule myModule;

	if(firstLoad)
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
	if(myDlError != NSObjectFileImageSuccess)
	{
		return NULL;
	}
	myModule = NSLinkModule(myImage, filename, NSLINKMODULE_OPTION_PRIVATE);
	return (void *) myModule;
}

int
dlclose(void *myModule)
{
	NSUnLinkModule(myModule, FALSE);
	return 0;
}

void *
dlsym(void *myModule, char *mySymbolName)
{
	NSSymbol mySymbol;

	mySymbol = NSLookupSymbolInModule((NSModule) myModule, mySymbolName);
	return NSAddressOfSymbol(mySymbol);
}
#endif
#endif


/*
 * HPUX dl compat functions
 */
#if defined(HAVE_SHL_LOAD) && !defined(HAVE_DLOPEN)
#define RTLD_LAZY BIND_DEFERRED
#define RTLD_GLOBAL DYNAMIC_PATH
#define dlopen(file,mode) (void *)shl_load((file), (mode), (long) 0)
#define dlclose(handle) shl_unload((shl_t)(handle))
#define dlsym(handle,name) hpux_dlsym(handle,name)
#define dlerror() strerror(errno)

static void *
hpux_dlsym(void *handle, char *name)
{
	void *sym_addr;
	if(!shl_findsym((shl_t *) & handle, name, TYPE_UNDEFINED, &sym_addr))
		return sym_addr;
	return NULL;
}

#endif

/* unload_one_module()
 *
 * inputs	- name of module to unload
 *		- 1 to say modules unloaded, 0 to not
 * output	- 0 if successful, -1 if error
 * side effects	- module is unloaded
 */
int
unload_one_module(const char *name, int warn)
{
	int modindex;

	if((modindex = findmodule_byname(name)) == -1)
		return -1;

	/*
	 ** XXX - The type system in C does not allow direct conversion between
	 ** data and function pointers, but as it happens, most C compilers will
	 ** safely do this, however it is a theoretical overlow to cast as we 
	 ** must do here.  I have library functions to take care of this, but 
	 ** despite being more "correct" for the C language, this is more 
	 ** practical.  Removing the abuse of the ability to cast ANY pointer
	 ** to and from an integer value here will break some compilers.
	 **          -jmallett
	 */
	/* Left the comment in but the code isn't here any more		-larne */
	switch (modlist[modindex]->mapi_version)
	{
	case 1:
	{
		struct mapi_mheader_av1* mheader = modlist[modindex]->mapi_header;
		if (mheader->mapi_command_list)
		{
			struct Message **m;
			for (m = mheader->mapi_command_list; *m; ++m)
				mod_del_cmd(*m);
		}

		if(mheader->mapi_encap_list)
		{
			struct encap **m;
			for(m = mheader->mapi_encap_list; *m; ++m)
				del_encap(*m);
		}

		if (mheader->mapi_unregister)
			mheader->mapi_unregister();
		break;
	}
	default:
		sendto_realops_flags(UMODE_ALL, L_ALL,
			"Unknown/unsupported MAPI version %d when unloading %s!",
			modlist[modindex]->mapi_version, modlist[modindex]->name);
		ilog(L_CRIT, "Unknown/unsupported MAPI version %d when unloading %s!",
			modlist[modindex]->mapi_version, modlist[modindex]->name);
		break;
	}

	dlclose(modlist[modindex]->address);

	MyFree(modlist[modindex]->name);
	memcpy(&modlist[modindex], &modlist[modindex + 1],
	       sizeof(struct module) * ((num_mods - 1) - modindex));

	if(num_mods != 0)
		num_mods--;

	if(warn == 1)
	{
		ilog(L_INFO, "Module %s unloaded", name);
		sendto_realops_flags(UMODE_ALL, L_ALL, "Module %s unloaded", name);
	}

	return 0;
}


/*
 * load_a_module()
 *
 * inputs	- path name of module, int to notice, int of core
 * output	- -1 if error 0 if success
 * side effects - loads a module if successful
 */
int
load_a_module(const char *path, int warn, int core)
{
	void *tmpptr = NULL;

	char *mod_basename;
	const char *ver;

	int *mapi_version;

	mod_basename = irc_basename(path);

	tmpptr = dlopen(path, RTLD_NOW);

	if(tmpptr == NULL)
	{
		const char *err = dlerror();

		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Error loading module %s: %s", mod_basename, err);
		ilog(L_WARN, "Error loading module %s: %s", mod_basename, err);
		MyFree(mod_basename);
		return -1;
	}

	
	/*
	 * _mheader is actually a struct mapi_mheader_*, but mapi_version
	 * is always the first member of this structure, so we treate it
	 * as a single int in order to determine the API version.
	 *	-larne.
	 */
	mapi_version = (int*) (uintptr_t) dlsym(tmpptr, "_mheader");
	if((mapi_version == NULL
	   && (mapi_version = (int*) (uintptr_t) dlsym(tmpptr, "__mheader")) == NULL)
	  || MAPI_MAGIC(*mapi_version) != MAPI_MAGIC_HDR)
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Data format error: module %s has no MAPI header.", mod_basename);
		ilog(L_WARN, "Data format error: module %s has no MAPI header.", mod_basename);
		(void) dlclose(tmpptr);
		MyFree(mod_basename);
		return -1;
	}

	switch (MAPI_VERSION(*mapi_version))
	{
	case 1:
	{
		struct mapi_mheader_av1* mheader = (struct mapi_mheader_av1*) mapi_version;	/* see above */
		if (mheader->mapi_register && (mheader->mapi_register() == -1))
		{
			ilog(L_WARN, "Module %s indicated failure during load.", mod_basename);
			sendto_realops_flags(UMODE_ALL, L_ALL,
				"Module %s indicated failure during load.", mod_basename);
			dlclose(tmpptr);
			MyFree(mod_basename);
			return -1;
		}
		if (mheader->mapi_command_list)
		{
			struct Message **m;
			for (m = mheader->mapi_command_list; *m; ++m)
				mod_add_cmd(*m);
		}

		if(mheader->mapi_encap_list)
		{
			struct encap **m;
			for(m = mheader->mapi_encap_list; *m; ++m)
				add_encap(*m);
		}

		if (mheader->mapi_hook_list)
		{
			mapi_hlist_av1 *m;
			for (m = mheader->mapi_hook_list; m->hapi_name; ++m)
				hook_add_event(m->hapi_name, m->hapi_id);
		}
		
		if (mheader->mapi_hfn_list)
		{
			mapi_hfn_list_av1 *m;
			for (m = mheader->mapi_hfn_list; m->hapi_name; ++m)
				hook_add_hook(m->hapi_name, m->fn);
		}
		
		ver = mheader->mapi_module_version;
		break;
	}
	
	default:
		ilog(L_WARN, "Module %s has unknown/unsupported MAPI version %d.",
			mod_basename, MAPI_VERSION(*mapi_version));
		sendto_realops_flags(UMODE_ALL, L_ALL,
			"Module %s has unknown/unsupported MAPI version %d.",
			mod_basename, *mapi_version);
		dlclose(tmpptr);
		MyFree(mod_basename);
		return -1;
	}

	if(ver == NULL)
		ver = unknown_ver;

	increase_modlist();

	modlist[num_mods] = MyMalloc(sizeof(struct module));
	modlist[num_mods]->address = tmpptr;
	modlist[num_mods]->version = ver;
	modlist[num_mods]->core = core;
	DupString(modlist[num_mods]->name, mod_basename);
	modlist[num_mods]->mapi_header = mapi_version;
	num_mods++;

	if(warn == 1)
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Module %s [version: %s; MAPI version: %d] loaded at 0x%lx",
				     mod_basename, ver, MAPI_VERSION(*mapi_version), (unsigned long) tmpptr);
		ilog(L_WARN, "Module %s [version: %s; MAPI version: %d] loaded at 0x%lx", 
			mod_basename, ver, MAPI_VERSION(*mapi_version), 
			(unsigned long) tmpptr);
	}
	MyFree(mod_basename);
	return 0;
}

/*
 * increase_modlist
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	- expand the size of modlist if necessary
 */
static void
increase_modlist(void)
{
	struct module **new_modlist = NULL;

	if((num_mods + 1) < max_mods)
		return;

	new_modlist = (struct module **) MyMalloc(sizeof(struct module) *
						  (max_mods + MODS_INCREMENT));
	memcpy((void *) new_modlist, (void *) modlist, sizeof(struct module) * num_mods);

	MyFree(modlist);
	modlist = new_modlist;
	max_mods += MODS_INCREMENT;
}

/*
 * find_a_symbol
 */
#endif /* STATIC_MODULES */
