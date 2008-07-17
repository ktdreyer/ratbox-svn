/* src/rsdbs_common.c
 *   Contains the code that is common for schema interactions through all databases.
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
#include "rserv.h"
#include "rsdb.h"
#include "rsdbs.h"



/* rsdb_schema_check()
 * Checks the database against the schema
 *
 * inputs	- schema set
 * outputs	-
 * side effects - runs checks for all tables in the schema
 */
void
rsdb_schema_check(struct rsdb_schema_set *schema_set)
{
	struct rsdb_schema *schema_element;
	struct rsdb_table data;
	const char *buf;
	int i, j;

	/* first fill out the serial_name column of the schema_set with the
	 * name of the serial column in the table, if it has one
	 */
	for(i = 0; schema_set[i].table_name; i++)
	{
		/* mark this as off unless we find a serial later */
		schema_set[i].has_serial = 0;

		schema_element = schema_set[i].schema;

		for(j = 0; schema_element[j].name; j++)
		{
			if(schema_element[j].option == RSDB_SCHEMA_SERIAL)
			{
				schema_set[i].has_serial = 1;
				break;
			}
		}
	}

	/* now walk the schema set to check it */
	for(i = 0; schema_set[i].table_name; i++)
	{
		/* run the relevant sql handler to get us the sql to check 
		 * whether the table exists 
		 */
		buf = rsdbs_sql_check_table(schema_set[i].table_name);

		rsdb_exec_fetch(&data, "%s", buf);

		/* table exists, run checks on each field */
		if(data.row_count > 0)
			rsdb_schema_check_table(&schema_set[i]);
		/* table doesn't exist.. just flat generate it */
		else
			rsdb_schema_generate_table(&schema_set[i]);

		rsdb_exec_fetch_end(&data);
	}
}

const char *
rsdbs_generate_key_name(const char *table_name, const char *field_list_text, rsdb_schema_option option)
{
	static char buf[BUFSIZE];
	dlink_list *field_list;
	dlink_node *ptr;

	field_list = rsdb_schema_split_key(field_list_text);

	snprintf(buf, sizeof(buf), "%s_", table_name);

	DLINK_FOREACH(ptr, field_list->head)
	{
		strlcat(buf, (char *) ptr->data, sizeof(buf));
		strlcat(buf, "_", sizeof(buf));
	}

	if(option == RSDB_SCHEMA_KEY_PRIMARY)
		strlcat(buf, "prikey", sizeof(buf));
	else if(option == RSDB_SCHEMA_KEY_UNIQUE)
		strlcat(buf, "unique", sizeof(buf));
	else if(option == RSDB_SCHEMA_KEY_INDEX)
		strlcat(buf, "idx", sizeof(buf));
	else
		strlcat(buf, "unknown", sizeof(buf));

	return buf;
}

void
rsdb_schema_debug(const char *table_name, dlink_list *table_data, dlink_list *key_data, int create)
{
	dlink_node *ptr, *next_ptr;

	if(create && dlink_list_length(table_data))
	{
		fprintf(stdout, "CREATE TABLE %s (", table_name);

		DLINK_FOREACH_SAFE(ptr, next_ptr, table_data->head)
		{
			fprintf(stdout, "%s", (const char *) ptr->data);

			if(next_ptr)
				fprintf(stdout, ", ");
		}

		fprintf(stdout, ");\n");
	}
	else
	{
		DLINK_FOREACH(ptr, table_data->head)
		{
			fprintf(stdout, "ALTER TABLE %s ADD COLUMN %s;\n", table_name, (const char *) ptr->data);
		}
	}

	DLINK_FOREACH(ptr, key_data->head)
	{
		fprintf(stdout, "%s\n", (const char *) ptr->data);
	}

}

/* rsdb_schema_split_key()
 * Splits a key value into its individual parts
 *
 * inputs	- comma delimited field string
 * outputs	- linked list of fields
 * side effects	-
 */
struct _dlink_list *
rsdb_schema_split_key(const char *key_fields)
{
	static dlink_list field_list = { NULL, NULL, 0 };
	dlink_node *ptr, *next_ptr;
	char *key;
	char *p, *s, *q;

	DLINK_FOREACH_SAFE(ptr, next_ptr, field_list.head)
	{
		my_free(ptr->data);
		dlink_destroy(ptr, &field_list);
	}

	key = LOCAL_COPY(key_fields);

	for(s = key; !EmptyString(s); s = p)
	{
		/* strip leading spaces */
		while(*s == ' ')
			s++;

		/* point p to the next field */
		if((p = strchr(s, ',')) != NULL)
			*p++ = '\0';

		/* strip out any trailing spaces */
		if((q = strchr(s, ' ')) != NULL)
			*q = '\0';

		dlink_add_alloc(my_strdup(s), &field_list);
	}

	return &field_list;
}

/* rsdb_schema_generate_table()
 * Generates the sql for an entire table without checking schema
 *
 * inputs	- schema set
 * outputs	-
 * side effects - generates the sql for the relevant schema set
 */
void
rsdb_schema_generate_table(struct rsdb_schema_set *schema_set)
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
		rsdb_schema_generate_element(schema_set, &schema[i], &table_data, &key_data);
	}

	rsdb_schema_debug(schema_set->table_name, &table_data, &key_data, 1);
}


