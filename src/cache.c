/*
 * ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 * cache.c - code for caching files
 *
 * Copyright (C) 2003 Lee Hardy <lee@leeh.co.uk>
 * Copyright (C) 2003 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 */

#include "stdinc.h"
#include "ircd_defs.h"
#include "common.h"
#include "s_conf.h"
#include "tools.h"
#include "client.h"
#include "memory.h"
#include "balloc.h"
#include "event.h"
#include "hash.h"
#include "cache.h"

static BlockHeap *cachefile_heap = NULL;
static BlockHeap *cacheline_heap = NULL;

const char emptyline[] = " ";

/* init_cache()
 *
 * inputs	-
 * outputs	-
 * side effects - inits the file/line cache blockheaps
 */
void
init_cache(void)
{
	cachefile_heap = BlockHeapCreate(sizeof(struct cachefile), CACHEFILE_HEAP_SIZE);
	cacheline_heap = BlockHeapCreate(sizeof(struct cacheline), CACHELINE_HEAP_SIZE);
}

/* cache_file()
 *
 * inputs	- file to cache, files "shortname", flags to set
 * outputs	- pointer to file cached, else NULL
 * side effects -
 */
struct cachefile *
cache_file(const char *filename, const char *shortname, int flags)
{
	FBFILE *in;
	struct cachefile *cacheptr;
	struct cacheline *lineptr;
	char line[BUFSIZE];
	char *p;

	if((in = fbopen(filename, "r")) == NULL)
		return NULL;

	cacheptr = BlockHeapAlloc(cachefile_heap);
	memset(cacheptr, 0, sizeof(struct cachefile));

	strlcpy(cacheptr->name, shortname, sizeof(cacheptr->name));
	cacheptr->flags = flags;

	/* cache the file... */
	while(fbgets(line, sizeof(line), in) != NULL)
	{
		if((p = strchr(line, '\n')) != NULL)
			*p = '\0';

		if(!EmptyString(line))
		{
			lineptr = BlockHeapAlloc(cacheline_heap);
			strlcpy(lineptr->data, line, sizeof(lineptr->data));
			dlinkAddTail(lineptr, &lineptr->linenode, &cacheptr->contents);
		}
		else
			dlinkAddTailAlloc((char *) emptyline, &cacheptr->contents);
	}

	fbclose(in);
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

	DLINK_FOREACH_SAFE(ptr, next_ptr, cacheptr->contents.head)
	{
		if(ptr->data != emptyline)
			BlockHeapFree(cacheline_heap, ptr->data);
	}

	BlockHeapFree(cachefile_heap, cacheptr);
}

/* load_help()
 *
 * inputs	-
 * outputs	-
 * side effects - contents of help directories are loaded.
 */
void
load_help(void)
{
	DIR *helpfile_dir = NULL;
	struct dirent *ldirent= NULL;
	char filename[MAXPATHLEN];
	struct cachefile *cacheptr;

#if defined(S_ISLNK) && defined(HAVE_LSTAT)
	struct stat sb;
#endif

	/* opers must be done first */
	helpfile_dir = opendir(HPATH);

	if(helpfile_dir == NULL)
		return;

	while((ldirent = readdir(helpfile_dir)) != NULL)
	{
		snprintf(filename, sizeof(filename), "%s/%s", HPATH, ldirent->d_name);
		cacheptr = cache_file(filename, ldirent->d_name, HELP_OPER);
		add_to_help_hash(cacheptr->name, cacheptr);
	}

	helpfile_dir = opendir(UHPATH);

	if(helpfile_dir == NULL)
		return;

	while((ldirent = readdir(helpfile_dir)) != NULL)
	{
		snprintf(filename, sizeof(filename), "%s/%s", UHPATH, ldirent->d_name);

#if defined(S_ISLNK) && defined(HAVE_LSTAT)
		if(lstat(filename, &sb) < 0)
			continue;

		/* ok, if its a symlink, we work on the presumption if an
		 * oper help exists of that name, its a symlink to that --fl
		 */
		if(S_ISLNK(sb.st_mode))
		{
			cacheptr = hash_find_help(ldirent->d_name, HELP_OPER);

			if(cacheptr != NULL)
			{
				cacheptr->flags |= HELP_USER;
				continue;
			}
		}
#endif

		cacheptr = cache_file(filename, ldirent->d_name, HELP_USER);
		add_to_help_hash(cacheptr->name, cacheptr);
	}

	closedir(helpfile_dir);
}

