/* src/rsdb_pgsql.c
 *   Contains the code for the postgresql database backend.
 *
 * Copyright (C) 2006-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2006-2007 Aaron Sethman <androsyn@ratbox.org>
 * Copyright (C) 2006-2007 ircd-ratbox development team
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
#include <libpq-fe.h>
#include "rsdb.h"
#include "rserv.h"
#include "conf.h"
#include "log.h"

#define RSDB_MAXCOLS			30
#define RSDB_MAX_RECONNECT_TIME		30

PGconn *rsdb_database;
int rsdb_doing_transaction;

static int rsdb_connect(int initial);

/* rsdb_init()
 */
void
rsdb_init(void)
{
	if(EmptyString(config_file.db_name) || EmptyString(config_file.db_host) ||
	   EmptyString(config_file.db_username) || EmptyString(config_file.db_password))
	{
		die(0, "Missing conf options in database {};");
	}

	rsdb_connect(1);
}

/* rsdb_connect()
 * attempts to connect to the postgresql database
 *
 * inputs	- initial, set if we're starting up
 * outputs	- 0 on success, > 0 on fatal error, < 0 on non-fatal error
 * side effects - connection to the postgresql database is attempted
 */
static int
rsdb_connect(int initial)
{

	rsdb_database = PQsetdbLogin(config_file.db_host, NULL, NULL, NULL, 
	                             config_file.db_name, config_file.db_username, 
	                             config_file.db_password);

	if(rsdb_database != NULL && PQstatus(rsdb_database) == CONNECTION_OK)
		return 0;

	/* all errors on startup are fatal */
	if(initial)
		die(0, "Unable to connect to postgresql database: %s",
			PQerrorMessage(rsdb_database));

	switch(PQstatus(rsdb_database))
	{
		case CONNECTION_BAD:
			return -1;

		default:
			return 1;
	}

	/* NOTREACHED */
	return 1;
}

void
rsdb_shutdown(void)
{
	PQfinish(rsdb_database);
}

static void
rsdb_try_reconnect(void)
{
	time_t expire_time = CURRENT_TIME + RSDB_MAX_RECONNECT_TIME;

	mlog("Warning: unable to connect to database, stopping all functions until we recover");

	while(CURRENT_TIME < expire_time)
	{
		PQfinish(rsdb_database);
		if(!rsdb_connect(0))
			return;

		my_sleep(1, 0);
		set_time();
	}

	die(0, "Unable to connect to postgresql database: %s", PQerrorMessage(rsdb_database));
}

/* rsdb_handle_connerror()
 * Handles a connection error from the database
 *
 * inputs	- result pointer, sql command to execute
 * outputs	-
 * side effects - will attempt to deal with non-fatal errors, and reexecute
 *		  the query if it can
 */
static void
rsdb_handle_connerror(PGresult **rsdb_result, const char *buf)
{
	if(rsdb_doing_transaction)
	{
		mlog("fatal error: problem with db file during transaction: %s",
			PQerrorMessage(rsdb_database));
		die(0, "problem with db file");
		return;
	}
	
	switch(PQstatus(rsdb_database))
	{
		case CONNECTION_BAD:
			PQreset(rsdb_database);

			if(PQstatus(rsdb_database) != CONNECTION_OK)
				rsdb_try_reconnect();

			break;

		default:
			mlog("fatal error: problem with db file: %s",
				PQerrorMessage(rsdb_database));
			die(0, "problem with db file");
			return;
	}

	/* connected back successfully */
	if(buf)
	{
		if((*rsdb_result = PQexec(rsdb_database, buf)) == NULL)
		{
			mlog("fatal error: problem with db file: %s",
				PQerrorMessage(rsdb_database));
			die(0, "problem with db file");
		}
	}
}

const char *
rsdb_quote(const char *src)
{
	static char buf[BUFSIZE*4];
	unsigned long length;

	length = strlen(src);

	if(length >= (sizeof(buf) / 2))
		die(0, "length problem compiling sql statement");

	PQescapeString(buf, src, length);
	return buf;
}

