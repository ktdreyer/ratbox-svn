/*
 *  ircd-ratbox: A slightly useful ircd.
 *  memory.h: A header for the memory functions.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
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

#ifndef _I_MEMORY_H
#define _I_MEMORY_H

#include "ircd_defs.h"
#include "setup.h"
#include "balloc.h"

/* Needed to use uintptr_t for some pointer manipulation. */
#ifdef __VMS
#include inttypes
#else /* Not VMS */
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#else /* No inttypes.h */
#ifndef HAVE_UINTPTR_T
typedef unsigned long uintptr_t;
#endif
#endif
#endif

extern void outofmemory(void);


extern void *MyMalloc(size_t size);
extern void *MyRealloc(void *x, size_t y);
extern void MyFree(void *x);
extern void _DupString(char **x, const char *y);

/* forte (and maybe others) dont like double declarations, 
 * so we dont declare the inlines unless GNUC
 */
/* darwin doesnt like these.. */
#ifndef __APPLE__

#ifdef __GNUC__
extern inline void *
MyMalloc(size_t size)
{
	void *ret = calloc(1, size);
	if(ret == NULL)
		outofmemory();
	return (ret);
}

extern inline void *
MyRealloc(void *x, size_t y)
{
	void *ret = realloc(x, y);

	if(ret == NULL)
		outofmemory();
	return (ret);
}

extern inline void
MyFree(void *x)
{
	if(x != NULL)
		free(x);
}

#ifndef HAVE_STRDUP
extern inline void
_DupString(char **x, const char *y)
{
	(*x) = malloc(strlen(y) + 1);
	if(x == NULL)
		outofmemory();
	strcpy((*x), y);
}
#endif /* HAVE_STRDUP */
#endif /* __GNUC__ */
#endif /* __APPLE__ */

#ifdef HAVE_STRDUP
#define DupString(x,y) do { (*x) = strdup(y); if(x == NULL) outofmemory(); } while(0)
#else
#define DupString(x,y) _DupString(&x, y)
#endif


#endif /* _I_MEMORY_H */
