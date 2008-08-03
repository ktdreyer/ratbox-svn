/* src/stest_main.c
 *   Contains the main functions for the schema test program.
 *
 * Copyright (C) 2008 Lee Hardy <lee@leeh.co.uk>
 * Copyright (C) 2008 ircd-ratbox development team
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
#include "event.h"
#include "balloc.h"
#include "rsdb.h"
#include "rsdbs.h"
#include "log.h"
#include "io.h"

void sendto_all(const char *format, ...) { }
void eventAdd(const char *name, EVH * func, void *arg, time_t when) { }
void set_time(void) { }

struct timeval system_time;

void
mlog(const char *format, ...)
{
	va_list args;
	char buf[BUFSIZE];

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	fprintf(stdout, "%s\n", buf);
}

void
die(int graceful, const char *format, ...)
{
	va_list args;
	char buf[BUFSIZE];

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	fprintf(stdout, "%s\n", buf);
	exit(0);
}

static void
print_help(void)
{
	printf("schematest [-h] <-d dbname|-t dbhost|-u dbusername|-p dbpassword>\n");
}

int
main(int argc, char *argv[])
{
	char *db_name = NULL;
	char *db_host = NULL;
	char *db_username = NULL;
	char *db_password = NULL;
	char c;

	while((c = getopt(argc, argv, "hid:t:u:p:")) != -1)
	{
		switch(c)
		{
			case 'h':
				print_help();
				exit(0);
				break;

			case 'd':
				db_name = my_strdup(optarg);
				break;

			case 't':
				db_host = my_strdup(optarg);
				break;

			case 'u':
				db_username = my_strdup(optarg);
				break;

			case 'p':
				db_password = my_strdup(optarg);
				break;
		}
	}

	if(EmptyString(db_name) || EmptyString(db_host) || EmptyString(db_username) || EmptyString(db_password))
	{
		print_help();
		exit(0);
	}

	init_balloc();
	init_tools();

	rsdb_init(db_name, db_host, db_username, db_password);

	schema_init(0);

	exit(0);
}