void
rsdb_exec(rsdb_callback cb, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	static const char *coldata[RSDB_MAXCOLS+1];
	PGresult *rsdb_result;
	va_list args;
	unsigned int field_count, row_count;
	int i;
	int cur_row;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem compiling sql statement: %s", buf);
		die(0, "length problem compiling sql statement");
	}

	if((rsdb_result = PQexec(rsdb_database, buf)) == NULL)
		rsdb_handle_connerror(&rsdb_result, buf);

	switch(PQresultStatus(rsdb_result))
	{
		case PGRES_FATAL_ERROR:
		case PGRES_BAD_RESPONSE:
		case PGRES_EMPTY_QUERY: /* i'm gonna guess this is bad for us too */
			mlog("fatal error: problem with db file: %s",
				PQresultErrorMessage(rsdb_result));
			die(0, "problem with db file");
			break;
		default:
			break;
	}

	field_count = PQnfields(rsdb_result);
	row_count = PQntuples(rsdb_result);

	if(field_count > RSDB_MAXCOLS)
		die(0, "too many columns in result set -- contact the ratbox team");

	if(!field_count || !row_count || !cb)
	{
		PQclear(rsdb_result);
		return;
	}		
	
	for(cur_row = 0; cur_row < row_count; cur_row++)
	{
		for(i = 0; i < field_count; i++)
		{
			coldata[i] = PQgetvalue(rsdb_result, cur_row, i);
		}
		coldata[i] = NULL;

		(cb)((int) field_count, coldata);
	}
	PQclear(rsdb_result);
}

void
rsdb_exec_insert(unsigned int *insert_id, const char *table_name, const char *field_name, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	static struct rsdb_table data;
	va_list args;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem compiling sql statement: %s", buf);
		die(0, "length problem compiling sql statement");
	}

	rsdb_exec(NULL, "%s", buf);

	rsdb_exec_fetch(&data, "SELECT currval(pg_get_serial_sequence('%s', '%s'))",
			table_name, field_name);

	if(data.row_count == 0)
	{
		mlog("fatal error: SELECT currval(pg_get_serial_sequence('%s', '%s')) returned 0 rows",
			table_name, field_name);
		die(0, "problem with db file");
	}

	*insert_id = atoi(data.row[0][0]);

	rsdb_exec_fetch_end(&data);
}

void
rsdb_exec_fetch(struct rsdb_table *table, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	PGresult *rsdb_result;
	va_list args;
	int i, j;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem compiling sql statement: %s", buf);
		die(0, "length problem compiling sql statement");
	}

	if((rsdb_result = PQexec(rsdb_database, buf)) == NULL)
		rsdb_handle_connerror(&rsdb_result, buf);

	switch(PQresultStatus(rsdb_result))
	{
		case PGRES_FATAL_ERROR:
		case PGRES_BAD_RESPONSE:
		case PGRES_EMPTY_QUERY: /* i'm gonna guess this is bad for us too */
			mlog("fatal error: problem with db file: %s",
				PQresultErrorMessage(rsdb_result));
			die(0, "problem with db file");
		default:
			break;
	}

	
	table->row_count = PQntuples(rsdb_result);
	table->col_count = PQnfields(rsdb_result);
	table->arg = rsdb_result;

	if(!table->row_count || !table->col_count)
	{
		table->row = NULL;
		return;
	}

	table->row = my_malloc(sizeof(char **) * table->row_count);

	for(i = 0; i < table->row_count; i++)
	{
		table->row[i] = my_malloc(sizeof(char *) * table->col_count);

		for(j = 0; j < table->col_count; j++)
		{
			table->row[i][j] = PQgetvalue(rsdb_result, i, j);
		}
	}
}

void
rsdb_exec_fetch_end(struct rsdb_table *table)
{
	rsdb_common_fetch_end(table);
	PQclear(table->arg);
}

