/* src/rsdbs_sqlite.h
 *   Contains the code for schema interactions with the sqlite database backend.
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
#include "log.h"

/* rsdbs_sql_check_table()
 * Returns the SQL for checking whether a table exists
 * 
 * inputs       - table name to check
 * outputs      - SQL
 * side effects - 
 */
const char *
rsdbs_sql_check_table(const char *table_name)
{
	static char buf[BUFSIZE*2];

	rs_snprintf(buf, sizeof(buf), "SELECT tbl_name FROM sqlite_master WHERE type='table' AND tbl_name='%Q'",
			table_name);
	return buf;
}

const char *
rsdbs_sql_drop_key_pri(const char *table_name)
{
	return NULL;
}

const char *
rsdbs_sql_drop_key_unique(const char *table_name, const char *key_name)
{
	return NULL;
}

/* rsdbs_pragma_header_pos()
 * Runs a pragma function, and uses it to work out the position of a given column.
 *
 * inputs	- table to search, pragma function to issue, column to search for
 * outputs	- position of column, -1 if not found, -2 if no results
 * side effects	-
 */
static int
rsdbs_pragma_header_pos(const char *table_name, const char *pragma_str, const char *column_name)
{
	struct rsdb_table data;
	char **res_data;
	int pos = -1;
	int i;

	rsdb_exec_fetch(&data, "PRAGMA %s(%Q)", pragma_str, table_name);

	/* no results, so no column headers either */
	if(data.row_count == 0)
	{
		rsdb_exec_fetch_end(&data);
		return -2;
	}

	/* rsdb_exec_fetch() loaded the results into its columns, however it skipped the column
	 * headers, and we need that information to work out the column position.
	 *
	 * Go back to the raw result set, and hunt for it there..
	 */
	res_data = data.arg;

	for(i = 0; i < data.col_count; i++)
	{
		if(!strcmp(res_data[i], column_name))
		{
			pos = i;
			break;
		}
	}

	rsdb_exec_fetch_end(&data);
	return pos;
}

int
rsdbs_check_column(const char *table_name, const char *column_name)
{
	struct rsdb_table data;
	int pos_name = -1;
	int i;

	pos_name = rsdbs_pragma_header_pos(table_name, "table_info", "name");

	/* didn't find a column caled 'name' -- so we have no idea where the
	 * column names are held..
	 */
	if(pos_name < 0)
	{
		mlog("fatal error: problem with db file: PRAGMA table_info() did not have a 'name' column");
		die(0, "problem with db file");
	}

	/* load the result set */
	rsdb_exec_fetch(&data, "PRAGMA table_info(%Q)", table_name);

	/* At this point, we know which column in the result set has the
	 * name of the column within the table we are looking for (pos_name).
	 *
	 * So now, hunt through the rows in the result set, checking if we
	 * can find the column we are hunting for in the results..
	 */
	for(i = 0; i < data.row_count; i++)
	{
		/* found it! */
		if(!strcmp(data.row[i][pos_name], column_name))
		{
			rsdb_exec_fetch_end(&data);
			return 1;
		}
	}

	rsdb_exec_fetch_end(&data);
	return 0;
}

static int
rsdbs_check_key_index_list(const char *table_name, const char *key_list_str, rsdbs_schema_key_option option)
{
	struct rsdb_table data;
	const char *idx_name;
	int pos_name = -1;
	int pos_unique = -1;
	int i;

	pos_name = rsdbs_pragma_header_pos(table_name, "index_list", "name");
	pos_unique = rsdbs_pragma_header_pos(table_name, "index_list", "unique");

	/* didn't find a column caled 'name' or unique -- so we have no idea where the
	 * index names are held..
	 */
	if(pos_name == -1 || pos_unique == -1)
	{
		mlog("fatal error: problem with db file: PRAGMA index_list() did not have a 'name'/'unique' column");
		die(0, "problem with db file");
	}

	/* no indexes */
	if(pos_name < 0 || pos_unique < 0)
		return 0;

	/* load the result set */
	rsdb_exec_fetch(&data, "PRAGMA index_list(%Q)", table_name);

	/* generate our index name */
	idx_name = rsdbs_generate_key_name(table_name, key_list_str, option);

	/* At this point, we know which column in the result set has the
	 * name of the column within the table we are looking for (pos_name).
	 *
	 * So now, hunt through the rows in the result set, checking if we
	 * can find the column we are hunting for in the results..
	 */
	for(i = 0; i < data.row_count; i++)
	{
		/* found it! */
		if(!strcmp(data.row[i][pos_name], idx_name))
		{
			if((option == RSDB_SCHEMA_KEY_UNIQUE && atoi(data.row[i][pos_unique]) == 1) ||
			   (option == RSDB_SCHEMA_KEY_INDEX && atoi(data.row[i][pos_unique]) == 0))
			{
				rsdb_exec_fetch_end(&data);
				return 1;
			}

			rsdb_exec_fetch_end(&data);
			return 0;
		}
	}

	rsdb_exec_fetch_end(&data);
	return 0;

}

