/* src/cache.c
 *   Contains code for caching files
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "tools.h"
#include "balloc.h"
#include "cache.h"
#include "io.h"

static BlockHeap *cachefile_heap = NULL;
static BlockHeap *cacheline_heap = NULL;

struct cacheline *emptyline = NULL;

/* init_cache()
 *
 * inputs	-
 * outputs	-
 * side effects - inits the file/line cache blockheaps, loads motds
 */
void
init_cache(void)
{
	cachefile_heap = BlockHeapCreate(sizeof(struct cachefile), HEAP_CACHEFILE);
	cacheline_heap = BlockHeapCreate(sizeof(struct cacheline), HEAP_CACHELINE);

	/* allocate the emptyline */
	emptyline = BlockHeapAlloc(cacheline_heap);
	emptyline->data[0] = ' ';
}

/* cache_file()
 *
 * inputs	- file to cache, files "shortname", flags to set
 * outputs	- pointer to file cached, else NULL
 * side effects -
 */
struct cachefile *
cache_file(const char *filename, const char *shortname)
{
	FILE *in;
	struct cachefile *cacheptr;
	struct cacheline *lineptr;
	char line[BUFSIZE];
	char *p;

	if((in = fopen(filename, "r")) == NULL)
		return NULL;

	cacheptr = BlockHeapAlloc(cachefile_heap);
	strlcpy(cacheptr->name, shortname, sizeof(cacheptr->name));

	/* cache the file... */
	while(fgets(line, sizeof(line), in) != NULL)
	{
		if((p = strchr(line, '\n')) != NULL)
			*p = '\0';

		if(!EmptyString(line))
		{
			lineptr = BlockHeapAlloc(cacheline_heap);

			strlcpy(lineptr->data, line, sizeof(lineptr->data));
			dlink_add_tail(lineptr, &lineptr->linenode, &cacheptr->contents);
		}
		else
			dlink_add_tail_alloc(emptyline, &cacheptr->contents);
	}

	fclose(in);
	return cacheptr;
}

/* free_cachefile()
 *
 * inputs	- cachefile to free
 * outputs	-
 * side effects - cachefile and its data is free'd
 */
void
free_cachefile(struct cachefile *cacheptr)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(cacheptr == NULL)
		return;

	DLINK_FOREACH_SAFE(ptr, next_ptr, cacheptr->contents.head)
	{
		if(ptr->data != emptyline)
			BlockHeapFree(cacheline_heap, ptr->data);
	}

	BlockHeapFree(cachefile_heap, cacheptr);
}

void
send_cachefile(struct cachefile *cacheptr, struct lconn *conn_p)
{
        struct cacheline *lineptr;
        dlink_node *ptr;

        if(cacheptr == NULL || conn_p == NULL)
                return;

        DLINK_FOREACH(ptr, cacheptr->contents.head)
        {
                lineptr = ptr->data;
                sendto_one(conn_p, "%s", lineptr->data);
        }
}
