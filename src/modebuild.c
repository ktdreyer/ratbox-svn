/* src/modebuild.c
 *   Contains functions to allow services to build a mode buffer.
 *
 * Copyright (C) 2004-2005 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004-2005 ircd-ratbox development team
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
static dlink_list kickbuildr_list;

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

	len = arg != NULL ? strlen(arg) : 0;

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
	if (arg != NULL)
	{
		strcat(parabuf, arg);
		strcat(parabuf, " ");
		modecount++;
		plen += (len + 1);
	}

	cur_len++;
}

void
modebuild_finish(void)
{
	if(cur_len != mlen)
		sendto_server("%s %s", modebuf, parabuf);
}

struct kickbuilder
{
	const char *name;
	const char *reason;
};

void
kickbuild_start(void)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, kickbuild_list.head)
	{
		dlink_destroy(ptr, &kickbuild_list);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, kickbuildr_list.head)
	{
		dlink_destroy(ptr, &kickbuildr_list);
	}
}

void
kickbuild_add(const char *nick, const char *reason)
{
	dlink_add_tail_alloc((void *) nick, &kickbuild_list);
	dlink_add_tail_alloc((void *) reason, &kickbuildr_list);
}

void
kickbuild_finish(struct client *service_p, struct channel *chptr)
{
	dlink_node *ptr, *next_ptr;
	dlink_node *rptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, kickbuild_list.head)
	{
		rptr = kickbuildr_list.head;

		sendto_server(":%s KICK %s %s :%s",
				service_p->name, chptr->name,
				ptr->data, rptr->data);
		dlink_destroy(rptr, &kickbuildr_list);
		dlink_destroy(ptr, &kickbuild_list);
	}
}
