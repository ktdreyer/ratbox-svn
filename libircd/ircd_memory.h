/* $Id$ */

#ifndef _I_MEMORY_H
#define _I_MEMORY_H

#undef MEMDEBUG

#include "ircd_defs.h"
#include <stdlib.h>
#include <string.h>

extern void outofmemory(void);


#ifdef MEMDEBUG
extern void*       _MyMalloc(size_t size, char * file, int line);
extern void*       _MyRealloc(void* p, size_t size, char * file, int line);
extern void        _MyFree(void* p, char * file, int line);
extern void        _DupString(char**, const char*, char*, int);
#define MyMalloc(x) _MyMalloc(x, __FILE__, __LINE__)
#define MyRealloc(x,y) _MyRealloc(x, y, __FILE__, __LINE__)
#define MyFree(x) _MyFree(x, __FILE__, __LINE__)
#define DupString(x,y) _DupString(&x, y, __FILE__, __LINE__)
#else /* MEMDEBUG */


extern inline void *
_MyMalloc(size_t size)
{
  void* ret = malloc(size);
  if (ret == NULL)
    outofmemory();
  else
    memset(ret, 0, size);
  return ret;
}
 
/*
 * MyRealloc - reallocate memory, call outofmemory on failure
 */
extern inline void*
_MyRealloc(void* x, size_t y)
{
  char *ret = realloc(x, y);

  if (!ret)
    outofmemory();
  return ret;
}
 
extern inline void
_MyFree(void *x)
{
  if ((x))
    free((x));
}
 
extern inline void 
_DupString(char **x, const char *y) {
  (*x) = _MyMalloc(strlen(y) + 1);
  strcpy((*x), y);
}

#define MyMalloc(x) _MyMalloc(x)
#define MyRealloc(x,y) _MyRealloc(x, y)
#define MyFree(x) _MyFree(x)
#define DupString(x,y) _DupString(&x, y)
#endif /* !MEMDEBUG */

#ifndef WE_ARE_MEMORY_C
#undef strdup
#undef malloc
#undef realloc
#undef calloc
#undef free
#define malloc do_not_call_old_memory_functions!call_My*functions
#define calloc do_not_call_old_memory_functions!call_My*functions
#define realloc do_not_call_old_memory_functions!call_My*functions
#define strdup do_not_call_old_memory_functions!call_My*functions
#define free do_not_call_old_memory_functions!call_My*functions
#endif


#endif /* _I_MEMORY_H */

