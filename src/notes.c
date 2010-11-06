/* src/notes.c
 *   Contains the code for handling channel notes
 *
 * Copyright (C) 2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2007 ircd-ratbox development team
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
 * $Id: s_alis.c 23596 2007-02-05 21:35:27Z leeh $
 */
#include "stdinc.h"

#include "rserv.h"
#include "rsdb.h"
#include "langs.h"
#include "service.h"
#include "log.h"
#include "conf.h"
#include "tools.h"
#include "notes.h"


int
add_channote(const char *chan, const char *author, uint32_t flags,
		const char *format, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	rsdb_exec(NULL,
			"INSERT INTO chan_note "
			"(chname, timestamp, author, flags, text) "
			"VALUES(LOWER('%Q'), '%lu', '%Q', '%u', '%Q')",
			chan, rb_current_time(), author, flags, buf);

	return 1;
}


int
delete_channote(long note_id)
{
	if(note_id < 1)
		return 0;

	rsdb_exec(NULL, "DELETE FROM chan_note WHERE id = %d", note_id);

	return 1;
}


int
list_channotes(struct client *service_p, struct client *client_p,
		const char *chan, int start, int end)
{
	struct rsdb_table data;
	int i;

	if(!service_p || !client_p)
		return 0;

	if(!start && !end)
	{
		rsdb_exec_fetch(&data, "SELECT id, author, timestamp, text FROM chan_note "
			"WHERE chname = '%Q' "
			"AND !(flags & %u) AND !(flags & %u) "
			"ORDER BY timestamp DESC LIMIT %d",
			chan, NOTE_CF_ALERT, NOTE_CF_BLOCK, config_file.max_notes);
	}
	else
	{
		rsdb_exec_fetch(&data, "SELECT id, author, timestamp, text FROM chan_note "
			"WHERE chname = '%Q' "
			"AND !(flags & %u) AND !(flags & %u) "
			"ORDER BY timestamp DESC LIMIT %d OFFSET %d",
			chan, NOTE_CF_ALERT, NOTE_CF_BLOCK, (end-start), start);
	}

	if(data.row_count > 0)
	{
		for(i = data.row_count-1; i >= 0; i--)
		{
			service_err(service_p, client_p, SVC_NOTE_SHOW,
					atoi(data.row[i][0]), data.row[i][1],
					get_time(atoi(data.row[i][2]), 0), data.row[i][3]);
		}
	}
	else
		service_err(service_p, client_p, SVC_NOTE_NONE);

	rsdb_exec_fetch_end(&data);

	return 1;
}


int
show_alert_note(struct client *service_p, struct client *client_p, const char *chan)
{
	struct rsdb_table data;

	rsdb_exec_fetch(&data, "SELECT id, author, timestamp, text "
			"FROM chan_note WHERE chname = '%Q' "
			"AND (flags & %u)", chan, NOTE_CF_ALERT);

	if(data.row_count == 1)
	{
		service_err(service_p, client_p, SVC_NOTE_SHOW,
				atoi(data.row[0][0]), data.row[0][1],
				get_time(atoi(data.row[0][2]), 0), data.row[0][3]);
	}

	rsdb_exec_fetch_end(&data);

	return 1;
}

int
show_block_note(struct client *service_p, struct client *client_p, const char *chan)
{
	struct rsdb_table data;

	rsdb_exec_fetch(&data, "SELECT id, author, timestamp, text "
			"FROM chan_note WHERE chname = '%Q' "
			"AND (flags & %u)", chan, NOTE_CF_BLOCK);

	if(data.row_count == 1)
	{
		service_err(service_p, client_p, SVC_NOTE_SHOW,
				atoi(data.row[0][0]), data.row[0][1],
				get_time(atoi(data.row[0][2]), 0), data.row[0][3]);
	}

	rsdb_exec_fetch_end(&data);

	return 1;
}

