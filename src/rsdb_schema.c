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

static void rsdb_schema_generate_table(struct rsdb_schema_set *schema_set);

void
rsdb_schema_generate(struct rsdb_schema_set *schema_set)
{
	int i;

	for(i = 0; schema_set[i].table_name; i++)
	{
		rsdb_schema_generate_table(&schema_set[i]);
	}
}

static void
rsdb_schema_generate_table(struct rsdb_schema_set *schema_set)
{
	struct rsdb_schema *schema;
	dlink_list table_data;
	static char buf[BUFSIZE];
	int i;
	dlink_node *ptr;

	memset(&table_data, 0, sizeof(struct _dlink_list));

	schema = schema_set->schema;

	snprintf(buf, sizeof(buf), "CREATE TABLE %s (", schema_set->table_name);
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
#elif defined(RSERV_DB_SQLITE)
				snprintf(buf, sizeof(buf), "%s INTEGER PRIMARY KEY, ", schema[i].name);
#endif
				break;

			case RSDB_SCHEMA_SERIAL_REF:
#if defined(RSERV_DB_PGSQL)
				snprintf(buf, sizeof(buf), "%s BIGINT%s%s%s, ",
					schema[i].name,
					(schema[i].not_null ? " NOT NULL" : ""),
					(schema[i].def != NULL ? " DEFAULT " : ""),
					(schema[i].def != NULL ? schema[i].def : ""));
#elif defined(RSERV_DB_MYSQL) || defined(RSERV_DB_SQLITE)
				snprintf(buf, sizeof(buf), "%s INTEGER%s%s%s, ",
					schema[i].name,
					(schema[i].not_null ? " NOT NULL" : ""),
					(schema[i].def != NULL ? " DEFAULT " : ""),
					(schema[i].def != NULL ? schema[i].def : ""));
#endif
				break;

			case RSDB_SCHEMA_BOOLEAN:
#if defined(RSERV_DB_PGSQL) || defined(RSERV_DB_MYSQL)
				snprintf(buf, sizeof(buf), "%s BOOL, ", schema[i].name);
#elif defined(RSERV_DB_SQLITE)
				snprintf(buf, sizeof(buf), "%s INTEGER, ", schema[i].name);
#endif
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

			case RSDB_SCHEMA_CHAR:
#if defined(RSERV_DB_PGSQL) || defined(RSERV_DB_MYSQL)
				snprintf(buf, sizeof(buf), "%s CHAR(%u)%s%s%s, ",
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
				snprintf(buf, sizeof(buf), "%s TEXT%s%s%s, ",
					schema[i].name,
					(schema[i].not_null ? " NOT NULL" : ""),
					(schema[i].def != NULL ? " DEFAULT " : ""),
					(schema[i].def != NULL ? schema[i].def : ""));
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
				if(!EmptyString(schema_set->primary_key))
				{
					char tmpbuf[BUFSIZE];
					tmpbuf[0] = '\0';

					snprintf(tmpbuf, sizeof(tmpbuf), "PRIMARY KEY(%s)", schema_set->primary_key);
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
