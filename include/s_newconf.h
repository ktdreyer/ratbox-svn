/*
 * ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 * s_newconf.h: code for dealing with conf stuff
 *
 * Copyright (C) 2004 Lee Hardy <lee@leeh.co.uk>
 * Copyright (C) 2004 ircd-ratbox development team
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

#ifndef INCLUDED_s_newconf_h
#define INCLUDED_s_newconf_h

#include "tools.h"

#define SHARED_KLINE	0x0001
#define SHARED_UNKLINE	0x0002
#define SHARED_LOCOPS	0x0004
#define SHARED_XLINE	0x0008
#define SHARED_UNXLINE	0x0010
#define SHARED_RESV	0x0020
#define SHARED_UNRESV	0x0040

#define SHARED_ALL	(SHARED_KLINE | SHARED_UNKLINE | SHARED_XLINE |\
			SHARED_UNXLINE | SHARED_RESV | SHARED_UNRESV)
#define CLUSTER_ALL	(SHARED_ALL | SHARED_LOCOPS)

struct shared_conf
{
	char *username;
	char *host;
	char *server;
	int flags;
};

extern dlink_list cluster_list;
extern dlink_list shared_list;

extern struct shared_conf *make_shared_conf(void);
extern void free_shared_conf(struct shared_conf *);
extern void clear_shared_conf(void);

extern int find_shared_conf(const char *username, const char *host,
			const char *server, int flags);
#endif

