/* src/schema.c
 *   Contains the database schema
 *
 * Copyright (C) 2008 Lee Hardy <lee@leeh.co.uk>
 * Copyright (C) 2008 ircd-ratbox development team
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
 * $Id: rsdb_schema.c 25456 2008-05-23 22:40:10Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "rsdb.h"
#include "rsdb_schema.h"
#include "client.h"
#include "channel.h"

/* table: users */
static struct rsdb_schema rsdb_schema_users[] = 
{
	{ RSDB_SCHEMA_SERIAL,	0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	USERREGNAME_LEN,	1, "username",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	PASSWDLEN,		1, "password",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	EMAILLEN,		0, "email",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	OPERNAMELEN,		0, "suspender",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	SUSPENDREASONLEN,	0, "suspend_reason",	NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "suspend_time",	"0"		},
	{ RSDB_SCHEMA_UINT,	0,			0, "reg_time",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "last_time",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "flags",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	8,			0, "verify_token",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,	255,			0, "language",		"''"		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: users_resetpass */
static struct rsdb_schema rsdb_schema_users_resetpass[] =
{
	{ RSDB_SCHEMA_VARCHAR,	USERREGNAME_LEN,	1, "username",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	10,			0, "token",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "time",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: users_resetemail */
static struct rsdb_schema rsdb_schema_users_resetemail[] =
{
	{ RSDB_SCHEMA_VARCHAR,	USERREGNAME_LEN,	1, "username",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	10,			0, "token",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	EMAILLEN,		0, "email",		"NULL"		},
	{ RSDB_SCHEMA_UINT,	0,			0, "time",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: users_sync */
static struct rsdb_schema rsdb_schema_users_sync[] =
{
	{ RSDB_SCHEMA_SERIAL,	0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	50,			1, "hook",		NULL		},
	{ RSDB_SCHEMA_TEXT,	0,			0, "data",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: nicks */
static struct rsdb_schema rsdb_schema_nicks[] =
{
	{ RSDB_SCHEMA_VARCHAR,	NICKLEN,		1, "nickname",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	USERREGNAME_LEN,	1, "username",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "reg_time",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "last_time",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "flags",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: channels */
static struct rsdb_schema rsdb_schema_channels[] =
{
	{ RSDB_SCHEMA_VARCHAR,	CHANNELLEN,		1, "chname",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	TOPICLEN,		0, "topic",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	URLLEN,			0, "url",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	50,			0, "createmodes",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,	50,			0, "enforcemodes",	NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "tsinfo",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "reg_time",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "last_time",		NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "flags",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	OPERNAMELEN,		0, "suspender",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,	SUSPENDREASONLEN,	0, "suspend_reason",	NULL		},
	{ RSDB_SCHEMA_UINT,	0,			0, "suspend_time",	"0"		},
	{ 0, 0, 0, NULL, NULL }
};

static struct rsdb_schema_set rsdb_schema_tables[] = {
	{ "users",		rsdb_schema_users,		"id"		},
	{ "users_resetpass",	rsdb_schema_users_resetpass,	"username"	},
	{ "users_resetemail",	rsdb_schema_users_resetemail,	"username"	},
	{ "users_sync",		rsdb_schema_users_sync,		"id"		},
	{ "nicks",		rsdb_schema_nicks,		"nickname"	},
	{ "channels",		rsdb_schema_channels,		"chname"	},
	{ NULL, NULL, NULL }
};

void
schema_init(void)
{
	rsdb_schema_generate(rsdb_schema_tables);
}

