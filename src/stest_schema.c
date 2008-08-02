/* src/stest_schema.c
 *   Contains the database schema for the schema test program.
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

static struct rsdbs_schema_col stest_cs1_nochange[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,	0, "id",	NULL	},
	{ RSDB_SCHEMA_SERIAL_REF,	0,	0, "id_ref",	NULL	},
	{ RSDB_SCHEMA_BOOLEAN,		0,	0, "v_bool",	NULL	},
	{ RSDB_SCHEMA_INT,		0,	0, "v_int",	"0"	},
	{ RSDB_SCHEMA_UINT,		0,	0, "v_uint",	"0"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v_varchar",	"''"	},
	{ RSDB_SCHEMA_CHAR,		100,	0, "v_char",	"''"	},
	{ RSDB_SCHEMA_TEXT,		0,	0, "v_text",	"''"	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key stest_ks1_nochange[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,	0, "id",		NULL	},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v_text",		NULL	},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v_varchar, v_char",	NULL	},
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v_bool",		NULL	},
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v_int, v_uint",	NULL	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_col stest_cs2_nochange[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,	0, "id",	NULL	},
	{ RSDB_SCHEMA_SERIAL_REF,	0,	0, "id_ref",	NULL	},
	{ RSDB_SCHEMA_BOOLEAN,		0,	0, "v_bool",	NULL	},
	{ RSDB_SCHEMA_INT,		0,	0, "v_int",	"0"	},
	{ RSDB_SCHEMA_UINT,		0,	0, "v_uint",	"0"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v_varchar",	"''"	},
	{ RSDB_SCHEMA_CHAR,		100,	0, "v_char",	"''"	},
	{ RSDB_SCHEMA_TEXT,		0,	0, "v_text",	"''"	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key stest_ks2_nochange[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,	0, "id",		NULL	},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v_text",		NULL	},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v_varchar, v_char",	NULL	},
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v_bool",		NULL	},
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v_int, v_uint",	NULL	},
	{ 0, 0, 0, NULL, NULL }
};



static struct rsdb_schema_set stest_schema1_tables[] =
{
	{ "nochange",		stest_cs1_nochange,		stest_ks1_nochange,		0 },
	{ NULL, NULL, NULL, 0 }
};

static struct rsdb_schema_set stest_schema2_tables[] =
{
	{ "nochange",		stest_cs2_nochange,		stest_ks2_nochange,		0 },
	{ NULL, NULL, NULL, 0 }
};

void
schema_init(int create)
{
	if(create)
		rsdb_schema_check(stest_schema1_tables, 1);
	else
		rsdb_schema_check(stest_schema2_tables, 0);
}
