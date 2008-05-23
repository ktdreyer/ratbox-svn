/* src/rsdb_schema.c
 *   Contains the code for handling the database schema.
 *
 * Copyright (C) 2008 Lee Hardy <lee@leeh.co.uk>
 * Copyright (C) 2008 ircd-ratbox development team
 *
 */
#include "stdinc.h"
#include "rserv.h"
#include "rsdb.h"
#include "rsdb_schema.h"

static void rsdb_schema_generate_table(const char *name, struct rsdb_schema *schema);

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

void
rsdb_schema_generate(void)
{
	rsdb_schema_generate_table("users", rsdb_schema_users);
}

static void
rsdb_schema_generate_table(const char *name, struct rsdb_schema *schema)
{
}
