/* src/tools.c
 *   Contains various useful functions.
 *
 * Copyright (C) 2003-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2012 ircd-ratbox development team
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
#include "tools.h"

/* get_duration()
 *   converts duration in seconds to a string form
 *
 * inputs       - duration in seconds
 * outputs      - string form of duration, "n day(s), h:mm:ss"
 */
const char *
get_duration(time_t seconds)
{
        static char buf[BUFSIZE];
        int days, hours, minutes;

        days = (int) (seconds / 86400);
        seconds %= 86400;
        hours = (int) (seconds / 3600);
        seconds %= 3600;
        minutes = (int) (seconds / 60);
        seconds %= 60;

        snprintf(buf, sizeof(buf), "%d day%s, %d:%02d:%02lu",
                 days, (days == 1) ? "" : "s", hours,
                 minutes, (unsigned long) seconds);

        return buf;
}

const char *
get_short_duration(time_t seconds)
{
	static char buf[BUFSIZE];
        int days, hours, minutes;

        days = (int) (seconds / 86400);
        seconds %= 86400;
        hours = (int) (seconds / 3600);
	seconds %= 3600;
	minutes = (int) (seconds / 60);

        snprintf(buf, sizeof(buf), "%dd%dh%dm", days, hours, minutes);

        return buf;
}

const char *
get_time(time_t when, int tz)
{
	static char timebuffer[BUFSIZE];
	struct tm *tmptr;

	if(!when)
		when = rb_time();

	tmptr = gmtime(&when);

	if(tz)
		strftime(timebuffer, MAX_DATE_STRING, "%Y-%m-%d %H:%M %Z", tmptr);
	else
		strftime(timebuffer, MAX_DATE_STRING, "%Y-%m-%d %H:%M", tmptr);

	return timebuffer;
}

time_t
get_temp_time(const char *duration)
{
	time_t result = 0;

	for(; *duration; duration++)
	{
		if(IsDigit(*duration))
		{
			result *= 10;
			result += ((*duration) & 0xF);
		}
		else
		{
			if (!result || *(duration+1))
				return 0;
			switch (*duration)
			{
				case 'h': case 'H':
					result *= 60; 
					break;
				case 'd': case 'D': 
					result *= 1440; 
					break;
				case 'w': case 'W':
					result *= 10080; 
					break;
				default:
					return 0;
			}
		}
	}

	/* max at 1 year */
	/* time_t is signed, so if we've overflowed, reset to max */
	if(result > (60*24*7*52) || result < 0)
		result = (60*24*7*52);

	return(result*60);
}

const char *
lcase(const char *text)
{
	static char buf[BUFSIZE+1];
	int i = 0;

	buf[0] = '\0';

	while(text[i] != '\0' && i < BUFSIZE-1)
	{
		buf[i] = ToLower(text[i]);
		i++;
	}

	buf[i] = '\0';

	return buf;
}

const char *
ucase(const char *text)
{
	static char buf[BUFSIZE+1];
	int i = 0;

	buf[0] = '\0';

	while(text[i] != '\0' && i < BUFSIZE-1)
	{
		buf[i] = ToUpper(text[i]);
		i++;
	}

	buf[i] = '\0';

	return buf;
}

/*
 * strip_tabs(dst, src, length)
 *
 *   Copies src to dst, while converting all \t (tabs) into spaces.
 *
 * NOTE: jdc: I have a gut feeling there's a faster way to do this.
 */
char *
strip_tabs(char *dest, const unsigned char *src, size_t len)
{
	char *d = dest;

	if(dest == NULL || src == NULL)
		return NULL;

	while (*src && (len > 0))
	{
		if(*src == '\t')
		{
			*d++ = ' ';	/* Translate the tab into a space */
		}
		else
		{
			*d++ = *src;	/* Copy src to dst */
		}
		++src;
		--len;
	}
	*d = '\0';		/* Null terminate, thanks and goodbye */
	return dest;
}

