/* src/rsdbs_mysql.c
 *   Contains the code for schema interactions with the mysql database backend.
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
#include "conf.h"
#include "log.h"

/* rsdbs_sql_check_table()
 * Returns the SQL for checking whether a table exists
 *
 * inputs	- table name to check
 * outputs	- SQL
 * side effects - 
 */
const char *
rsdbs_sql_check_table(const char *table_name)
{
	static char buf[BUFSIZE*2];

	rs_snprintf(buf, sizeof(buf), 
			"SELECT TABLE_NAME FROM information_schema.tables "
			"WHERE TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q'",
			rsdb_conf.db_name, table_name);
	return buf;
}

const char *
rsdbs_sql_drop_key_pri(const char *table_name)
{
	static char buf[BUFSIZE*2];
	struct rsdb_table data;
	const char *buf_ptr = NULL;

	rsdb_exec_fetch(&data, "SELECT TABLE_NAME FROM information_schema.TABLE_CONSTRAINTS "
				"WHERE CONSTRAINT_TYPE='PRIMARY KEY' "
				"AND TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q'",
			rsdb_conf.db_name, table_name);

	if(data.row_count > 0)
	{
		rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q DROP PRIMARY KEY", table_name);
		buf_ptr = buf;
	}

	rsdb_exec_fetch_end(&data);

	return buf_ptr;
}

const char *
rsdbs_sql_drop_key_unique(const char *table_name, const char *key_name)
{
	static char buf[BUFSIZE*2];
	struct rsdb_table data;
	const char *buf_ptr = NULL;

	rsdb_exec_fetch(&data, "SELECT CONSTRAINT_NAME FROM information_schema.TABLE_CONSTRAINTS "
				"WHERE CONSTRAINT_TYPE='UNIQUE' "
				"AND TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q' "
				"AND CONSTRAINT_NAME='%Q'",
			rsdb_conf.db_name, table_name, key_name);

	if(data.row_count > 0)
	{
		rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q DROP INDEX %Q", table_name, key_name);
		buf_ptr = buf;
	}

	rsdb_exec_fetch_end(&data);

	return buf_ptr;
}

const char *
rsdbs_sql_drop_key_index(const char *table_name, const char *key_name)
{
	static char buf[BUFSIZE*2];
	struct rsdb_table data;
	const char *buf_ptr = NULL;

	rsdb_exec_fetch(&data, "SELECT INDEX_NAME FROM information_schema.STATISTICS "
				"WHERE NON_UNIQUE=1 AND TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q' "
				"AND INDEX_NAME='%Q'",
			rsdb_conf.db_name, table_name, key_name);

	if(data.row_count > 0)
	{
		rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q DROP INDEX %Q", table_name, key_name);
		buf_ptr = buf;
	}

	rsdb_exec_fetch_end(&data);

	return buf_ptr;
}

int
rsdbs_check_column(const char *table_name, const char *column_name)
{
	struct rsdb_table data;
	int row_count;

	rsdb_exec_fetch(&data, "SELECT COLUMN_NAME FROM information_schema.columns "
				"WHERE TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q' AND COLUMN_NAME='%Q'",
			rsdb_conf.db_name, table_name, column_name);
	row_count = data.row_count;
	rsdb_exec_fetch_end(&data);

	if(row_count > 0)
		return 1;

	return 0;
}

