/* $Id: s_nickserv.h 20344 2005-05-06 21:51:24Z leeh $ */
#ifndef INCLUDED_banserv_h
#define INCLUDED_banserv_h

#ifdef PCRE_BUILD
#include "pcre.h"
#else
#include <pcre.h>
#endif

extern rb_dlink_list regexp_list;
extern struct ev_entry *banserv_autosync_ev;

struct regexp_ban
{
	char *regexp_str;
	char *reason;
	char *oper;

	time_t hold;
	time_t create_time;

	unsigned int id;

	rb_dlink_node ptr;
	rb_dlink_list negations;

	struct regexp_ban *parent;

	pcre *regexp;
};

#endif
