/* $Id$ */
#ifndef INCLUDED_hook_h
#define INCLUDED_hook_h

#define HOOK_JOIN_CHANNEL	0	/* someone joining a channel */
#define HOOK_MODE_OP		1
#define HOOK_SQUIT_UNKNOWN	2	/* squit an unknown server */
#define HOOK_LAST_HOOK		3

typedef int (*hook_func)(void *, void *);

extern void hook_add(hook_func func, int hook);
extern int hook_call(int hook, void *arg, void *arg2);

#endif