int
rsdbs_check_key_pri(const char *table_name, const char *key_list_str)
{
	struct rsdb_table data;
	dlink_list *field_list;
	int pos_name = -1;
	int pos_pkey = -1;
	int key_count = 0;
	int list_count = 0;
	int i;

	pos_name = rsdbs_pragma_header_pos(table_name, "table_info", "name");
	pos_pkey = rsdbs_pragma_header_pos(table_name, "table_info", "pk");

	/* didn't find a column caled 'name' -- so we have no idea where the
	 * column names are held..
	 */
	if(pos_name < 0 || pos_pkey < 0)
	{
		mlog("fatal error: problem with db file: PRAGMA table_info() did not have a 'name'/'pk' column");
		die(0, "problem with db file");
	}

	/* split up our key */
	field_list = rsdb_schema_split_key(key_list_str);
	list_count = dlink_list_length(field_list);

	/* load the result set */
	rsdb_exec_fetch(&data, "PRAGMA table_info(%Q)", table_name);

	/* Hunt through the result set, finding all elements that are marked PRIMARY KEY,
	 * and knock them out of the field_list one by one.
	 */
	for(i = 0; i < data.row_count; i++)
	{
		if(atoi(data.row[i][pos_pkey]) == 1)
		{
			key_count++;

			dlink_find_string_destroy(data.row[i][pos_name], field_list);
		}
	}

	rsdb_exec_fetch_end(&data);

	/* in the end, field list should be empty, and the number of keys found should match */
	if(dlink_list_length(field_list) == 0 && key_count == list_count)
		return 1;

	return 0;
}

int
rsdbs_check_key_unique(const char *table_name, const char *key_list_str)
{
	return rsdbs_check_key_index_list(table_name, key_list_str, RSDB_SCHEMA_KEY_UNIQUE);
}

int
rsdbs_check_key_index(const char *table_name, const char *key_list_str)
{
	return rsdbs_check_key_index_list(table_name, key_list_str, RSDB_SCHEMA_KEY_INDEX);
}

void
rsdbs_check_deletekey_unique(const char *table_name, dlink_list *key_list, dlink_list *table_data)
{
}

void
rsdbs_check_deletekey_index(const char *table_name, dlink_list *key_list, dlink_list *table_data)
{
}

const char *
rsdbs_sql_create_col(struct rsdb_schema_set *schema_set, struct rsdbs_schema_col *schema_element,
			int alter_table)
{
	static char buf[BUFSIZE*2];
	static char empty_string[] = "";
	char *alter_table_str = empty_string;

	/* prepare the 'ALTER TABLE .. ADD COLUMN' prefix if required */
	if(alter_table)
	{
		rs_snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD COLUMN ", schema_set->table_name);

		/* create a temporary copy of the buffer in alter_table_str */
		alter_table_str = LOCAL_COPY(buf);
	}

	buf[0] = '\0';

	switch(schema_element->option)
	{
		case RSDB_SCHEMA_SERIAL:
			snprintf(buf, sizeof(buf), "%s%s INTEGER PRIMARY KEY", 
				alter_table_str, schema_element->name);
			break;

		case RSDB_SCHEMA_SERIAL_REF:
			snprintf(buf, sizeof(buf), "%s%s INTEGER%s%s%s",
				alter_table_str, schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_BOOLEAN:
			snprintf(buf, sizeof(buf), "%s%s INTEGER", alter_table_str, schema_element->name);
			break;

		case RSDB_SCHEMA_INT:
			snprintf(buf, sizeof(buf), "%s%s INTEGER%s%s%s",
				alter_table_str, schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_UINT:
			snprintf(buf, sizeof(buf), "%s%s INTEGER%s%s%s",
				alter_table_str, schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_VARCHAR:
			snprintf(buf, sizeof(buf), "%s%s TEXT%s%s%s",
				alter_table_str, schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_CHAR:
			snprintf(buf, sizeof(buf), "%s%s TEXT%s%s%s",
				alter_table_str, schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_TEXT:
			snprintf(buf, sizeof(buf), "%s%s TEXT%s%s%s",
				alter_table_str, schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;
	}

	if(!EmptyString(buf))
		return buf;

	return NULL;
}

const char *
rsdbs_sql_create_key(struct rsdb_schema_set *schema_set, struct rsdbs_schema_key *schema_element)
{
	static char buf[BUFSIZE*2];
	const char *idx_name;

	buf[0] = '\0';

	idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name, schema_element->option);

	switch(schema_element->option)
	{
		case RSDB_SCHEMA_KEY_PRIMARY:
			if(schema_set->has_serial)
				break;

			rs_snprintf(buf, sizeof(buf), "CREATE UNIQUE INDEX %Q ON %Q (%Q);",
				idx_name, schema_set->table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_UNIQUE:
			rs_snprintf(buf, sizeof(buf), "CREATE UNIQUE INDEX %Q ON %Q (%Q);",
				idx_name, schema_set->table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_INDEX:
			rs_snprintf(buf, sizeof(buf), "CREATE INDEX %Q ON %Q (%Q);",
				idx_name, schema_set->table_name, schema_element->name);
			break;

		/* sqlite tables don't properly support foreign keys */
		case RSDB_SCHEMA_KEY_F_MATCH:
		case RSDB_SCHEMA_KEY_F_CASCADE:
			return NULL;
			break;
	}

	if(!EmptyString(buf))
		return buf;

	return NULL;
}

