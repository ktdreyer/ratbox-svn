/* src/s_memoserv.c
 *   Contains the code for memo services
 *
 * Copyright (C) 2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2007 ircd-ratbox development team
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
 * $Id: s_alis.c 23596 2007-02-05 21:35:27Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_MEMOSERV
#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "service.h"
#include "io.h"
#include "client.h"
#include "channel.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "s_userserv.h"

static struct client *memoserv_p;

static int s_memo_list(struct client *, struct lconn *, const char **, int);
static int s_memo_read(struct client *, struct lconn *, const char **, int);
static int s_memo_send(struct client *, struct lconn *, const char **, int);
static int s_memo_delete(struct client *, struct lconn *, const char **, int);

static struct service_command memoserv_command[] =
{
	{ "LIST",	&s_memo_list,	0, NULL, 1, 0L, 0, 0, 0 },
	{ "READ",	&s_memo_read,	1, NULL, 1, 0L, 0, 0, 0 },
	{ "SEND",	&s_memo_send,	2, NULL, 1, 0L, 0, 0, 0 },
	{ "DELETE",	&s_memo_delete,	1, NULL, 1, 0L, 0, 0, 0 },
};

static struct service_handler memoserv_service = {
	"MEMOSERV", "MEMOSERV", "memoserv", "services.int", "Memo Services",
        0, 0, memoserv_command, sizeof(memoserv_command), NULL, NULL, NULL
};

void
preinit_s_memoserv(void)
{
	memoserv_p = add_service(&memoserv_service);
}


static int
s_memo_list(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	return 0;
}

static int
s_memo_read(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	return 0;
}


static int
s_memo_send(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *msg;
	struct user_reg *ureg_p;
	struct rsdb_table data;
	unsigned int memo_id;
	dlink_node *ptr;

	if((ureg_p = find_user_reg_nick(client_p, parv[0])) == NULL)
		return 1;

	/* this user cannot receive memos */
	if(ureg_p->flags & US_FLAGS_NOMEMOS)
	{
		service_err(memoserv_p, client_p, SVC_USER_QUERYOPTION,
				ureg_p->name, "NOMEMOS", "ON");
		return 1;
	}

	rsdb_exec_fetch(&data, "SELECT COUNT(*) FROM memos WHERE user_id='%u'",
			ureg_p->id);

	if(data.row_count == 0)
	{
		mlog("fatal error: SELECT COUNT() returned 0 rows in s_memo_send()");
		die(0, "problem with db file");
	}

	if(atoi(data.row[0][0]) >= config_file.ms_max_memos)
	{
		service_err(memoserv_p, client_p, SVC_MEMO_TOOMANYMEMOS,
				ureg_p->name);
		rsdb_exec_fetch_end(&data);
		return 1;
	}

	rsdb_exec_fetch_end(&data);

	msg = rebuild_params(parv, parc, 1);

	rsdb_exec_insert(&memo_id, "memos", "id",
			"INSERT INTO memos (user_id, source_id, timestamp, flags, text)"
			"VALUES('%u', '%u', '%ld', '0', '%Q')",
			ureg_p->id, client_p->user->user_reg->id,
			CURRENT_TIME, msg);

	service_err(memoserv_p, client_p, SVC_MEMO_SENT, ureg_p->name);

	DLINK_FOREACH(ptr, ureg_p->users.head)
	{
		service_err(memoserv_p, ptr->data, SVC_MEMO_RECEIVED,
				memo_id, client_p->name);
	}

	return 0;
}


static int
s_memo_delete(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	return 0;
}


#endif
