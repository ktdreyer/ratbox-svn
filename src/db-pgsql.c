/*
 * db-pgsql.c -- Interface to postgreSQL 
 * Copyright 2000-2004 Aaron Sethman <androsyn@ratbox.org>
 *
 * $Id$
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libpq-fe.h>
#include "db.h"

results *dbquery(void *con, const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);
        return(vdbquery(con, fmt, ap));
}

results *vdbquery(void *conn, const char *fmt, va_list ap)
{
	PGresult *res;
	ExecStatusType status;
	char buf[2048];
	int x,y;
	results *row = calloc(1, sizeof(results));
	vsnprintf(buf, sizeof(buf), fmt, ap);

	res = PQexec(conn, buf);	
	if(res == NULL)
	{
		row->status = FALSE;
		return row;
	}
	status = PQresultStatus(res);
	if(status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
	{
		row->status = FALSE;
		return row;
	}
	
	row->row_count = PQntuples(res);
	row->col_count = PQnfields(res);
	if(strlen(PQcmdTuples(res)) > 0)
	{
		row->affected_rows = atoi(PQcmdTuples(res));
	}
	row->status = TRUE;
	if(row->row_count > 0)
	{
		row->row = calloc(1, row->row_count * sizeof(char **));
		for(x = 0; x < row->row_count; x++)
		{
			row->row[x] = calloc(1, row->col_count * sizeof(char *));
			for(y = 0; y < row->col_count; y++)
			{
				row->row[x][y] = strdup(PQgetvalue(res, x, y));	
			}		 
		}			
	} else {
		row->row = NULL;
	}
	
	PQclear(res);
	return row;
}

void free_results(results *row)
{
 	int x, y;
 	if(row->row != NULL && row->status == TRUE)
 	{
 	 	for(x = row->row_count - 1; x >= 0; x--)
         	{
         	 	for(y = row->col_count - 1; y >= 0; y--)
         	 	{
         	 	 	free(row->row[x][y]);
 	 	 	}
                 	free(row->row[x]);
	  	}
                free(row->row);
 	}
	free(row);

}