void
rsdb_transaction(rsdb_transtype type)
{
	if(type == RSDB_TRANS_START)
	{
		rsdb_exec(NULL, "START TRANSACTION");
		rsdb_doing_transaction = 1;
	}
	else if(type == RSDB_TRANS_END)
	{
		rsdb_exec(NULL, "COMMIT");
		rsdb_doing_transaction = 0;
	}
}

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
	int j;

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
				rsdb_exec_fetch(&data, "SELECT column_name FROM information_schema.columns WHERE table_name='%Q' AND column_name='%Q'",
						schema_set->table_name, schema[i].name);

				if(data.row_count == 0)
					rsdb_schema_generate_element(schema_set->table_name, &schema[i], &table_data, &key_data);

				rsdb_exec_fetch_end(&data);

				break;

			case RSDB_SCHEMA_KEY_PRIMARY:
				field_list = rsdb_schema_split_key(schema[i].name);

				/* build a sql statement, to grab the count of elements in our primary key
				 * for the table, that match any of the columns specified.  The count value 
				 * returned should equal the number of fields we're searching for.
				 */
				rs_snprintf(buf, sizeof(buf), "SELECT COUNT(ccu.column_name) FROM information_schema.constraint_column_usage ccu "
							"JOIN information_schema.table_constraints tc "
							"ON ccu.constraint_name=tc.constraint_name "
							"WHERE tc.constraint_type='PRIMARY KEY' AND ccu.table_name='%Q'",
						schema_set->table_name);

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
					/* find the name of the constraint, and drop it specifically */
					rsdb_exec_fetch(&data, "SELECT tc.constraint_name FROM information_schema.table_constraints tc "
								"WHERE tc.constraint_type='PRIMARY KEY' AND tc.table_name='%Q'",
							schema_set->table_name);

					for(j = 0; j < data.row_count; j++)
					{
						rs_snprintf(buf, sizeof(buf), "ALTER TABLE %Q DROP CONSTRAINT %Q CASCADE;",
							schema_set->table_name, data.row[j][0]);
						dlink_add_alloc(my_strdup(buf), &key_data);
					}

					rsdb_exec_fetch_end(&data);

					/* add in the sql for the primary key */
					rsdb_schema_generate_element(schema_set->table_name, &schema[i], &table_data, &key_data);
				}

				break;

			case RSDB_SCHEMA_KEY_UNIQUE:
				field_list = rsdb_schema_split_key(schema[i].name);

				/* build a sql statement, to grab the count of elements in our primary key
				 * for the table, that match any of the columns specified.  The count value 
				 * returned should equal the number of fields we're searching for.
				 */
				rs_snprintf(buf, sizeof(buf), "SELECT COUNT(ccu.column_name) FROM information_schema.constraint_column_usage ccu "
							"JOIN information_schema.table_constraints tc "
							"ON ccu.constraint_name=tc.constraint_name "
							"WHERE tc.constraint_type='UNIQUE' AND ccu.table_name='%Q'",
						schema_set->table_name);

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
					rsdb_schema_generate_element(schema_set->table_name, &schema[i], &table_data, &key_data);

				break;


			/* I never found a nice way to check the indexes in pgsql, so just attempt to find
			 * an index with a name of what is appropriate (e.g. an index for the table bans
			 * on the field chname, should be called bans_chname_idx), and leave it at that.
			 */
			case RSDB_SCHEMA_KEY_INDEX:
				field_list = rsdb_schema_split_key(schema[i].name);

				rs_snprintf(buf, sizeof(buf), "%s_", schema_set->table_name);

				DLINK_FOREACH(ptr, field_list->head)
				{
					strlcat(buf, (char *) ptr->data, sizeof(buf));
					strlcat(buf, "_", sizeof(buf));
				}

				strlcat(buf, "idx", sizeof(buf));

				rsdb_exec_fetch(&data, "SELECT COUNT(*) FROM pg_catalog.pg_class WHERE relname='%Q'", buf);

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
					rsdb_schema_generate_element(schema_set->table_name, &schema[i], &table_data, &key_data);

				break;

			case RSDB_SCHEMA_KEY_F_MATCH:
			case RSDB_SCHEMA_KEY_F_CASCADE:
				break;
		}
	}

	rsdb_schema_debug(schema_set->table_name, &table_data, &key_data, 0);
}

void
rsdb_schema_generate_element(const char *table_name, struct rsdb_schema *schema_element,
				dlink_list *table_data, dlink_list *key_data)
{
	static char buf[BUFSIZE];
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
				table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_UNIQUE:
			is_key = 1;
			snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD UNIQUE(%s);",
				table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_INDEX:
		{
			char lbuf[BUFSIZE];
			dlink_list *field_list;
			dlink_node *ptr;

			is_key = 1;

			field_list = rsdb_schema_split_key(schema_element->name);

			snprintf(lbuf, sizeof(lbuf), "%s_", table_name);

			DLINK_FOREACH(ptr, field_list->head)
			{
				strlcat(lbuf, (char *) ptr->data, sizeof(lbuf));
				strlcat(lbuf, "_", sizeof(lbuf));
			}

			strlcat(lbuf, "idx", sizeof(lbuf));

			snprintf(buf, sizeof(buf), "CREATE INDEX %s ON %s (%s);",
				lbuf, table_name, schema_element->name);
			break;
		}

		case RSDB_SCHEMA_KEY_F_MATCH:
#if 0
			is_key = 1;
			snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD FOREIGN KEY(%s) REFERENCES %s MATCH FULL;",
				table_name, schema_element->name, schema_element->def);
#endif
			break;

		case RSDB_SCHEMA_KEY_F_CASCADE:
#if 0
			is_key = 1;
			snprintf(buf, sizeof(buf), "ALTER TABLE %s ADD FOREIGN KEY(%s) REFERENCES %s ON DELETE CASCADE;",
				table_name, schema_element->name, schema_element->def);
#endif
			break;
	}

	if(!EmptyString(buf))
		dlink_add_tail_alloc(my_strdup(buf), (is_key ? key_data : table_data));
}

