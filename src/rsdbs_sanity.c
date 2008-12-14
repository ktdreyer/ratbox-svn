/* src/rsdbs_sanity.c
 *   Contains the code to run sanity checks on a schema.
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

/* rsdbs_san_multi_serial()
 * Test a schema for the existence of multiple SERIAL fields
 *
 * inputs	- schema set
 * outputs	- 1 if found, otherwise 0
 * side effects	-
 */
static int
rsdbs_san_multi_serial(struct rsdb_schema_set *schema_set)
{
	struct rsdbs_schema_col *schema_col;
	const char *serial_field = NULL;
	int i;

	schema_col = schema_set->schema_col;

	if(!schema_col)
		return 0;

	for(i = 0; schema_col[i].name; i++)
	{
		if(schema_col[i].option == RSDB_SCHEMA_SERIAL)
		{
			/* already have a serial field! */
			if(serial_field)
				return 1;
			else
				serial_field = schema_col[i].name;
		}
	}

	return 0;
}

/* rsdbs_san_multi_primary_key()
 * Test a schema for the existence of multiple PRIMARY KEY
 *
 * inputs	- schema set
 * outputs	- 1 if found, otherwise 0
 * side effects	-
 */
static int
rsdbs_san_multi_primary_key(struct rsdb_schema_set *schema_set)
{
	struct rsdbs_schema_key *schema_key;
	int pkey_count = 0;
	int i;

	schema_key = schema_set->schema_key;

	if(schema_key)
	{
		for(i = 0; schema_key[i].name; i++)
		{
			if(schema_key[i].option == RSDB_SCHEMA_KEY_PRIMARY)
				pkey_count++;
		}

		if(pkey_count > 1)
			return 1;
	}

	return 0;
}

/* rsdbs_san_non_serial_primary_key()
 * Test a schema set for a primary key containing a field that is not SERIAL
 *
 * inputs	- schema set
 * outputs	- 1 if found, otherwise 0
 * side effects	-
 */
static int
rsdbs_san_non_auto_primary_key(struct rsdb_schema_set *schema_set)
{
	struct rsdbs_schema_col *schema_col;
	struct rsdbs_schema_key *schema_key;
	const char *serial_field = NULL;
	int i;
	dlink_list *field_list;
	dlink_node *ptr;

	schema_col = schema_set->schema_col;
	schema_key = schema_set->schema_key;

	/* try and find a SERIAL field */
	if(schema_col)
	{
		for(i = 0; schema_col[i].name; i++)
		{
			if(schema_col[i].option != RSDB_SCHEMA_SERIAL)
				continue;

			serial_field = schema_col[i].name;
		}
	}

	/* no keys at all */
	if(!schema_key)
	{
		/* but we do have a serial column? */
		if(serial_field)
			return 1;
		else
			return 0;
	}

	for(i = 0; schema_key[i].name; i++)
	{
		if(schema_key[i].option != RSDB_SCHEMA_KEY_PRIMARY)
			continue;

		/* no serial field at all.. it must be different */
		if(!serial_field)
			return 1;

		field_list = rsdb_schema_split_key(schema_key[i].name);

		/* primary key with more than one field, must be different.. */
		if(dlink_list_length(field_list) != 1)
			return 1;

		ptr = field_list->head;

		/* serial field from the column set is different from the primary key */
		if(strcasecmp(serial_field, (const char *) (ptr->data)))
			return 1;
	}

	return 0;
}

/* rsdbs_san_check()
 * Checks a schema_set array for sanity
 *
 * inputs	- schema set array
 * outputs	- 0 if sane, otherwise 1
 * side effects	-
 */
int
rsdbs_san_check(struct rsdb_schema_set *schema_set)
{
	int i;

	for(i = 0; schema_set[i].table_name; i++)
	{
		if(rsdbs_san_multi_serial(&schema_set[i]))
			;

		if(rsdbs_san_multi_primary_key(&schema_set[i]))
			;

		if(rsdbs_san_non_auto_primary_key(&schema_set[i]))
			;
	}

	return 0;
}

