/* src/rsdbs_pgsql.c
 *   Contains the code for schema interactions with the postgresql database backend.
 *
 * Copyright (C) 2008 Lee Hardy <leeh@leeh.co.uk>
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
#include "rsdb.h"
#include "rsdbs.h"
#include "rserv.h"

/* rsdbs_sql_check_table()
 * Returns the SQL for checking whether a table exists
 *
 * inputs	- table name to check
 * outputs	- SQL
 * side effects	-
 */
const char *
rsdbs_sql_check_table(const char *table_name)
{
	static char buf[BUFSIZE*2];

	rs_snprintf(buf, sizeof(buf), "SELECT table_name FROM information_schema.tables WHERE table_name='%Q'",
			table_name);
	return buf;
}

static const char *
rsdbs_sql_drop_key_pri(const char *table_name)
{
	static char buf[BUFSIZE*2];
	struct rsdb_table data;
	const char *buf_ptr = NULL;

	/* drop any existing primary keys -- there can be only one. */
	/* find the name of the constraint, and drop it specifically */
	rsdb_exec_fetch(&data, "SELECT tc.constraint_name FROM information_schema.table_constraints tc "
				"WHERE tc.constraint_type='PRIMARY KEY' AND tc.table_name='%Q'",
			table_name);

	if(data.row_count > 0)
	{
		rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q DROP CONSTRAINT %Q CASCADE;",
				table_name, data.row[0][0]);
		buf_ptr = buf;
	}

	rsdb_exec_fetch_end(&data);

	return buf_ptr;
}

static int
rsdbs_check_column(const char *table_name, const char *column_name)
{
	struct rsdb_table data;

	rsdb_exec_fetch(&data, "SELECT column_name FROM information_schema.columns WHERE table_name='%Q' AND column_name='%Q'",
			table_name, column_name);

	if(data.row_count > 0)
	{
		rsdb_exec_fetch_end(&data);
		return 1;
	}

	rsdb_exec_fetch_end(&data);
	return 0;
}

static int
rsdbs_check_key_is(const char *table_name, const char *key_list_str, rsdb_schema_option option)
{
	static const char option_str_pri[] = "PRIMARY KEY";
	static const char option_str_unique[] = "UNIQUE";
	char buf[BUFSIZE*2];
	char lbuf[BUFSIZE*2];
	struct rsdb_table data;
	dlink_list *field_list;
	dlink_node *ptr;
	const char *option_str;

	if(option == RSDB_SCHEMA_KEY_PRIMARY)
		option_str = option_str_pri;
	else if(option == RSDB_SCHEMA_KEY_UNIQUE)
		option_str = option_str_unique;
	else
		return 0;

	field_list = rsdb_schema_split_key(key_list_str);

	/* build a sql statement, to grab the count of elements in our key
	 * for the table, that match any of the columns specified.  The count value 
	 * returned should equal the number of fields we're searching for.
	 */
	/* XXX There is a flaw with this code -- in that if a key includes the values we
	 * are searching for, along with others that shouldn't be there, it will not find
	 * any problems..
	 */
	rs_snprintf(buf, sizeof(buf), "SELECT ccu.column_name FROM information_schema.constraint_column_usage ccu "
					"JOIN information_schema.table_constraints tc "
					"ON ccu.constraint_name=tc.constraint_name "
					"WHERE tc.constraint_type='%Q' AND ccu.table_name='%Q'",
			option_str, table_name);

	DLINK_FOREACH(ptr, field_list->head)
	{
		/* we want to OR the column names together to find the count of all the
		 * matching entries -- but this OR block itself needs an AND for the first
		 * element to join it to the buffer above
		 */
		if(ptr == field_list->head)
			rs_snprintf(lbuf, sizeof(lbuf), " AND (ccu.column_name='%Q'",
					(char *) ptr->data);
		else
			rs_snprintf(lbuf, sizeof(lbuf), " OR ccu.column_name='%Q'",
					(char *) ptr->data);

		strlcat(buf, lbuf, sizeof(buf));
	}

	/* close the sql brace */
	strlcat(buf, ")", sizeof(buf));

	rsdb_exec_fetch(&data, "%s", buf);

	/* the SQL above should have returned a row per match -- so the number of elements
	 * in the primary key we are adding should match this..
	 */
	if(data.row_count == dlink_list_length(field_list))
	{
		rsdb_exec_fetch_end(&data);
		return 1;
	}

	rsdb_exec_fetch_end(&data);
	return 0;
}

static int
rsdbs_check_key_pri(const char *table_name, const char *key_list_str)
{
	return rsdbs_check_key_is(table_name, key_list_str, RSDB_SCHEMA_KEY_PRIMARY);
}

static int
rsdbs_check_key_unique(const char *table_name, const char *key_list_str)
{
	return rsdbs_check_key_is(table_name, key_list_str, RSDB_SCHEMA_KEY_PRIMARY);
}

