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

const char *
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

const char *
rsdbs_sql_drop_key_unique(const char *table_name, const char *key_name)
{
	return NULL;
}

int
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
rsdbs_check_key_is(const char *table_name, const char *key_list_str, rsdbs_schema_key_option option)
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

int
rsdbs_check_key_pri(const char *table_name, const char *key_list_str)
{
	return rsdbs_check_key_is(table_name, key_list_str, RSDB_SCHEMA_KEY_PRIMARY);
}

int
rsdbs_check_key_unique(const char *table_name, const char *key_list_str)
{
	return rsdbs_check_key_is(table_name, key_list_str, RSDB_SCHEMA_KEY_UNIQUE);
}

int
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
rsdbs_check_deletekey_unique(const char *table_name, dlink_list *key_list, dlink_list *table_data)
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
			snprintf(buf, sizeof(buf), "%s%s SERIAL", alter_table_str, schema_element->name);
			break;

		case RSDB_SCHEMA_SERIAL_REF:
			snprintf(buf, sizeof(buf), "%s%s BIGINT%s%s%s",
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
			snprintf(buf, sizeof(buf), "%s%s INTEGER%s%s%s",
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
			snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD PRIMARY KEY(%s)",
				schema_set->table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_UNIQUE:
			idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name, 
								schema_element->option);

			rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q ADD CONSTRAINT %Q UNIQUE (%Q)",
				schema_set->table_name, idx_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_INDEX:
			idx_name = rsdbs_generate_key_name(schema_set->table_name, schema_element->name, 
								schema_element->option);

			rs_snprintf(buf, sizeof(buf), "CREATE INDEX %Q ON %Q (%Q)",
				idx_name, schema_set->table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_F_MATCH:
		case RSDB_SCHEMA_KEY_F_CASCADE:
			return NULL;
			break;
	}

	if(!EmptyString(buf))
		return buf;

	return NULL;
}