static int
rsdbs_check_key_kcu(const char *table_name, const char *key_list_str, rsdbs_schema_key_option option)
{
	static const char option_str_pri[] = "PRIMARY KEY";
	static const char option_str_unique[] = "UNIQUE";
	char buf[BUFSIZE*2];
	char lbuf[BUFSIZE*2];
	struct rsdb_table data;
	dlink_list *field_list;
	const char *option_str;
	int i;

	if(option == RSDB_SCHEMA_KEY_PRIMARY)
		option_str = option_str_pri;
	else if(option == RSDB_SCHEMA_KEY_UNIQUE)
		option_str = option_str_unique;
	else
		return -1;

	field_list = rsdb_schema_split_key(key_list_str);

	/* grab the names of all columns in our key */
	rs_snprintf(buf, sizeof(buf), "SELECT COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE AS ccu "
					"JOIN information_schema.TABLE_CONSTRAINTS AS tc "
					"ON tc.TABLE_NAME=ccu.TABLE_NAME AND tc.CONSTRAINT_NAME=ccu.CONSTRAINT_NAME "
					"WHERE tc.CONSTRAINT_TYPE='%Q' "
					"AND tc.TABLE_SCHEMA='%Q' AND tc.TABLE_NAME='%Q'",
			option_str, rsdb_conf.db_name, table_name);

	/* if its a PRIMARY KEY there can only be one possible constraint per table, but if it's
	 * UNIQUE then we can have multiple -- so grab the one for our key name
	 */
	if(option == RSDB_SCHEMA_KEY_UNIQUE)
	{
		rs_snprintf(lbuf, sizeof(lbuf), " AND ccu.CONSTRAINT_NAME='%Q'",
				rsdbs_generate_key_name(table_name, key_list_str, option));
		strlcat(buf, lbuf, sizeof(buf));
	}

	rsdb_exec_fetch(&data, "%s", buf);

	/* the SQL above should have returned a row per match -- so the number of elements
	 * in the key should match this
	 */
	if(data.row_count != dlink_list_length(field_list))
	{
		rsdb_exec_fetch_end(&data);
		return 0;
	}

	/* The number of keys is right, so walk through the list of columns we got back and
	 * remove matches from field_list.  If the lists are a match, we should end up with
	 * field_list being empty..
	 */

	for(i = 0; i < data.row_count; i++)
	{
		dlink_find_string_destroy(data.row[i][0], field_list);
	}

	rsdb_exec_fetch_end(&data);

	if(dlink_list_length(field_list) == 0)
		return 1;

	return 0;
}

int
rsdbs_check_key_pri(const char *table_name, const char *key_list_str)
{
	return rsdbs_check_key_kcu(table_name, key_list_str, RSDB_SCHEMA_KEY_PRIMARY);
}

int
rsdbs_check_key_unique(const char *table_name, const char *key_list_str)
{
	return rsdbs_check_key_kcu(table_name, key_list_str, RSDB_SCHEMA_KEY_UNIQUE);
}


int
rsdbs_check_key_index(const char *table_name, const char *key_list_str)
{
	struct rsdb_table data;
	const char *idx_name;
	int row_count;

	idx_name = rsdbs_generate_key_name(table_name, key_list_str, RSDB_SCHEMA_KEY_INDEX);

	rsdb_exec_fetch(&data, "SELECT * FROM information_schema.statistics "
				"WHERE NON_UNIQUE=1 AND TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q' AND INDEX_NAME='%Q'",
			rsdb_conf.db_name, table_name, idx_name);
	row_count = data.row_count;
	rsdb_exec_fetch_end(&data);

	if(row_count > 0)
		return 1;
	
	return 0;
}

void
rsdbs_check_deletekey_unique(const char *table_name, dlink_list *key_list, dlink_list *table_data)
{
	char buf[BUFSIZE*2];
	char lbuf[BUFSIZE*2];
	struct rsdb_table data;
	dlink_node *ptr;
	const char *key_list_str;
	const char *idx_name;
	int i;

	rs_snprintf(buf, sizeof(buf), "SELECT CONSTRAINT_NAME FROM information_schema.TABLE_CONSTRAINTS "
					"WHERE CONSTRAINT_TYPE='UNIQUE' "
					"AND TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q'",
			rsdb_conf.db_name, table_name);

	DLINK_FOREACH(ptr, key_list->head)
	{
		key_list_str = ptr->data;
		idx_name = rsdbs_generate_key_name(table_name, key_list_str, RSDB_SCHEMA_KEY_UNIQUE);

		rs_snprintf(lbuf, sizeof(lbuf), " AND CONSTRAINT_NAME <> '%Q'",	idx_name);
		strlcat(buf, lbuf, sizeof(buf));
	}

	rsdb_exec_fetch(&data, buf);

	/* delete any extra keys we got back.. */
	for(i = 0; i < data.row_count; i++)
	{
		const char *add_sql;

		add_sql  = rsdbs_sql_drop_key_unique(table_name, data.row[i][0]);

		if(add_sql)
			dlink_add_tail_alloc(my_strdup(add_sql), table_data);
	}

	rsdb_exec_fetch_end(&data);
}

