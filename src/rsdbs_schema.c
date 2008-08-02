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
 * $Id$
 */
#include "stdinc.h"
#include "rserv.h"
#include "rsdbs.h"
#include "client.h"
#include "channel.h"

/* table: users */
static struct rsdbs_schema_col rsdbs_cs_users[] = 
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
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_users[] =
{
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,			0, "username",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: users_resetpass */
static struct rsdbs_schema_col rsdbs_cs_users_resetpass[] =
{
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		10,			0, "token",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "time",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_users_resetpass[] =
{
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "username",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: users_resetemail */
static struct rsdbs_schema_col rsdbs_cs_users_resetemail[] =
{
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		10,			0, "token",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		EMAILLEN,		0, "email",		"NULL"		},
	{ RSDB_SCHEMA_UINT,		0,			0, "time",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_users_resetemail[] =
{
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "username",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: users_sync */
static struct rsdbs_schema_col rsdbs_cs_users_sync[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		50,			1, "hook",		NULL		},
	{ RSDB_SCHEMA_TEXT,		0,			0, "data",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_users_sync[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: nicks */
static struct rsdbs_schema_col rsdbs_cs_nicks[] =
{
	{ RSDB_SCHEMA_VARCHAR,		NICKLEN-1,		1, "nickname",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "reg_time",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "last_time",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		NULL			},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_nicks[] =
{
	{ RSDB_SCHEMA_KEY_F_MATCH,	0,			0, "username",		"users (username)"	},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "nickname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: channels */
static struct rsdbs_schema_col rsdbs_cs_channels[] =
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
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_channels[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: channels_dropowner */
static struct rsdbs_schema_col rsdbs_cs_channels_dropowner[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		10,			0, "token",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "time",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_channels_dropowner[] =
{
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "time",		NULL		},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: members */
static struct rsdbs_schema_col rsdbs_cs_members[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "lastmod",		NULL			},
	{ RSDB_SCHEMA_INT,		0,			0, "level",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		NULL			},
	{ RSDB_SCHEMA_INT,		0,			0, "suspend",		NULL			},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_members[] =
{
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "chname",		NULL			},
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "username",		NULL			},
	{ RSDB_SCHEMA_KEY_F_MATCH,	0,			0, "chname",		"channels (chname)"	},
	{ RSDB_SCHEMA_KEY_F_MATCH,	0,			0, "username",		"users (username)"	},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname, username",	NULL			},
	{ 0, 0, 0, NULL, NULL }
};

/* table: bans */
static struct rsdbs_schema_col rsdbs_cs_bans[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		NICKUSERHOSTLEN-1,	1, "mask",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		50,			1, "reason",		NULL			},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "username",		NULL			},
	{ RSDB_SCHEMA_INT,		0,			0, "level",		NULL			},
	{ RSDB_SCHEMA_UINT,		0,			0, "hold",		NULL			},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_bans[] =
{
	{ RSDB_SCHEMA_KEY_INDEX,	0,			0, "chname",		NULL			},
	{ RSDB_SCHEMA_KEY_F_MATCH,	0,			0, "chname",		"channels (chname)"	},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname, mask",	NULL			},
	{ 0, 0, 0, NULL, NULL }
};

/* table: operbot */
static struct rsdbs_schema_col rsdbs_cs_operbot[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "tsinfo",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "oper",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_operbot[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: operserv */
static struct rsdbs_schema_col rsdbs_cs_operserv[] =
{
	{ RSDB_SCHEMA_VARCHAR,		CHANNELLEN,		1, "chname",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "tsinfo",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "oper",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_operserv[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "chname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: jupes */
static struct rsdbs_schema_col rsdbs_cs_jupes[] =
{
	{ RSDB_SCHEMA_VARCHAR,		HOSTLEN,		1, "servername",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,		50,			1, "reason",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_jupes[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "servername",	NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: operbans */
static struct rsdbs_schema_col rsdbs_cs_operbans[] =
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
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_operbans[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "type, mask",	NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: operbans_regexp */
static struct rsdbs_schema_col rsdbs_cs_operbans_regexp[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "regex",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		REASONLEN,		1, "reason",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "hold",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "create_time",	NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		0, "oper",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_operbans_regexp[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: operbans_regexp_neg */
static struct rsdbs_schema_col rsdbs_cs_operbans_regexp_neg[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_SERIAL_REF, 	0,			1, "parent_id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "regex",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		1, "oper",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_operbans_regexp_neg[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: global_welcome */
static struct rsdbs_schema_col rsdbs_cs_global_welcome[] =
{
	{ RSDB_SCHEMA_INT,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_TEXT,		0,			0, "text",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_global_welcome[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: email_banned_domain */
static struct rsdbs_schema_col rsdbs_cs_email_banned_domain[] =
{
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "domain",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_email_banned_domain[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "domain",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: ignore_hosts */
static struct rsdbs_schema_col rsdbs_cs_ignore_hosts[] =
{
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "hostname",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		OPERNAMELEN,		1, "oper",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		255,			1, "reason",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_ignore_hosts[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "hostname",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

/* table: memos */
static struct rsdbs_schema_col rsdbs_cs_memos[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,			0, "id",		NULL		},
	{ RSDB_SCHEMA_SERIAL_REF, 	0,			1, "user_id",		NULL		},
	{ RSDB_SCHEMA_SERIAL_REF, 	0,			1, "source_id",		NULL		},
	{ RSDB_SCHEMA_VARCHAR,		USERREGNAME_LEN-1,	1, "source",		NULL		},
	{ RSDB_SCHEMA_UINT,		0,			0, "timestamp",		"0"		},
	{ RSDB_SCHEMA_UINT,		0,			0, "flags",		"0"		},
	{ RSDB_SCHEMA_TEXT,		0,			0, "text",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key rsdbs_ks_memos[] =
{
	{ RSDB_SCHEMA_KEY_F_CASCADE,	0,			0, "user_id",		"users (id)"	},
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,			0, "id",		NULL		},
	{ 0, 0, 0, NULL, NULL }
};

static struct rsdb_schema_set rsdb_schema_tables[] = {
	{ "users",		rsdbs_cs_users,			rsdbs_ks_users,			0 },
	{ "users_resetpass",	rsdbs_cs_users_resetpass,	rsdbs_ks_users_resetpass,	0 },
	{ "users_resetemail",	rsdbs_cs_users_resetemail,	rsdbs_ks_users_resetemail,	0 },
	{ "users_sync",		rsdbs_cs_users_sync,		rsdbs_ks_users_sync,		0 },
	{ "nicks",		rsdbs_cs_nicks,			rsdbs_ks_nicks,			0 },
	{ "channels",		rsdbs_cs_channels,		rsdbs_ks_channels,		0 },
	{ "channels_dropowner",	rsdbs_cs_channels_dropowner,	rsdbs_ks_channels_dropowner,	0 },
	{ "members",		rsdbs_cs_members,		rsdbs_ks_members,		0 },
	{ "bans",		rsdbs_cs_bans,			rsdbs_ks_bans,			0 },
	{ "operbot",		rsdbs_cs_operbot,		rsdbs_ks_operbot,		0 },
	{ "operserv",		rsdbs_cs_operserv,		rsdbs_ks_operserv,		0 },
	{ "jupes",		rsdbs_cs_jupes,			rsdbs_ks_jupes,			0 },
	{ "operbans",		rsdbs_cs_operbans,		rsdbs_ks_operbans,		0 },
	{ "operbans_regexp",	rsdbs_cs_operbans_regexp,	rsdbs_ks_operbans_regexp,	0 },
	{ "operbans_regexp_neg",rsdbs_cs_operbans_regexp_neg,	rsdbs_ks_operbans_regexp_neg,	0 },
	{ "global_welcome",	rsdbs_cs_global_welcome,	rsdbs_ks_global_welcome,	0 },
	{ "email_banned_domain",rsdbs_cs_email_banned_domain,	rsdbs_ks_email_banned_domain,	0 },
	{ "ignore_hosts",	rsdbs_cs_ignore_hosts,		rsdbs_ks_ignore_hosts,		0 },
	{ "memos",		rsdbs_cs_memos,			rsdbs_ks_memos,			0 },
	{ NULL, NULL, 0 }
};

void
schema_init(int write_sql)
{
	rsdb_schema_check(rsdb_schema_tables, write_sql);
}

