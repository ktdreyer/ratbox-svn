/* 
 * db.h - Semi generic database wrapper
 * Copyright 2000-2004 Aaron Sethman <androsyn@ratbox.org>
 *
 * $Id$
 */
 
#ifndef INCLUDED_db_h
#define INCLUDED_db_h

typedef struct {
	char ***row;
	int row_count;
	int col_count;
	int affected_rows;
	int status;
	int cstatus;
} results;

#endif

