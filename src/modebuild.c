/* src/modebuild.c
 *   Contains functions to allow services to build a mode buffer.
 *
 * Copyright (C) 2004 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#include "rserv.h"
#include "client.h"
#include "channel.h"
#include "io.h"
#include "modebuild.h"

static dlink_list kickbuild_list;

static char modebuf[BUFSIZE];
static char parabuf[BUFSIZE];
static int modedir;
static int modecount;
static int mlen;
static int plen;
static int cur_len;

static void
modebuild_init(void)
{
	cur_len = mlen;
	plen = 0;
	parabuf[0] = '\0';
	modedir = DIR_NONE;
	modecount = 0;
}

void
modebuild_start(struct client *source_p, struct channel *chptr)
{
	mlen = snprintf(modebuf, sizeof(modebuf), ":%s MODE %s ",
			source_p->name, chptr->name);

	modebuild_init();
}

void
modebuild_add(int dir, const char *mode, const char *arg)
{
	int len;

	len = strlen(arg);

	if((cur_len + plen + len + 4) > (BUFSIZE - 3) ||
	   modecount >= MAX_MODES)
	{
		sendto_server("%s %s", modebuf, parabuf);

		modebuf[mlen] = '\0';
		modebuild_init();
	}

	if(modedir != dir)
	{
		if(dir == DIR_ADD)
			strcat(modebuf, "+");
		else
			strcat(modebuf, "-");
		cur_len++;
	}

	strcat(modebuf, mode);
	strcat(parabuf, arg);
	strcat(parabuf, " ");

	cur_len++;
	plen += (len + 1);
	modecount++;
}

void
modebuild_finish(void)
{
	if(cur_len != mlen)
		sendto_server("%s %s", modebuf, parabuf);
}

void
kickbuild_start(void)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, kickbuild_list.head)
	{
		my_free(ptr->data);
		dlink_destroy(ptr, &kickbuild_list);
	}
}

void
kickbuild_add(const char *nick)
{
	dlink_add_alloc(my_strdup(nick), &kickbuild_list);
}

void
kickbuild_finish(struct client *service_p, struct channel *chptr,
		const char *reason)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, kickbuild_list.head)
	{
		sendto_server(":%s KICK %s %s :%s",
				service_p->name, chptr->name,
				ptr->data, reason);
		my_free(ptr->data);
		dlink_destroy(ptr, &kickbuild_list);
	}
}
