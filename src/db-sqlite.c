/*
 * db-sqlite.c -- Interface to postgreSQL 
 * Copyright 2000-2004 Aaron Sethman <androsyn@ratbox.org>
 *
 * $Id$
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sqlite.h>
#include "db.h"
#include "defs.h"

results *dbquery(void *con, const char *fmt, ...)
{
	results *row;
        va_list ap;
        va_start(ap, fmt);
	row = vdbquery(con, fmt, ap);
	va_end(ap);
	return(row);
}

void *dbopen(const char *connstring)
{
	return(sqlite_open(connstring, 0, NULL));
}


results *vdbquery(void *conn, const char *fmt, va_list ap)
{
	results *row = calloc(1, sizeof(results));
	row->status = sqlite_get_table_vprintf(conn, fmt, results->row, &results->row_count, &results->col_count, NULL, ap);
	row->cstatus = row->status;
	return row;
}


void free_results(results *row)
{
	sqlite_free_table(results->row);
	free(row);
}

