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
#include "client.h"
#include "channel.h"

/* table: users */
static struct rsdb_schema rsdb_schema_users[] = 
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		PASSWDLEN,		1, "password",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		EMAILLEN,		0, "email",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "suspender",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		SUSPENDREASONLEN,	0, "suspend_reason",	NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "suspend_time",	"0"		},
	{ RSDB_SCHEMA_UINT,		0,			0, "reg_time",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "last_time",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		8,			0, "verify_token",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,		255,			0, "language",		"''"		},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,			0, "username",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: users_resetpass */
static struct rsdb_schema rsdb_schema_users_resetpass[] =
{
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		10,			0, "token",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "username",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: users_resetemail */
static struct rsdb_schema rsdb_schema_users_resetemail[] =
{
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		10,			0, "token",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		EMAILLEN,		0, "email",		"NULL"		},
	{ RSDB_SCHEMA_UINT,		0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "username",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: users_sync */
static struct rsdb_schema rsdb_schema_users_sync[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		50,			1, "hook",		NULL		},
	{ RSDB_SCHEMA_TEXT,		0,			0, "data",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: nicks */
static struct rsdb_schema rsdb_schema_nicks[] =
{
	{ RSDB_SCHEMA_VARCHAR,		NICKLEN-1,		1, "nickname",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "reg_time",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "last_time",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		NULL			},
	{ RSDB_SCHEMA_KEY_F_MATCH,	0,			0, "username",		"users (username)"	},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "nickname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: channels */
static struct rsdb_schema rsdb_schema_channels[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		TOPICLEN-1,		0, "topic",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		URLLEN,			0, "url",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		50,			0, "createmodes",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,		50,			0, "enforcemodes",	NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "tsinfo",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "reg_time",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "last_time",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "suspender",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		SUSPENDREASONLEN,	0, "suspend_reason",	NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "suspend_time",	"0"		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: channels_dropowner */
static struct rsdb_schema rsdb_schema_channels_dropowner[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		10,			0, "token",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: members */
static struct rsdb_schema rsdb_schema_members[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "lastmod",		NULL			},
	{ RSDB_SCHEMA_INT,		0,			0, "level",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		NULL			},
	{ RSDB_SCHEMA_INT,		0,			0, "suspend",		NULL			},
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "chname",		NULL			},
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "username",		NULL			},
	{ RSDB_SCHEMA_KEY_F_MATCH,	0,			0, "chname",		"channels (chname)"	},
	{ RSDB_SCHEMA_KEY_F_MATCH,	0,			0, "username",		"users (username)"	},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname, username",	NULL			},
	{ 0, 0, 0, NULL, NULL }
};
/* table: bans */
static struct rsdb_schema rsdb_schema_bans[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		NICKUSERHOSTLEN-1,	1, "mask",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		50,			1, "reason",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL			},
	{ RSDB_SCHEMA_INT,		0,			0, "level",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "hold",		NULL			},
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "chname",		NULL			},
	{ RSDB_SCHEMA_KEY_F_MATCH,	0,			0, "chname",		"channels (chname)"	},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname, mask",	NULL			},
	{ 0, 0, 0, NULL, NULL }
};
/* table: operbot */
static struct rsdb_schema rsdb_schema_operbot[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "tsinfo",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "oper",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: operserv */
static struct rsdb_schema rsdb_schema_operserv[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "tsinfo",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "oper",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: jupes */
static struct rsdb_schema rsdb_schema_jupes[] =
{
	{ RSDB_SCHEMA_VARCHAR,		HOSTLEN,		1, "servername",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,		50,			1, "reason",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "servername",	NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: operbans */
static struct rsdb_schema rsdb_schema_operbans[] =
{
	{ RSDB_SCHEMA_CHAR,		1,			1, "type",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "mask",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		REASONLEN,		1, "reason",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		REASONLEN,		0, "operreason",	NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "hold",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "create_time",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "oper",		NULL		},
	{ RSDB_SCHEMA_BOOLEAN,		0,			0, "remove",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "type, mask",	NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: operbans_regexp */
static struct rsdb_schema rsdb_schema_operbans_regexp[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "regex",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		REASONLEN,		1, "reason",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "hold",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "create_time",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "oper",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: operbans_regexp_neg */
static struct rsdb_schema rsdb_schema_operbans_regexp_neg[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_SERIAL_REF, 	0,			1, "parent_id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "regex",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		1, "oper",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: global_welcome */
static struct rsdb_schema rsdb_schema_global_welcome[] =
{
	{ RSDB_SCHEMA_INT,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_TEXT,		0,			0, "text",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: email_banned_domain */
static struct rsdb_schema rsdb_schema_email_banned_domain[] =
{
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "domain",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "domain",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: ignore_hosts */
static struct rsdb_schema rsdb_schema_ignore_hosts[] =
{
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "hostname",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		1, "oper",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "reason",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "hostname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
/* table: memos */
static struct rsdb_schema rsdb_schema_memos[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_SERIAL_REF, 	0,			1, "user_id",		NULL		},
	{ RSDB_SCHEMA_SERIAL_REF, 	0,			1, "source_id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "source",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "timestamp",		"0"		},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		"0"		},
	{ RSDB_SCHEMA_TEXT,		0,			0, "text",		NULL		},
	{ RSDB_SCHEMA_KEY_F_CASCADE,	0,			0, "user_id",		"users (id)"	},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

static struct rsdb_schema_set rsdb_schema_tables[] = {
	{ "users",		rsdb_schema_users		},
	{ "users_resetpass",	rsdb_schema_users_resetpass	},
	{ "users_resetemail",	rsdb_schema_users_resetemail	},
	{ "users_sync",		rsdb_schema_users_sync		},
	{ "nicks",		rsdb_schema_nicks		},
	{ "channels",		rsdb_schema_channels		},
	{ "channels_dropowner",	rsdb_schema_channels_dropowner	},
	{ "members",		rsdb_schema_members,		},
	{ "bans",		rsdb_schema_bans,		},
	{ "operbot",		rsdb_schema_operbot		},
	{ "operserv",		rsdb_schema_operserv		},
	{ "jupes",		rsdb_schema_jupes		},
	{ "operbans",		rsdb_schema_operbans,		},
	{ "operbans_regexp",	rsdb_schema_operbans_regexp	},
	{ "operbans_regexp_neg",rsdb_schema_operbans_regexp_neg	},
	{ "global_welcome",	rsdb_schema_global_welcome	},
	{ "email_banned_domain",rsdb_schema_email_banned_domain },
	{ "ignore_hosts",	rsdb_schema_ignore_hosts	},
	{ "memos",		rsdb_schema_memos	},
	{ NULL, NULL }
};

void
schema_init(void)
{
	rsdb_schema_check(rsdb_schema_tables);
}

