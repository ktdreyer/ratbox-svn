/* src/rsdb_mysql.c
 *   Contains the code for the mysql database backend.
 *
 * Copyright (C) 2006 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2006 ircd-ratbox development team
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
#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#include "rsdb.h"
#include "rserv.h"
#include "conf.h"
#include "log.h"

#define RSDB_MAXCOLS 30

MYSQL *rsdb_database;

static int rsdb_connect(int initial);

/* rsdb_init()
 */
void
rsdb_init(void)
{
	if(EmptyString(config_file.db_name) || EmptyString(config_file.db_host) ||
	   EmptyString(config_file.db_username) || EmptyString(config_file.db_password))
	{
		die("Missing conf options in database {};");
	}

	if((rsdb_database = mysql_init(NULL)) == NULL)
		die("Out of memory -- failed to initialise mysql pointer");

	rsdb_connect(1);
}

/* rsdb_connect()
 * attempts to connect to the mysql database
 *
 * inputs	- initial, set if we're starting up
 * outputs	- 0 on success, > 0 on fatal error, < 0 on non-fatal error
 * side effects - connection to the mysql database is attempted
 */
static int
rsdb_connect(int initial)
{
	void *unused = mysql_real_connect(rsdb_database, config_file.db_host,
				config_file.db_username, config_file.db_password,
				config_file.db_name, 0, NULL, 0);

	if(unused)
		return 0;

	/* all errors on startup are fatal */
	if(initial)
		die("Unable to connect to mysql database: %s",
			mysql_error(rsdb_database));

	switch(mysql_errno(rsdb_database))
	{
		case CR_SERVER_LOST:
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
	mysql_close(rsdb_database);
}

static void
rsdb_handle_error(void)
{
	switch(mysql_errno(rsdb_database))
	{
		case 0:
			return;

		case CR_SERVER_GONE_ERROR:
		case CR_SERVER_LOST:
			/* try to reconnect immediately.. if that fails fall
			 * into periodic reconnections
			 */
			if(!rsdb_connect(0))
				;	/* XXX */

			break;

		default:
			mlog("fatal error: problem with db file: %s",
				mysql_error(rsdb_database));
			die("problem with db file");
			return;
	}
}

const char *
rsdb_quote(const char *src)
{
	static char buf[BUFSIZE*4];
	unsigned long length;

	length = strlen(src);

	if(length >= (sizeof(buf) / 2))
		die("length problem compiling sql statement");

	mysql_real_escape_string(rsdb_database, buf, src, length);
	return buf;
}

void
rsdb_exec(rsdb_callback cb, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	static const char *coldata[RSDB_MAXCOLS+1];
	MYSQL_RES *rsdb_result;
	MYSQL_ROW row;
	va_list args;
	unsigned int field_count;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem compiling sql statement: %s", buf);
		die("length problem compiling sql statement");
	}

	if(mysql_query(rsdb_database, buf))
		rsdb_handle_error();

	field_count = mysql_field_count(rsdb_database);

	if(field_count > RSDB_MAXCOLS)
		die("too many columns in result set -- contact the ratbox team");

	if(!field_count || !cb)
		return;

	if((rsdb_result = mysql_store_result(rsdb_database)) == NULL)
		rsdb_handle_error();

	while((row = mysql_fetch_row(rsdb_result)))
	{
		for(i = 0; i < field_count; i++)
		{
			coldata[i] = row[i];
		}
		coldata[i] = NULL;

		(cb)((int) field_count, coldata);
	}

	mysql_free_result(rsdb_result);
}

void
rsdb_exec_fetch(struct rsdb_table *table, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	MYSQL_RES *rsdb_result;
	MYSQL_ROW row;
	va_list args;
	int i, j;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem compiling sql statement: %s", buf);
		die("length problem compiling sql statement");
	}

	if(mysql_query(rsdb_database, buf))
		rsdb_handle_error();

	if((rsdb_result = mysql_store_result(rsdb_database)) == NULL)
		rsdb_handle_error();

	table->row_count = (unsigned int) mysql_num_rows(rsdb_result);
	table->col_count = mysql_field_count(rsdb_database);
	table->arg = rsdb_result;

	if(!table->row_count || !table->col_count)
	{
		table->row = NULL;
		return;
	}

	table->row = my_malloc(sizeof(char **) * table->row_count);

	for(i = 0; row; i++, row = mysql_fetch_row(rsdb_result))
	{
		table->row[i] = my_malloc(sizeof(char *) * table->col_count);

		for(j = 0; j < table->col_count; j++)
		{
			table->row[i][j] = row[j] ? strdup(row[j]) : NULL;	// XXX - available?
		}
	}
}

void
rsdb_exec_fetch_end(struct rsdb_table *table)
{
	int i, j;

	for(i = 0; i < table->row_count; i++)
	{
		for(j = 0; j < table->col_count; j++)
		{
			my_free(table->row[i][j]);
		}

		my_free(table->row[i]);
	}

	my_free(table->row);

	mysql_free_result((MYSQL_RES *) table->arg);
}

void
rsdb_transaction(rsdb_transtype type)
{
	/* XXX */
	if(type == RSDB_TRANS_START)
		;
	else if(type == RSDB_TRANS_END)
		;
}

