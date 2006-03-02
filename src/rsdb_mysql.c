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
#include "rsdb.h"
#include "rserv.h"
#include "conf.h"
#include "log.h"

#define RSDB_MAXCOLS 30

MYSQL *rsdb_database;
MYSQL_RES *rsdb_result;
unsigned int rsdb_field_count;

/* rsdb_init()
 */
void
rsdb_init(void)
{
	if((rsdb_database = mysql_init(NULL)) == NULL)
		die("Out of memory -- failed to initialise mysql pointer");

	if(!mysql_real_connect(rsdb_database, config_file.db_host,
				config_file.db_username, config_file.db_password,
				config_file.db_name, 0, NULL, 0))
		die("Unable to connect to mysql database: %s",
			mysql_error(rsdb_database));
}

void
rsdb_shutdown(void)
{
	mysql_close(rsdb_database);
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

static int
rsdb_callback_func(void *cbfunc, int argc, char **argv, char **colnames)
{
	rsdb_callback cb = cbfunc;
}

void
rsdb_exec(rsdb_callback cb, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	static const char *colnames[RSDB_MAXCOLS+1];
	static const char *coldata[RSDB_MAXCOLS+1];
	MYSQL_ROW row;
	MYSQL_FIELD *fields;
	va_list args;
	unsigned int field_count;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
		die("length problem compiling sql statement");

	if(mysql_query(rsdb_database, buf))
	{
		mlog("fatal error: problem with db file: %s",
			mysql_error(rsdb_database));
		die("problem with db file");
	}

	field_count = mysql_field_count(rsdb_database);

	if(field_count > RSDB_MAXCOLS)
	{
		die("foo"); /* XXX */
	}

	if(!field_count || !cb)
		return;

	if((rsdb_result = mysql_use_result(rsdb_database)) == NULL)
	{
		// CR_SERVER_LOST?
		mlog("fatal error: problem with db file: %s",
			mysql_error(rsdb_database));
		die("problem with db file");
	}

	fields = mysql_fetch_fields(rsdb_result);

	for(i = 0; i < field_count; i++)
	{
		colnames[i] = fields[i].name;
	}
	colnames[i] = NULL;

	while((row = mysql_fetch_row(rsdb_result)))
	{
		/* const char ** cast? */
		for(i = 0; i < field_count; i++)
		{
			coldata[i] = row[i];
		}
		coldata[i] = NULL;

		(cb)((int) field_count, coldata, colnames);
	}

	/* XXX error? */

	mysql_free_result(rsdb_result);
}

void
rsdb_transaction(rsdb_transtype type)
{
	if(type == RSDB_TRANS_START)
		;
	else if(type == RSDB_TRANS_END)
		;
}

void
rsdb_step_init(const char *format, ...)
{
	static char buf[BUFSIZE*4];
	int i;
	va_list args;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
		die("length problem compiling sql statement");

	if(mysql_query(rsdb_database, buf))
	{
		mlog("fatal error: problem with db file: %s",
			mysql_error(rsdb_database));
		die("problem with db file");
	}

	rsdb_field_count = mysql_field_count(rsdb_database);

	if(rsdb_field_count > RSDB_MAXCOLS)
	{
		die("foo"); /* XXX */
	}

	if((rsdb_result = mysql_use_result(rsdb_database)) == NULL)
	{
		// CR_SERVER_LOST?
		mlog("fatal error: problem with db file: %s",
			mysql_error(rsdb_database));
		die("problem with db file");
	}
}

int
rsdb_step(int *ncol, const char ***coldata, const char ***colnames)
{
	MYSQL_ROW row;
	MYSQL_FIELD *fields;
	int i;

	if(!rsdb_field_count)
		return 0;

	fields = mysql_fetch_fields(rsdb_result);

	for(i = 0; i < rsdb_field_count; i++)
	{
		*colnames[i] = fields[i].name;
	}
	*colnames[i] = NULL;

	while((row = mysql_fetch_row(rsdb_result)))
	{
		/* const char ** cast? */
		for(i = 0; i < rsdb_field_count; i++)
		{
			*coldata[i] = row[i];
		}
		*coldata[i] = NULL;
	}

	/* xxx error check */
	return 1;
}

void
rsdb_step_end(void)
{
	while(mysql_fetch_row(rsdb_result))
		;

	mysql_free_result(rsdb_result);
}
	

