/*
 *  ircd-ratbox: A slightly useful ircd.
 *  modules.h: A header for the modules functions.
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

#ifndef INCLUDED_modules_h
#define INCLUDED_modules_h
#include "config.h"
#include "setup.h"
#include "parse.h"


#if defined(HAVE_SHL_LOAD)
#include <dl.h>
#endif
#if !defined(STATIC_MODULES) && defined(HAVE_DLFCN_H)
#include <dlfcn.h>
#endif

#include "ircd_handler.h"
#include "msg.h"
#include "memory.h"

#ifndef STATIC_MODULES
struct module
{
	char *name;
	const char *version;
	void *address;
	int core;
	int mapi_version;
	void * mapi_header; /* actually struct mapi_mheader_av<mapi_version>	*/
};

struct module_path
{
	char path[MAXPATHLEN];
};

#define MAPI_MAGIC_HDR	0x4D410000

#define MAPI_V1		(MAPI_MAGIC_HDR | 0x1)

#define MAPI_MAGIC(x)	((x) & 0xffff0000)
#define MAPI_VERSION(x)	((x) & 0x0000ffff)

typedef struct Message* mapi_clist_av1;

typedef struct
{
	const char *	hapi_name;
	int *		hapi_id;
} mapi_hlist_av1;

struct mapi_mheader_av1
{
	int		  mapi_version;				/* Module API version		*/
	int		(*mapi_register)	(void);		/* Register function;
								   ret -1 = failure (unload)	*/
	void		(*mapi_unregister)	(void);		/* Unregister function.		*/
	mapi_clist_av1	* mapi_command_list;			/* List of commands to add.	*/
	mapi_hlist_av1	* mapi_hook_list;			/* List of hooks to add.	*/
	const char *	  mapi_module_version;			/* Module's version (freeform)	*/
};

#ifndef STATIC_MODULES
# define DECLARE_MODULE_AV1(reg,unreg,cl,hl,v) \
	struct mapi_mheader_av1 _mheader = { MAPI_V1, reg, unreg, cl, hl, v}
#else
# define DECLARE_MODULE ERROR MAPI_NOT_AVAILABLE
#endif

#define _modinit ERROR DO_NOT_USE_MODINIT_WITH_NEW_MAPI_IT_WILL_NOT_WORK

/* add a path */
void mod_add_path(const char *path);
void mod_clear_paths(void);

/* load a module */
extern void load_module(char *path);

/* load all modules */
extern void load_all_modules(int warn);

/* load core modules */
extern void load_core_modules(int);

extern int unload_one_module(char *, int);
extern int load_one_module(char *, int);
extern int load_a_module(char *, int, int);
extern int findmodule_byname(char *);
extern char *irc_basename(char *);
extern void modules_init(void);

#else /* STATIC_MODULES */

extern struct Message accept_msgtab;
extern struct Message admin_msgtab;
extern struct Message away_msgtab;
extern struct Message capab_msgtab;
#ifdef HAVE_LIBCRYPTO
extern struct Message challenge_msgtab;
extern struct Message cryptlink_msgtab;
#endif
extern struct Message client_msgtab;
extern struct Message close_msgtab;
extern struct Message connect_msgtab;
extern struct Message die_msgtab;
extern struct Message dmem_msgtab;
extern struct Message eob_msgtab;
extern struct Message error_msgtab;
extern struct Message gline_msgtab;
#ifdef FL_DEBUG
extern struct Message hash_msgtab;
#endif
extern struct Message htm_msgtab;
extern struct Message help_msgtab;
extern struct Message info_msgtab;
extern struct Message invite_msgtab;
extern struct Message ison_msgtab;
extern struct Message join_msgtab;
extern struct Message kick_msgtab;
extern struct Message kill_msgtab;
extern struct Message kline_msgtab;
extern struct Message dline_msgtab;
extern struct Message xline_msgtab;
extern struct Message knock_msgtab;
extern struct Message links_msgtab;
extern struct Message list_msgtab;
extern struct Message locops_msgtab;
extern struct Message lusers_msgtab;
extern struct Message privmsg_msgtab;
extern struct Message notice_msgtab;
extern struct Message mode_msgtab;
extern struct Message motd_msgtab;
extern struct Message names_msgtab;
extern struct Message nick_msgtab;
extern struct Message oper_msgtab;
extern struct Message operwall_msgtab;
extern struct Message part_msgtab;
extern struct Message pass_msgtab;
extern struct Message ping_msgtab;
extern struct Message pong_msgtab;
extern struct Message post_msgtab;
extern struct Message quit_msgtab;
extern struct Message rehash_msgtab;
extern struct Message restart_msgtab;
extern struct Message resv_msgtab;
extern struct Message server_msgtab;
extern struct Message set_msgtab;
extern struct Message sjoin_msgtab;
extern struct Message squit_msgtab;
extern struct Message stats_msgtab;
extern struct Message svinfo_msgtab;
extern struct Message testline_msgtab;
extern struct Message time_msgtab;
extern struct Message topic_msgtab;
extern struct Message trace_msgtab;
extern struct Message msgtabs[];
extern struct Message unresv_msgtab;
extern struct Message user_msgtab;
extern struct Message userhost_msgtab;
extern struct Message users_msgtab;
extern struct Message version_msgtab;
extern struct Message wallops_msgtab;
extern struct Message who_msgtab;
extern struct Message whois_msgtab;
extern struct Message whowas_msgtab;
extern struct Message get_msgtab;
extern struct Message put_msgtab;
extern void load_all_modules(int check);

#endif /* STATIC_MODULES */
#endif /* INCLUDED_modules_h */
