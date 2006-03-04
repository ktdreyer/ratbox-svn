/* src/email.c
 *   Contains code for generating emails.
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
 * $Id: cache.c 20234 2005-04-07 13:12:33Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "conf.h"
#include "log.h"
#include "email.h"

int
send_email(const char *address, const char *subject, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	static char databuf[BUFSIZE*4];
	static char cmdbuf[BUFSIZE];
	FILE *out;
	va_list args;

	if(EmptyString(config_file.email_program))
	{
		mlog("warning: unable to send email, email program is not set");
		return 0;
	}

	if(EmptyString(config_file.email_address))
	{
		mlog("warning: unable to send email, email address is not set");
		return 0;
	}

	snprintf(cmdbuf, sizeof(cmdbuf), "%s", config_file.email_program);

	if((out = popen(cmdbuf, "w")) == NULL)
	{
		mlog("warning: unable to send email, cannot execute email program");
		return 0;
	}

	snprintf(buf, sizeof(buf),
		"From: %s <%s>\n"
		"To: %s\n"
		"Subject: %s\n\n",
		EmptyString(config_file.email_name) ? "" : config_file.email_name, 
		config_file.email_address,
		address, subject);

	va_start(args, format);
	vsnprintf(databuf, sizeof(databuf), format, args);
	va_end(args);

	fputs(buf, out);
	fputs(databuf, out);

	pclose(out);

	return 1;
}
