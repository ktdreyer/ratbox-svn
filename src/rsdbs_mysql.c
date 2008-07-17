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
			config_file.db_name, table_name);
	return buf;
}

/* rsdb_schema_check_table()
 * Checks a specific table against the schema
 *
 * inputs	- the schema entry for the table
 * outputs	-
 * side effects	- checks the table against the schema
 */
void
rsdb_schema_check_table(struct rsdb_schema_set *schema_set)
{
	char buf[BUFSIZE*2];
	char lbuf[BUFSIZE];
	struct rsdb_table data;
	struct rsdb_schema *schema;
	dlink_list table_data;
	dlink_list key_data;
	dlink_list *field_list;
	dlink_node *ptr;
	int add_key;
	int i;
	int numval;

	memset(&table_data, 0, sizeof(struct _dlink_list));
	memset(&key_data, 0, sizeof(struct _dlink_list));

	schema = schema_set->schema;

	for(i = 0; schema[i].name; i++)
	{
		add_key = 0;

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
				rsdb_exec_fetch(&data, "SELECT COLUMN_NAME FROM information_schema.columns "
							"WHERE TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q' AND COLUMN_NAME='%Q'",
						config_file.db_name, schema_set->table_name,
						schema[i].name);

				if(data.row_count == 0)
					rsdb_schema_generate_element(schema_set, &schema[i], &table_data, &key_data);

				rsdb_exec_fetch_end(&data);

				break;

			case RSDB_SCHEMA_KEY_PRIMARY:
				field_list = rsdb_schema_split_key(schema[i].name);

				/* build a sql statement, to grab the count of elements in our primary key
				 * for the table, that match any of the columns specified.  The count value
				 * returned should equal the number of fields we're searching for.
				 */
				rs_snprintf(buf, sizeof(buf), "SELECT COUNT(COLUMN_NAME) FROM information_schema.KEY_COLUMN_USAGE AS ccu "
								"JOIN information_schema.TABLE_CONSTRAINTS AS tc "
								"ON tc.TABLE_NAME=ccu.TABLE_NAME "
								"WHERE tc.CONSTRAINT_TYPE='PRIMARY KEY' "
								"AND tc.TABLE_SCHEMA='%Q' AND tc.TABLE_NAME='%Q'",
						config_file.db_name, schema_set->table_name);

				DLINK_FOREACH(ptr, field_list->head)
				{
					/* we want to OR the column names together to find the count of all the
					 * matching entries -- but this OR block itself needs an AND for the first
					 * element to join it to the buffer above
					 */
					if(ptr == field_list->head)
						rs_snprintf(lbuf, sizeof(lbuf), " AND (ccu.COLUMN_NAME='%Q'",
								(char *) ptr->data);
					else
						rs_snprintf(lbuf, sizeof(lbuf), " OR ccu.COLUMN_NAME='%Q'",
								(char *) ptr->data);

					strlcat(buf, lbuf, sizeof(buf));
				}

				/* close the sql brace */
				strlcat(buf, ")", sizeof(buf));

				rsdb_exec_fetch(&data, "%s", buf);

				if(data.row_count == 0)
				{
					mlog("fatal error: SELECT COUNT() returned 0 rows in rsdb_schema_check_table()");
					die(0, "problem with db file");
				}

				/* this field should be the count of all the elements in the primary key
				 * that match the list of fields we're searching for.  Therefore, they
				 * should be equal if the key is correct.
				 */
				if(atoi(data.row[0][0]) != dlink_list_length(field_list))
					add_key++;

				rsdb_exec_fetch_end(&data);

				/* drop any existing primary keys */
				if(add_key)
				{
					rsdb_exec_fetch(&data, "SELECT COUNT(TABLE_NAME) FROM information_schema.TABLE_CONSTRAINTS "
								"WHERE CONSTRAINT_TYPE='PRIMARY KEY' "
								"AND TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q'",
							config_file.db_name, schema_set->table_name);

					if(data.row_count == 0)
					{
						mlog("fatal error: SELECT COUNT() returned 0 rows in rsdb_schema_check_table()");
						die(0, "problem with db file");
					}

					numval = atoi(data.row[0][0]);

					rsdb_exec_fetch_end(&data);

					if(numval)
					{
						rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q DROP PRIMARY KEY", schema_set->table_name);
						dlink_add_alloc(my_strdup(buf), &key_data);
					}

					rsdb_schema_generate_element(schema_set, &schema[i], &table_data, &key_data);
				}

				break;

			case RSDB_SCHEMA_KEY_UNIQUE:
				field_list = rsdb_schema_split_key(schema[i].name);

				/* build a sql statement, to grab the count of elements in our primary key
				 * for the table, that match any of the columns specified.  The count value
				 * returned should equal the number of fields we're searching for.
				 */
				rs_snprintf(buf, sizeof(buf), "SELECT COUNT(COLUMN_NAME) FROM information_schema.KEY_COLUMN_USAGE AS ccu "
								"JOIN information_schema.TABLE_CONSTRAINTS AS tc "
								"ON tc.TABLE_NAME=ccu.TABLE_NAME "
								"WHERE tc.CONSTRAINT_TYPE='UNIQUE' "
								"AND tc.TABLE_SCHEMA='%Q' AND tc.TABLE_NAME='%Q'",
						config_file.db_name, schema_set->table_name);

				DLINK_FOREACH(ptr, field_list->head)
				{
					/* we want to OR the column names together to find the count of all the
					 * matching entries -- but this OR block itself needs an AND for the first
					 * element to join it to the buffer above
					 */
					if(ptr == field_list->head)
						rs_snprintf(lbuf, sizeof(lbuf), " AND (ccu.COLUMN_NAME='%Q'",
								(char *) ptr->data);
					else
						rs_snprintf(lbuf, sizeof(lbuf), " OR ccu.COLUMN_NAME='%Q'",
								(char *) ptr->data);

					strlcat(buf, lbuf, sizeof(buf));
				}

				/* close the sql brace */
				strlcat(buf, ")", sizeof(buf));

				rsdb_exec_fetch(&data, "%s", buf);

				if(data.row_count == 0)
				{
					mlog("fatal error: SELECT COUNT() returned 0 rows in rsdb_schema_check_table()");
					die(0, "problem with db file");
				}

				/* this field should be the count of all the elements in the primary key
				 * that match the list of fields we're searching for.  Therefore, they
				 * should be equal if the key is correct.
				 */
				if(atoi(data.row[0][0]) != dlink_list_length(field_list))
					add_key++;

				rsdb_exec_fetch_end(&data);

				if(add_key)
					rsdb_schema_generate_element(schema_set, &schema[i], &table_data, &key_data);

				break;

			case RSDB_SCHEMA_KEY_INDEX:
				field_list = rsdb_schema_split_key(schema[i].name);

				rs_snprintf(buf, sizeof(buf), "%s_", schema_set->table_name);

				DLINK_FOREACH(ptr, field_list->head)
				{
					strlcat(buf, (char *) ptr->data, sizeof(buf));
					strlcat(buf, "_", sizeof(buf));
				}

				strlcat(buf, "idx", sizeof(buf));

				rsdb_exec_fetch(&data, "SELECT COUNT(*) FROM information_schema.statistics "
							"WHERE TABLE_SCHEMA='%Q' AND TABLE_NAME='%Q' AND INDEX_NAME='%Q'",
						config_file.db_name, schema_set->table_name, buf);

				if(data.row_count == 0)
				{
					mlog("fatal error: SELECT COUNT() returned 0 rows in rsdb_schema_check_table()");
					die(0, "problem with db file");
				}

				/* if the index exists, presume it is ok */
				if(atoi(data.row[0][0]) == 0)
					add_key++;

				rsdb_exec_fetch_end(&data);

				if(add_key)
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
			snprintf(buf, sizeof(buf), "%s INTEGER AUTO_INCREMENT PRIMARY KEY", schema_element->name);
			break;

		case RSDB_SCHEMA_SERIAL_REF:
			snprintf(buf, sizeof(buf), "%s INTEGER%s%s%s",
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
			snprintf(buf, sizeof(buf), "%s INTEGER UNSIGNED%s%s%s",
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
			/* mysql requires AUTO_INCREMENT columns are defined as PRIMARY KEY when 
			 * they're added, so skip the primary key if this is the case.
			 */
			if(schema_set->has_serial)
				break;

			is_key = 1;
			snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD PRIMARY KEY(%s);",
				schema_set->table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_UNIQUE:
			is_key = 1;
			idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name,
								schema_element->option);

			rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q ADD UNIQUE %Q (%Q);",
				schema_set->table_name, idx_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_INDEX:
			is_key = 1;
			idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name,
								schema_element->option);

			rs_snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD INDEX %s (%s);",
				schema_set->table_name, idx_name, schema_element->name);
			break;

		/* MyISAM tables don't support foreign keys */
		case RSDB_SCHEMA_KEY_F_MATCH:
		case RSDB_SCHEMA_KEY_F_CASCADE:
			break;
	}

	if(!EmptyString(buf))
		dlink_add_tail_alloc(my_strdup(buf), (is_key ? key_data : table_data));
}