void
rsdbs_check_deletekey_index(const char *table_name, dlink_list *key_list, dlink_list *table_data)
{
	char buf[BUFSIZE*2];
	char lbuf[BUFSIZE*2];
	struct rsdb_table data;
	dlink_node *ptr;
	const char *key_list_str;
	const char *idx_name;
	int i;

	rs_snprintf(buf, sizeof(buf), "SELECT DISTINCT INDEX_NAME FROM information_schema.STATISTICS "
					"WHERE NON_UNIQUE=1 AND TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q'",
			rsdb_conf.db_name, table_name);

	DLINK_FOREACH(ptr, key_list->head)
	{
		key_list_str = ptr->data;
		idx_name = rsdbs_generate_key_name(table_name, key_list_str, RSDB_SCHEMA_KEY_INDEX);

		rs_snprintf(lbuf, sizeof(lbuf), " AND INDEX_NAME <> '%Q'", idx_name);
		strlcat(buf, lbuf, sizeof(buf));
	}

	rsdb_exec_fetch(&data, buf);

	/* delete any extra keys we got back.. */
	for(i = 0; i < data.row_count; i++)
	{
		const char *add_sql;

		add_sql  = rsdbs_sql_drop_key_index(table_name, data.row[i][0]);

		if(add_sql)
			dlink_add_tail_alloc(my_strdup(add_sql), table_data);
	}

	rsdb_exec_fetch_end(&data);
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
			snprintf(buf, sizeof(buf), "%s%s INTEGER AUTO_INCREMENT PRIMARY KEY", 
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
			snprintf(buf, sizeof(buf), "%s%s BOOL", alter_table_str, schema_element->name);
			break;

		case RSDB_SCHEMA_INT:
			snprintf(buf, sizeof(buf), "%s%s INTEGER%s%s%s",
				alter_table_str, schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_UINT:
			snprintf(buf, sizeof(buf), "%s%s INTEGER UNSIGNED%s%s%s",
				alter_table_str, schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_VARCHAR:
			snprintf(buf, sizeof(buf), "%s%s VARCHAR(%u)%s%s%s",
				alter_table_str, schema_element->name, schema_element->length,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_CHAR:
			snprintf(buf, sizeof(buf), "%s%s CHAR(%u)%s%s%s",
				alter_table_str, schema_element->name, schema_element->length,
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

	switch(schema_element->option)
	{
		case RSDB_SCHEMA_KEY_PRIMARY:
			/* mysql requires AUTO_INCREMENT columns are defined as PRIMARY KEY when 
			 * they're added, so skip the primary key if this is the case.
			 */
			if(schema_set->has_serial)
				break;

			snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD PRIMARY KEY(%s);",
				schema_set->table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_UNIQUE:
			idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name,
								schema_element->option);

			rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q ADD UNIQUE %Q (%Q);",
				schema_set->table_name, idx_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_INDEX:
			idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name,
								schema_element->option);

			rs_snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD INDEX %s (%s);",
				schema_set->table_name, idx_name, schema_element->name);
			break;

		/* MyISAM tables don't support foreign keys */
		case RSDB_SCHEMA_KEY_F_MATCH:
		case RSDB_SCHEMA_KEY_F_CASCADE:
			return NULL;
	}

	if(!EmptyString(buf))
		return buf;

	return NULL;
}
