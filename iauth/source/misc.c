/************************************************************************
 *   IRC - Internet Relay Chat, iauth/misc.c
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   $Id$
 */

#include "headers.h"

static void outofmemory();

/*
 * strncpy_irc - optimized strncpy
 * This may not look like it would be the fastest possible way to do it,
 * but it generally outperforms everything else on many platforms, 
 * including asm library versions and memcpy, if compiled with the 
 * optimizer on. (-O2 for gcc) --Bleep
 */
char *
strncpy_irc(char* s1, const char* s2, size_t n)

{
  register char* endp = s1 + n;
  register char* s = s1;
  while (s < endp && (*s++ = *s2++))
    ;
  return s1;
}

/*
 * MyMalloc - allocate memory, call outofmemory on failure
 */
void *
MyMalloc(size_t x)

{
  void* ret = malloc(x);

  if (!ret)
    outofmemory();
  return ret;
}

/*
MyFree()
 Free a pointer
*/

void
MyFree(void *ptr)

{
	free(ptr);
	ptr = NULL;
} /* MyFree() */

static void
outofmemory()

{
#ifdef bingo
	log("Out of memory, exiting");
#endif
	exit (-1);
} /* outofmemory() */
