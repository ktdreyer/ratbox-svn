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
	dlink_list key_data;
	int i;
	dlink_node *ptr, *ptr_next;

	memset(&table_data, 0, sizeof(struct _dlink_list));
	memset(&key_data, 0, sizeof(struct _dlink_list));

	schema = schema_set->schema;

	for(i = 0; schema[i].name; i++)
	{
		rsdb_schema_generate_element(schema_set->table_name, &schema[i], &table_data, &key_data);
	}

#if 1
	fprintf(stderr, "CREATE TABLE %s (", schema_set->table_name);
	DLINK_FOREACH_SAFE(ptr, ptr_next, table_data.head)
	{
		fprintf(stderr, "%s", (const char *) ptr->data);

		if(ptr_next)
			fprintf(stderr, ", ");
		else if(!EmptyString(schema_set->primary_key))
			fprintf(stderr, ", PRIMARY KEY(%s)", schema_set->primary_key);
	}
	fprintf(stderr, ");\n");

	DLINK_FOREACH(ptr, key_data.head)
	{
		fprintf(stderr, "%s", (const char *) ptr->data);
		fprintf(stderr, "\n");
	}
#endif
}

