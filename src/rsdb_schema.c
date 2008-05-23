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

static void rsdb_schema_generate_table(const char *name, const char *primary_key, struct rsdb_schema *schema);

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
	rsdb_schema_generate_table("users", "id", rsdb_schema_users);
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
				/* this field has a primary key, add it now */
				if(primary_key)
				{
					char tmpbuf[BUFSIZE];
					tmpbuf[0] = '\0';

#if defined(RSERV_DB_PGSQL) || defined(RSERV_DB_MYSQL)
					snprintf(tmpbuf, sizeof(tmpbuf), "PRIMARY KEY(%s)", primary_key);
#endif
					strlcat(buf, tmpbuf, sizeof(buf));
				}
				else
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
