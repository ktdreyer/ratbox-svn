/* $Id$ */
#ifndef INCLUDED_CACHE_H
#define INCLUDED_CACHE_H

#include "tools.h"

#define HELP_MAX	100

#define CACHELINELEN	81
#define CACHEFILELEN	30

extern const char emptyline[];

#define HELP_USER	0x001
#define HELP_OPER	0x002

struct cachefile
{
	char name[CACHEFILELEN];
	dlink_list contents;
	int flags;
};

struct cacheline
{
	char data[CACHELINELEN];
	dlink_node linenode;
};

extern void init_cache(void);
extern struct cachefile *cache_file(const char *, const char *, int);
extern void load_help(void);
extern void free_cachefile(struct cachefile *);

#endif

