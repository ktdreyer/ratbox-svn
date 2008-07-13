/* src/rsdb_sqlite.h
 *   Contains the code for the sqlite database backend.
 *
 * Copyright (C) 2003-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2007 ircd-ratbox development team
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
#include "rserv.h"
#include "log.h"
#include "schema-sqlite.h"

/* build sqlite, so use local version */
#ifdef SQLITE_BUILD
#include "sqlite3.h"
#else
#include <sqlite3.h>
#endif

struct sqlite3 *rserv_db;

/* rsdb_init()
 */
void
rsdb_init(void)
{
	if(sqlite3_open(DB_PATH, &rserv_db))
	{
		die(0, "Failed to open db file: %s", sqlite3_errmsg(rserv_db));
	}
}

void
rsdb_shutdown(void)
{
	if(rserv_db)
		sqlite3_close(rserv_db);
}

const char *
rsdb_quote(const char *src)
{
	static char buf[BUFSIZE*4];
	char *p = buf;

	/* cheap and dirty length check.. */
	if(strlen(src) >= (sizeof(buf) / 2))
		return NULL;

	while(*src)
	{
		if(*src == '\'')
			*p++ = '\'';

		*p++ = *src++;
	}

	*p = '\0';
	return buf;
}

static int
rsdb_callback_func(void *cbfunc, int argc, char **argv, char **colnames)
{
	rsdb_callback cb = cbfunc;
	(cb)(argc, (const char **) argv);
	return 0;
}

void
rsdb_exec(rsdb_callback cb, const char *format, ...)
{
	static char errmsg_busy[] = "Database file locked";
	static char buf[BUFSIZE*4];
	va_list args;
	char *errmsg;
	int errcount = 0;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die(0, "problem with compiling sql statement");
	}

tryexec:
	if((i = sqlite3_exec(rserv_db, buf, (cb ? rsdb_callback_func : NULL), cb, &errmsg)))
	{
		switch(i)
		{
			case SQLITE_BUSY:
				/* sleep for upto 5 seconds in 10 iterations
				 * to try and get through..
				 */
				errcount++;

				if(errcount <= 10)
				{
					my_sleep(0, 500000);
					goto tryexec;
				}

				errmsg = errmsg_busy;					
				/* otherwise fall through */

			default:
				mlog("fatal error: problem with db file: %s", errmsg);
				die(0, "problem with db file");
				break;
		}
	}
}

void
rsdb_exec_insert(unsigned int *insert_id, const char *table_name, const char *field_name, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	va_list args;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die(0, "problem with compiling sql statement");
	}

	rsdb_exec(NULL, "%s", buf);

	*insert_id = (unsigned int) sqlite3_last_insert_rowid(rserv_db);
}

void
rsdb_exec_fetch(struct rsdb_table *table, const char *format, ...)
{
	static char errmsg_busy[] = "Database file locked";
	static char buf[BUFSIZE*4];
	va_list args;
	char *errmsg;
	char **data;
	int pos;
	int errcount = 0;
	int i, j;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die(0, "problem with compiling sql statement");
	}

tryexec:
	if((i = sqlite3_get_table(rserv_db, buf, &data, &table->row_count, &table->col_count, &errmsg)))
	{
		switch(i)
		{
			case SQLITE_BUSY:
				/* sleep for upto 5 seconds in 10 iterations
				 * to try and get through..
				 */
				errcount++;

				if(errcount <= 10)
				{
					my_sleep(0, 500000);
					goto tryexec;
				}
					
				errmsg = errmsg_busy;					
				/* otherwise fall through */

			default:
				mlog("fatal error: problem with db file: %s", errmsg);
				die(0, "problem with db file");
				break;
		}
	}

	/* we need to be able to free data afterward */
	table->arg = data;

	if(table->row_count == 0)
	{
		table->row = NULL;
		return;
	}

	/* sqlite puts the column names as the first row */
	pos = table->col_count;
	table->row = my_malloc(sizeof(char **) * table->row_count);
	for(i = 0; i < table->row_count; i++)
	{
		table->row[i] = my_malloc(sizeof(char *) * table->col_count);

		for(j = 0; j < table->col_count; j++)
		{
			table->row[i][j] = data[pos++];
		}
	}
}

