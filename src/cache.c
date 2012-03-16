/* src/cache.c
 *   Contains code for caching files
 *
 * Copyright (C) 2003-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2012 ircd-ratbox development team
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
#include "rserv.h"
#include "balloc.h"
#include "cache.h"
#include "io.h"
#include "tools.h"

static rb_bh *cachefile_heap = NULL;
rb_bh *cacheline_heap = NULL;

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
	cachefile_heap = rb_bh_create(sizeof(struct cachefile), HEAP_CACHEFILE, "Helpfile Cache");
	cacheline_heap = rb_bh_create(sizeof(struct cacheline), HEAP_CACHELINE, "Helplines Cache");

	/* allocate the emptyline */
	emptyline = rb_bh_alloc(cacheline_heap);
	emptyline->data[0] = ' ';
}

/* cache_file()
 *
 * inputs	- file to cache, files "shortname", whether to add blank
 * 		  line at end
 * outputs	- pointer to file cached, else NULL
 * side effects -
 */
struct cachefile *
cache_file(const char *filename, const char *shortname, int add_blank)
{
	FILE *in;
	struct cachefile *cacheptr;
	struct cacheline *lineptr;
	char line[BUFSIZE];
	char *p;

	if((in = fopen(filename, "r")) == NULL)
		return NULL;

	cacheptr = rb_bh_alloc(cachefile_heap);
	rb_strlcpy(cacheptr->name, shortname, sizeof(cacheptr->name));

	/* cache the file... */
	while(fgets(line, sizeof(line), in) != NULL)
	{
		if((p = strchr(line, '\n')) != NULL)
			*p = '\0';

		if(!EmptyString(line))
		{
			lineptr = rb_bh_alloc(cacheline_heap);

			rb_strlcpy(lineptr->data, line, sizeof(lineptr->data));
			rb_dlinkAddTail(lineptr, &lineptr->linenode, &cacheptr->contents);
		}
		else
			rb_dlinkAddTailAlloc(emptyline, &cacheptr->contents);
	}

	if(add_blank)
		rb_dlinkAddTailAlloc(emptyline, &cacheptr->contents);

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
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;

	if(cacheptr == NULL)
		return;

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, cacheptr->contents.head)
	{
		if(ptr->data != emptyline)
			rb_bh_free(cacheline_heap, ptr->data);
		else
			rb_free_rb_dlink_node(ptr);
	}

	rb_bh_free(cachefile_heap, cacheptr);
}

void
send_cachefile(struct cachefile *cacheptr, struct lconn *conn_p)
{
        struct cacheline *lineptr;
        rb_dlink_node *ptr;

        if(cacheptr == NULL || conn_p == NULL)
                return;

        RB_DLINK_FOREACH(ptr, cacheptr->contents.head)
        {
                lineptr = ptr->data;
                sendto_one(conn_p, "%s", lineptr->data);
        }
}
