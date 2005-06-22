/*
 *  ircd-ratbox: A slightly useful ircd.
 *  irc_string.c: IRC string functions.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "ircd_lib.h"
#include "irc_string.h"

/*
 * myctime - This is like standard ctime()-function, but it zaps away
 *   the newline from the end of that string. Also, it takes
 *   the time value as parameter, instead of pointer to it.
 *   Note that it is necessary to copy the string to alternate
 *   buffer (who knows how ctime() implements it, maybe it statically
 *   has newline there and never 'refreshes' it -- zapping that
 *   might break things in other places...)
 *
 *
 * Thu Nov 24 18:22:48 1986 
 */
const char *
myctime(time_t value)
{
	static char buf[32];
	char *p;

	strcpy(buf, ctime(&value));
	if((p = strchr(buf, '\n')) != NULL)
		*p = '\0';
	return buf;
}


/*
 * clean_string - clean up a string possibly containing garbage
 *
 * *sigh* Before the kiddies find this new and exciting way of 
 * annoying opers, lets clean up what is sent to local opers
 * -Dianora
 */
char *
clean_string(char *dest, const unsigned char *src, size_t len)
{
	char *d = dest;
	s_assert(0 != dest);
	s_assert(0 != src);

	if(dest == NULL || src == NULL)
		return NULL;

	len -= 3;		/* allow for worst case, '^A\0' */

	while (*src && (len > 0))
	{
		if(*src & 0x80)	/* if high bit is set */
		{
			*d++ = '.';
			--len;
		}
		else if(!IsPrint(*src))	/* if NOT printable */
		{
			*d++ = '^';
			--len;
			*d++ = 0x40 + *src;	/* turn it into a printable */
		}
		else
			*d++ = *src;
		++src;
		--len;
	}
	*d = '\0';
	return dest;
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
	/* Sanity check; we don't want anything nasty... */
	s_assert(0 != dest);
	s_assert(0 != src);

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

/*
 * strtoken - walk through a string of tokens, using a set of separators
 *   argv 9/90
 *
 */
char *
strtoken(char **save, char *str, const char *fs)
{
	char *pos = *save;	/* keep last position across calls */
	char *tmp;

	if(str)
		pos = str;	/* new string scan */

	while (pos && *pos && strchr(fs, *pos) != NULL)
		++pos;		/* skip leading separators */

	if(!pos || !*pos)
		return (pos = *save = NULL);	/* string contains only sep's */

	tmp = pos;		/* now, keep position of the token */

	while (*pos && strchr(fs, *pos) == NULL)
		++pos;		/* skip content of the token */

	if(*pos)
		*pos++ = '\0';	/* remove first sep after the token */
	else
		pos = NULL;	/* end of string */

	*save = pos;
	return tmp;
}


