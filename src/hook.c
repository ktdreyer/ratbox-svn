/* src/hook.c
 *   Contains code for "hooks"
 *
 * Copyright (C) 2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "hook.h"

static dlink_list hooks[HOOK_LAST_HOOK];

void
hook_add(hook_func func, int hook)
{
	if(hook >= HOOK_LAST_HOOK)
		return;

	dlink_add_tail_alloc(func, &hooks[hook]);
}

int
hook_call(int hook, void *arg, void *arg2)
{
	hook_func func;
	dlink_node *ptr;

	if(hook >= HOOK_LAST_HOOK)
		return 0;

	DLINK_FOREACH(ptr, hooks[hook].head)
	{
		func = ptr->data;
		if((*func)(arg, arg2) < 0)
			return -1;
	}

	return 0;
}