void
rsdb_exec_fetch_end(struct rsdb_table *table)
{
	rsdb_common_fetch_end(table);
	sqlite3_free_table((char **) table->arg);
}

void
rsdb_transaction(rsdb_transtype type)
{
	if(type == RSDB_TRANS_START)
		rsdb_exec(NULL, "BEGIN TRANSACTION");
	else if(type == RSDB_TRANS_END)
		rsdb_exec(NULL, "COMMIT TRANSACTION");
}


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

static int
rsdbs_check_column(const char *table_name, const char *column_name)
{
	struct rsdb_table data;
	char **res_data;
	int pos_name = -1;
	int i;

	rsdb_exec_fetch(&data, "PRAGMA table_info(%Q)", table_name);

	/* the rsdb_exec_fetch() loaded the result set of columns, however
	 * this is purely the results, without any column headers.
	 *
	 * Because we are using a PRAGMA rather than a SELECT, it's possible
	 * the results here could be in any order.  We therefore need the
	 * column headers to work out which column is which.
	 *
	 * We are looking for a column called 'name' in the result set, so
	 * hunt through to work out which position it is at.
	 */
	res_data = data.arg;

	for(i = 0; i < data.col_count; i++)
	{
		if(!strcmp(res_data[i], "name"))
		{
			pos_name = i;
			break;
		}
	}

	/* didn't find a column caled 'name' -- so we have no idea where the
	 * column names are held..
	 */
	if(pos_name < 0)
	{
		mlog("fatal error: problem with db file: PRAGMA table_info() did not have a 'name' column");
		die(0, "problem with db file");
	}

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

void
rsdb_schema_check_table(struct rsdb_schema_set *schema_set)
{
	struct rsdb_schema *schema;
	dlink_list table_data;
	dlink_list key_data;
	int add_key;
	int i;

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
				if(!rsdbs_check_column(schema_set->table_name, schema[i].name))
					rsdb_schema_generate_element(schema_set->table_name, &schema[i], 
									&table_data, &key_data);
				break;

			case RSDB_SCHEMA_KEY_PRIMARY:
			case RSDB_SCHEMA_KEY_UNIQUE:
			case RSDB_SCHEMA_KEY_INDEX:
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
			snprintf(buf, sizeof(buf), "%s INTEGER PRIMARY KEY", schema_element->name);
			break;

		case RSDB_SCHEMA_SERIAL_REF:
			snprintf(buf, sizeof(buf), "%s INTEGER%s%s%s",
				schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_BOOLEAN:
			snprintf(buf, sizeof(buf), "%s INTEGER", schema_element->name);
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
			snprintf(buf, sizeof(buf), "%s TEXT%s%s%s",
				schema_element->name,
				(schema_element->not_null ? " NOT NULL" : ""),
				(schema_element->def != NULL ? " DEFAULT " : ""),
				(schema_element->def != NULL ? schema_element->def : ""));
			break;

		case RSDB_SCHEMA_CHAR:
			snprintf(buf, sizeof(buf), "%s TEXT%s%s%s",
				schema_element->name,
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
			snprintf(buf, sizeof(buf), "CREATE UNIQUE INDEX %s_%s_prikey ON %s (%s);",
				table_name, schema_element->name,
				table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_UNIQUE:
			is_key = 1;
			snprintf(buf, sizeof(buf), "CREATE UNIQUE INDEX %s_%s_unique ON %s (%s);",
				table_name, schema_element->name,
				table_name, schema_element->name);
			break;

		case RSDB_SCHEMA_KEY_INDEX:
			is_key = 1;
			snprintf(buf, sizeof(buf), "CREATE INDEX %s_%s_idx ON %s (%s);",
				table_name, schema_element->name,
				table_name, schema_element->name);
			break;

		/* sqlite tables don't properly support foreign keys */
		case RSDB_SCHEMA_KEY_F_MATCH:
		case RSDB_SCHEMA_KEY_F_CASCADE:
			break;
	}

	if(!EmptyString(buf))
		dlink_add_tail_alloc(my_strdup(buf), (is_key ? key_data : table_data));
}

