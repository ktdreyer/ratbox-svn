/* src/rsdb_common.c
 *   Contains the code that is common through all databases.
 *
 * Copyright (C) 2006-2008 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2006-2008 Aaron Sethman <androsyn@ratbox.org>
 * Copyright (C) 2006-2008 ircd-ratbox development team
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

/* rsdb_common_fetch_end()
 * free()'s the memory after rsdb_exec_fetch()
 *
 * inputs	- table entry used for rsdb_exec_fetch()
 * outputs	-
 * side effects	- memory allocated for the table entry is free()'d
 */
void
rsdb_common_fetch_end(struct rsdb_table *table)
{
	int i;

	for(i = 0; i < table->row_count; i++)
	{
		my_free(table->row[i]);
	}

	my_free(table->row);
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
		rsdb_schema_generate_element(schema_set->table_name, &schema[i], &table_data, &key_data);
	}

	rsdb_schema_debug(schema_set->table_name, &table_data, &key_data, 1);
}


