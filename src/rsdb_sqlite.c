/* src/rsdb_sqlite.h
 *   Contains the code for the sqlite database backend.
 *
 * Copyright (C) 2003-2006 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2006 ircd-ratbox development team
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

/* build sqlite, so use local version */
#ifdef SQLITE_BUILD
#include "sqlite.h"
#else
#include <sqlite.h>
#endif

struct sqlite *rserv_db;

sqlite_vm *rsdb_step_vm;

/* rsdb_init()
 */
void
rsdb_init(void)
{
	char *errmsg;

	if((rserv_db = sqlite_open(DB_PATH, 0, &errmsg)) == NULL)
	{
		die("Failed to open db file: %s", errmsg);
	}
}

void
rsdb_shutdown(void)
{
	if(rserv_db)
		sqlite_close(rserv_db);
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
	static char buf[BUFSIZE*4];
	va_list args;
	char *errmsg;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die("problem with compiling sql statement");
	}

	if((i = sqlite_exec(rserv_db, buf, (cb ? rsdb_callback_func : NULL), cb, &errmsg)))
	{
		mlog("fatal error: problem with db file: %s", errmsg);
		die("problem with db file");
	}
}

void
rsdb_exec_fetch(struct rsdb_table *table, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	va_list args;
	char *errmsg;
	char **data;
	int pos;
	int i, j;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die("problem with compiling sql statement");
	}

	if(sqlite_get_table(rserv_db, buf, &data, &table->row_count, &table->col_count, &errmsg))
	{
		mlog("fatal error: problem with db file: %s", errmsg);
		die("problem with db file");
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
	int i;

	for(i = 0; i < table->row_count; i++)
	{
		my_free(table->row[i]);
	}
	my_free(table->row);

	sqlite_free_table((char **) table->arg);
}

void
rsdb_transaction(rsdb_transtype type)
{
	if(type == RSDB_TRANS_START)
		rsdb_exec(NULL, "BEGIN TRANSACTION");
	else if(type == RSDB_TRANS_END)
		rsdb_exec(NULL, "COMMIT TRANSACTION");
}

void
rsdb_step_init(const char *format, ...)
{
	static char buf[BUFSIZE*4];
	const char *tail;
	char *errmsg;
	int i;
	va_list args;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die("problem with compiling sql statement");
	}

	if((i = sqlite_compile(rserv_db, buf, &tail, &rsdb_step_vm, &errmsg)))
	{
		mlog("fatal eror: problem with compiling sql: %s", errmsg);
		die("problem with compiling sql statement");
	}
}

int
rsdb_step(int *ncol, const char ***coldata)
{
	static const char **colnames;
	int i;

	if((i = sqlite_step(rsdb_step_vm, ncol, coldata, &colnames)))
	{
		if(i == SQLITE_DONE)
			rsdb_step_end();
		else if(i == SQLITE_ROW)
			return 1;
		else
		{
			mlog("fatal error: problem with sql step: %d", i);
			die("problem with sql step");
		}
	}

	return 0;
}

void
rsdb_step_end(void)
{
	char *errmsg;
	int i;

	if((i = sqlite_finalize(rsdb_step_vm, &errmsg)))
	{
		mlog("fatal error: problem with finalizing sql: %s",
			errmsg);
		die("problem with finalising sql statement");
	}
}