static int
rsdbs_check_key_index(const char *table_name, const char *key_list_str)
{
	struct rsdb_table data;
	const char *idx_name;

	idx_name = rsdbs_generate_key_name(table_name, key_list_str, RSDB_SCHEMA_KEY_INDEX);

	/* I never found a nice way to check the indexes in pgsql, so just attempt to find
	 * an index with a name of what is appropriate (e.g. an index for the table bans
	 * on the field chname, should be called bans_chname_idx), and leave it at that.
	 */
	rsdb_exec_fetch(&data, "SELECT * FROM pg_catalog.pg_class WHERE relname='%Q'", idx_name);

	if(data.row_count > 0)
	{
		rsdb_exec_fetch_end(&data);
		return 1;
	}

	rsdb_exec_fetch_end(&data);
	return 0;
}

void
rsdb_schema_check_table(struct rsdb_schema_set *schema_set)
{
	struct rsdb_schema *schema;
	dlink_list table_data;
	dlink_list key_data;
	int i;

	memset(&table_data, 0, sizeof(struct _dlink_list));
	memset(&key_data, 0, sizeof(struct _dlink_list));

	schema = schema_set->schema;

	for(i = 0; schema[i].name; i++)
	{
		switch(schema[i].option)
		{
			case RSDB_SCHEMA_SERIAL:
			case RSDB_SCHEMA_SERIAL_REF:
			case RSDB_SCHEMA_BOOLEAN:
			case RSDB_SCHEMA_INT:
			case RSDB_SCHEMA_UINT:
			case RSDB_SCHEMA_VARCHAR:
			case RSDB_SCHEMA_CHAR:
			case RSDB_SCHEMA_TEXT:
				if(!rsdbs_check_column(schema_set->table_name, schema[i].name))
					rsdb_schema_generate_element(schema_set, &schema[i], &table_data, &key_data);

				break;

			case RSDB_SCHEMA_KEY_PRIMARY:
				/* check if the constraint exists */
				if(!rsdbs_check_key_pri(schema_set->table_name, schema[i].name))
				{
					const char *drop_sql = rsdbs_sql_drop_key_pri(schema_set->table_name);

					/* drop existing primary keys if found */
					if(drop_sql)
						dlink_add_alloc(my_strdup(drop_sql), &key_data);

					/* add in the sql for the primary key */
					rsdb_schema_generate_element(schema_set, &schema[i], &table_data, &key_data);
				}

				break;

			case RSDB_SCHEMA_KEY_UNIQUE:
				/* check if the constraint exists */
				if(!rsdbs_check_key_unique(schema_set->table_name, schema[i].name))
					rsdb_schema_generate_element(schema_set, &schema[i], &table_data, &key_data);

				break;


			case RSDB_SCHEMA_KEY_INDEX:
				if(!rsdbs_check_key_index(schema_set->table_name, schema[i].name))
					rsdb_schema_generate_element(schema_set, &schema[i], &table_data, &key_data);

				break;

			case RSDB_SCHEMA_KEY_F_MATCH:
			case RSDB_SCHEMA_KEY_F_CASCADE:
				break;
		}
	}

	rsdb_schema_debug(schema_set->table_name, &table_data, &key_data, 0);
}

void
rsdb_schema_generate_element(struct rsdb_schema_set *schema_set, struct rsdb_schema *schema_element,
				dlink_list *table_data, dlink_list *key_data)
{
	static char buf[BUFSIZE];
	const char *idx_name;
	int is_key = 0;

	buf[0] = '\0';

	switch(schema_element->option)
	{
		case RSDB_SCHEMA_SERIAL:
			snprintf(buf, sizeof(buf), "%s SERIAL", schema_element->name);
			break;

		case RSDB_SCHEMA_SERIAL_REF:
			snprintf(buf, sizeof(buf), "%s BIGINT%s%s%s",
				schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_BOOLEAN:
			snprintf(buf, sizeof(buf), "%s BOOL", schema_element->name);
			break;

		case RSDB_SCHEMA_INT:
			snprintf(buf, sizeof(buf), "%s INTEGER%s%s%s",
				schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_UINT:
			snprintf(buf, sizeof(buf), "%s INTEGER%s%s%s",
				schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_VARCHAR:
			snprintf(buf, sizeof(buf), "%s VARCHAR(%u)%s%s%s",
				schema_element->name, schema_element->length,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_CHAR:
			snprintf(buf, sizeof(buf), "%s CHAR(%u)%s%s%s",
				schema_element->name, schema_element->length,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_TEXT:
			snprintf(buf, sizeof(buf), "%s TEXT%s%s%s",
				schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_KEY_PRIMARY:
			is_key = 1;
			snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD PRIMARY KEY(%s);",
				schema_set->table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_UNIQUE:
			is_key = 1;
			idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name, 
								schema_element->option);

			rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q ADD CONSTRAINT %Q UNIQUE (%Q);",
				schema_set->table_name, idx_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_INDEX:
			is_key = 1;
			idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name, 
								schema_element->option);

			rs_snprintf(buf, sizeof(buf), "CREATE INDEX %Q ON %Q (%Q);",
				idx_name, schema_set->table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_F_MATCH:
		case RSDB_SCHEMA_KEY_F_CASCADE:
			break;
	}

	if(!EmptyString(buf))
		dlink_add_tail_alloc(my_strdup(buf), (is_key ? key_data : table_data));
}

