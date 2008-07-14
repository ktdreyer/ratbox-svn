/* src/rsdb_common.c
 *   Contains the code that is common through all databases.
 *
 * Copyright (C) 2006-2008 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2006-2008 Aaron Sethman <androsyn@ratbox.org>
 * Copyright (C) 2006-2008 ircd-ratbox development team
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

/* rsdb_common_fetch_end()
 * free()'s the memory after rsdb_exec_fetch()
 *
 * inputs	- table entry used for rsdb_exec_fetch()
 * outputs	-
 * side effects	- memory allocated for the table entry is free()'d
 */
void
rsdb_common_fetch_end(struct rsdb_table *table)
{
	int i;

	for(i = 0; i < table->row_count; i++)
	{
		my_free(table->row[i]);
	}

	my_free(table->row);
}


