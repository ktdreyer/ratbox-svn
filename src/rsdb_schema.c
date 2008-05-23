/* src/rsdb_schema.c
 *   Contains the code for handling the database schema.
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
#include "rsdb_schema.h"

static void rsdb_schema_generate_table(const char *name, const char *primary_key, struct rsdb_schema *schema);

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

static struct rsdb_schema_set
{
	const char *table_name;
	struct rsdb_schema *schema;
	const char *primary_key;
} rsdb_schema_tables[] = {
	{ "users",		rsdb_schema_users,		"id"		},
	{ "users_resetpass",	rsdb_schema_users_resetpass,	"username"	},
	{ NULL, NULL, NULL }
};

void
rsdb_schema_generate(void)
{
	int i;

	for(i = 0; rsdb_schema_tables[i].table_name; i++)
	{
		rsdb_schema_generate_table(rsdb_schema_tables[i].table_name, rsdb_schema_tables[i].primary_key,
						rsdb_schema_tables[i].schema);
	}
}

static void
rsdb_schema_generate_table(const char *name, const char *primary_key, struct rsdb_schema *schema)
{
	dlink_list table_data;
	static char buf[BUFSIZE];
	int i;
	dlink_node *ptr;

	memset(&table_data, 0, sizeof(struct _dlink_list));

	snprintf(buf, sizeof(buf), "CREATE TABLE %s (", name);
	dlink_add_tail_alloc(my_strdup(buf), &table_data);

	for(i = 0; schema[i].name; i++)
	{
		buf[0] = '\0';

		switch(schema[i].option)
		{
			case RSDB_SCHEMA_SERIAL:
#if defined(RSERV_DB_PGSQL)
				snprintf(buf, sizeof(buf), "%s SERIAL, ", schema[i].name);
#elif defined(RSERV_DB_MYSQL)
				snprintf(buf, sizeof(buf), "%s INTEGER AUTO_INCREMENT, ", schema[i].name);
#elif defined(RSDB_DB_SQLITE)
				snprintf(buf, sizeof(buf), "%s INTEGER PRIMARY KEY, ", schema[i].name);
#endif
				break;

			case RSDB_SCHEMA_BOOLEAN:
				break;

			case RSDB_SCHEMA_INT:
				snprintf(buf, sizeof(buf), "%s INTEGER%s%s%s, ",
					schema[i].name,
					(schema[i].not_null ? " NOT NULL" : ""),
					(schema[i].def != NULL ? " DEFAULT " : ""),
					(schema[i].def != NULL ? schema[i].def : ""));
				break;

			case RSDB_SCHEMA_UINT:
				/* no unsigned ints here */
#if defined(RSERV_DB_PGSQL) || defined(RSERV_DB_SQLITE)
				snprintf(buf, sizeof(buf), "%s INTEGER%s%s%s, ",
					schema[i].name,
					(schema[i].not_null ? " NOT NULL" : ""),
					(schema[i].def != NULL ? " DEFAULT " : ""),
					(schema[i].def != NULL ? schema[i].def : ""));
#else
				snprintf(buf, sizeof(buf), "%s INTEGER UNSIGNED%s%s%s, ",
					schema[i].name,
					(schema[i].not_null ? " NOT NULL" : ""),
					(schema[i].def != NULL ? " DEFAULT " : ""),
					(schema[i].def != NULL ? schema[i].def : ""));
#endif
				break;

			case RSDB_SCHEMA_VARCHAR:
#if defined(RSERV_DB_PGSQL) || defined(RSERV_DB_MYSQL)
				snprintf(buf, sizeof(buf), "%s VARCHAR(%u)%s%s%s, ",
					schema[i].name, schema[i].length,
					(schema[i].not_null ? " NOT NULL" : ""),
					(schema[i].def != NULL ? " DEFAULT " : ""),
					(schema[i].def != NULL ? schema[i].def : ""));
#elif defined(RSERV_DB_SQLITE)
				snprintf(buf, sizeof(buf), "%s TEXT%s%s%s, ",
					schema[i].name,
					(schema[i].not_null ? " NOT NULL" : ""),
					(schema[i].def != NULL ? " DEFAULT " : ""),
					(schema[i].def != NULL ? schema[i].def : ""));
#endif
				break;

			case RSDB_SCHEMA_TEXT:
				break;
		}

		if(buf[0])
		{
			/* this field is the last element, either add a
			 * primary key value, or remove the trailing ','
			 */
			if(schema[i+1].name == NULL)
			{
/* primary keys are defined with the SERIAL value in sqlite */
#if defined(RSERV_DB_PGSQL) || defined(RSERV_DB_MYSQL)
				/* this field has a primary key, add it now */
				if(primary_key)
				{
					char tmpbuf[BUFSIZE];
					tmpbuf[0] = '\0';

					snprintf(tmpbuf, sizeof(tmpbuf), "PRIMARY KEY(%s)", primary_key);
					strlcat(buf, tmpbuf, sizeof(buf));
				}
				else
#endif
				{
					char *x = strchr(buf, ',');
					if(x)
						*x = '\0';
				}
			}

			dlink_add_tail_alloc(my_strdup(buf), &table_data);
		}
	}

	snprintf(buf, sizeof(buf), ");");
	dlink_add_tail_alloc(my_strdup(buf), &table_data);

#if 1
	DLINK_FOREACH(ptr, table_data.head)
	{
		fprintf(stderr, "%s", (const char *) ptr->data);
	}

	fprintf(stderr, "\n");
#endif
}
