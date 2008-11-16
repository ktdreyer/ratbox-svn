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
#include "rsdb.h"
#include "rsdbs.h"
#include "log.h"

static struct rsdbs_schema_col stest_cs_nochange[] =
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
static struct rsdbs_schema_key stest_ks_nochange[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,	0, "id",		NULL	},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v_char",	NULL	},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v_varchar, v_int",	NULL	},
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v_bool",		NULL	},
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v_int, v_uint",	NULL	},
	{ 0, 0, 0, NULL, NULL }
};

static struct rsdbs_schema_col stest_cs1_addserial[] =
{
	{ RSDB_SCHEMA_BOOLEAN,		0,	0, "v_bool",	NULL	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_col stest_cs2_addserial[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,	0, "id",	NULL	},
	{ RSDB_SCHEMA_BOOLEAN,		0,	0, "v_bool",	NULL	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key stest_ks2_addserial[] =
{
	{ RSDB_SCHEMA_KEY_PRIMARY,	0,	0, "id",	NULL	},
	{ 0, 0, 0, NULL, NULL }
};

static struct rsdbs_schema_col stest_cs_addunique[] =
{
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v1_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v21_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v22_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v31_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v32_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v33_varchar",	"''"	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key stest_ks2_addunique[] =
{
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v1_varchar",		NULL	},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v21_varchar, v22_varchar",	NULL	},
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v31_varchar, v32_varchar, v33_varchar", NULL },
	{ 0, 0, 0, NULL, NULL }
};

static struct rsdbs_schema_col stest_cs_expandunique[] =
{
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v21_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v22_varchar",	"''"	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key stest_ks1_expandunique[] =
{
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v21_varchar",		NULL	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key stest_ks2_expandunique[] =
{
	{ RSDB_SCHEMA_KEY_UNIQUE,	0,	0, "v21_varchar, v22_varchar",	NULL	},
	{ 0, 0, 0, NULL, NULL }
};

static struct rsdbs_schema_col stest_cs_addindex[] =
{
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v1_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v21_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v22_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v31_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v32_varchar",	"''"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v33_varchar",	"''"	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_key stest_ks2_addindex[] =
{
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v1_varchar",		NULL	},
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v21_varchar, v22_varchar",	NULL	},
	{ RSDB_SCHEMA_KEY_INDEX,	0,	0, "v31_varchar, v32_varchar, v33_varchar", NULL },
	{ 0, 0, 0, NULL, NULL }
};

static struct rsdbs_schema_col stest_cs1_addcols[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,	0, "id",	NULL	},
	{ 0, 0, 0, NULL, NULL }
};
static struct rsdbs_schema_col stest_cs2_addcols[] =
{
	{ RSDB_SCHEMA_SERIAL,		0,	0, "id",	NULL	},
	{ RSDB_SCHEMA_BOOLEAN,		0,	0, "v_bool",	NULL	},
	{ RSDB_SCHEMA_INT,		0,	0, "v_int",	"0"	},
	{ RSDB_SCHEMA_UINT,		0,	0, "v_uint",	"0"	},
	{ RSDB_SCHEMA_VARCHAR,		100,	0, "v_varchar",	"''"	},
	{ RSDB_SCHEMA_CHAR,		100,	0, "v_char",	"''"	},
	{ RSDB_SCHEMA_TEXT,		0,	0, "v_text",	"''"	},
	{ 0, 0, 0, NULL, NULL }
};

static struct stest_schema_set
{
	const char *table_name;
	struct rsdbs_schema_col *schema1_col;
	struct rsdbs_schema_col *schema2_col;
	struct rsdbs_schema_key *schema1_key;
	struct rsdbs_schema_key *schema2_key;
	const char *description;
} stest_schema_tables[] = {
	{ 
		"nochange",	
		stest_cs_nochange,	stest_cs_nochange,	
		stest_ks_nochange,	stest_ks_nochange,	
		"No changes to this table"
	},
	{
		"addserial",
		stest_cs1_addserial,	stest_cs2_addserial,
		NULL,			stest_ks2_addserial,
		"Adding a SERIAL field (PRIMARY KEY)"
	},
	{
		"addunique",
		stest_cs_addunique,	stest_cs_addunique,
		NULL,			stest_ks2_addunique,
		"Adding UNIQUE constraints"
	},
	{
		"expandunique",
		stest_cs_expandunique,	stest_cs_expandunique,
		stest_ks1_expandunique,	stest_ks2_expandunique,
		"Expanding UNIQUE constraint to additional field"
	},
	{
		"addindex",
		stest_cs_addindex,	stest_cs_addindex,
		NULL,			stest_ks2_addindex,
		"Adding INDEXes"
	},
	{
		"addcols",
		stest_cs1_addcols,	stest_cs2_addcols,
		NULL,			NULL,
		"Adding various columns"
	},
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

void
schema_init(void)
{
	struct rsdb_table data;
	struct rsdb_schema_set *schema_set;
	const char *sql;
	size_t schema_size;
	int i;

	mlog("First pass, checking for a clean database.");

	/* run a quick check to make sure the tables dont exist */
	for(i = 0; stest_schema_tables[i].table_name; i++)
	{
		sql = rsdbs_sql_check_table(stest_schema_tables[i].table_name);

		rsdb_exec_fetch(&data, "%s", sql);

		/* table exists, it shouldn't */
		if(data.row_count > 0)
		{
			die(0, "Found already existing table '%s'.  Please run with a clean database.",
				stest_schema_tables[i].table_name);
		}
	}

	schema_size = sizeof(struct rsdb_schema_set) * (sizeof(stest_schema_tables) / sizeof(struct stest_schema_set));

	mlog("Second pass, creating initial schema.");

	schema_set = my_malloc(schema_size);

	for(i = 0; stest_schema_tables[i].table_name; i++)
	{
		schema_set[i].table_name = stest_schema_tables[i].table_name;
		schema_set[i].schema_col = stest_schema_tables[i].schema1_col;
		schema_set[i].schema_key = stest_schema_tables[i].schema1_key;

	}

	rsdb_schema_check(schema_set);

	mlog("Third pass, checking modifications.");

	for(i = 0; stest_schema_tables[i].table_name; i++)
	{
		mlog("%s", stest_schema_tables[i].description);

		memset(schema_set, 0, schema_size);
		schema_set[0].table_name = stest_schema_tables[i].table_name;
		schema_set[0].schema_col = stest_schema_tables[i].schema2_col;
		schema_set[0].schema_key = stest_schema_tables[i].schema2_key;
		rsdb_schema_check(schema_set);
	}

	rsdb_schema_check(schema_set);

	mlog("Fourth pass, no further modifications should be needed.");

	memset(schema_set, 0, sizeof(struct rsdb_schema_set) * (sizeof(stest_schema_tables) / sizeof(struct stest_schema_set)));

	for(i = 0; stest_schema_tables[i].table_name; i++)
	{
		schema_set[i].table_name = stest_schema_tables[i].table_name;
		schema_set[i].schema_col = stest_schema_tables[i].schema2_col;
		schema_set[i].schema_key = stest_schema_tables[i].schema2_key;
	}

	rsdb_schema_check(schema_set);
}
