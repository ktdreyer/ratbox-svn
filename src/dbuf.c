/************************************************************************
 *   IRC - Internet Relay Chat, src/dbuf.c
 *   Copyright (C) 1990 Markku Savela
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
 *
 * For documentation of the *global* functions implemented here,
 * see the header file (dbuf.h).
 *
 *
 * $Id$
 */
#include "dbuf.h"
#include "common.h"
#include "irc_string.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>


/*
 * And this 'DBufBuffer' should never be referenced outside the
 * implementation of 'dbuf'--would be "hidden" if C had such
 * keyword...
 * doh!!! ya just gotta know how to do it ;-)
 * If it was possible, this would compile to be exactly 1 memory
 * page in size. 2048 bytes seems to be the most common size, so
 * as long as a pointer is 4 bytes, we get 2032 bytes for buffer
 * data after we take away a bit for malloc to play with. -avalon
 */
#ifdef _4K_DBUFS
# define DBUF_SIZE (4096 - sizeof(void*))
#else
# define DBUF_SIZE (2048 - sizeof(void*))
#endif

struct DBufBuffer {
  struct DBufBuffer* next;  /* Next data buffer, NULL if this is last */
  char            data[DBUF_SIZE];/* Actual data stored here */
};

int                       DBufUsedCount = 0;
int                       DBufCount = 0;
static struct DBufBuffer* dbufFreeList = NULL;

void count_dbuf_memory(size_t* allocated, size_t* used)
{
  assert(0 != allocated);
  assert(0 != used);
  *allocated = DBufCount * sizeof(struct DBufBuffer);
  *used      = DBufUsedCount * sizeof(struct DBufBuffer);
}

/* 
 * dbuf_init--initialize a stretch of memory as dbufs.
 * Doing this early on should save virtual memory if not real memory..
 * at the very least, we get more control over what the server is doing 
 * 
 * mika@cs.caltech.edu 6/24/95
 *
 * XXX - Unfortunately this makes cleanup impossible because the block 
 * pointer isn't saved and dbufs are not allocated in chunks anywhere else.
 * --Bleep
 */
void dbuf_init()
{
  int      i;
  struct DBufBuffer* dbp;

  assert(0 == dbufFreeList);
  dbufFreeList = (struct DBufBuffer*) MyMalloc(sizeof(struct DBufBuffer) * INITIAL_DBUFS);
  assert(0 != dbufFreeList);

  dbp = dbufFreeList;

  for (i = 0; i < INITIAL_DBUFS - 1; ++i) {
    dbp->next = (dbp + 1);
    ++dbp;
  }
  dbp->next  = NULL;
  DBufCount = INITIAL_DBUFS;
}

/*
** dbuf_alloc - allocates a struct DBufBuffer structure either from dbufFreeList or
** creates a new one.
*/
static struct DBufBuffer *dbuf_alloc()
{
  struct DBufBuffer* dbptr = dbufFreeList;

  if (DBufUsedCount * DBUF_SIZE == BUFFERPOOL)
    return NULL;

  if (dbptr)
    dbufFreeList = dbufFreeList->next;
  else {
    dbptr = (struct DBufBuffer*) MyMalloc(sizeof(struct DBufBuffer));
    assert(0 != dbptr);
    ++DBufCount;
  }
  ++DBufUsedCount;
  return dbptr;
}

/*
** dbuf_free - return a struct DBufBuffer structure to the dbufFreeList
*/
static void dbuf_free(struct DBufBuffer* ptr)
{
  assert(0 != ptr);
  --DBufUsedCount;
  ptr->next = dbufFreeList;
  dbufFreeList = ptr;
}
/*
** This is called when malloc fails. Scrap the whole content
** of dynamic buffer and return -1. (malloc errors are FATAL,
** there is no reason to continue this buffer...). After this
** the "dbuf" has consistent EMPTY status... ;)
*/
static int dbuf_malloc_error(struct DBuf *dyn)
{
  struct DBufBuffer *p;

  dyn->length = 0;
  dyn->offset = 0;
  while ((p = dyn->head) != NULL)
    {
      dyn->head = p->next;
      dbuf_free(p);
    }
  dyn->tail = dyn->head;
  return -1;
}


int dbuf_put(struct DBuf *dyn, char *buf, int length)
{
  struct DBufBuffer       **h, *d;
  int    off;
  int   chunk;

  off = (dyn->offset + dyn->length) % DBUF_SIZE;
  /*
  ** Locate the last non-empty buffer. If the last buffer is
  ** full, the loop will terminate with 'd==NULL'. This loop
  ** assumes that the 'dyn->length' field is correctly
  ** maintained, as it should--no other check really needed.
  */
  if (!dyn->length)
    h = &(dyn->head);
  else
    {
      if (off)
        h = &(dyn->tail);
      else
        h = &(dyn->tail->next);
    }
  /*
  ** Append users data to buffer, allocating buffers as needed
  */
  chunk = DBUF_SIZE - off;
  dyn->length += length;
  for ( ;length > 0; h = &(d->next))
    {
      if ((d = *h) == NULL)
        {
          if ((d = (struct DBufBuffer *)dbuf_alloc()) == NULL)
            return dbuf_malloc_error(dyn);
          dyn->tail = d;
          *h = d;
          d->next = NULL;
        }
      if (chunk > length)
        chunk = length;
      memcpy(d->data + off, buf, chunk);
      length -= chunk;
      buf += chunk;
      off = 0;
      chunk = DBUF_SIZE;
    }
  return 1;
}


char* dbuf_map(struct DBuf *dyn, int *length)
{
  if (dyn->head == NULL)
    {
      dyn->tail = NULL;
      *length = 0;
      return NULL;
    }
  *length = DBUF_SIZE - dyn->offset;
  if (*length > dyn->length)
    *length = dyn->length;
  return (dyn->head->data + dyn->offset);
}

void dbuf_delete(struct DBuf *dyn,int length)
{
  struct DBufBuffer *d;
  int chunk;

  if (length > dyn->length)
    length = dyn->length;
  chunk = DBUF_SIZE - dyn->offset;
  while (length > 0)
    {
      if (chunk > length)
        chunk = length;
      length -= chunk;
      dyn->offset += chunk;
      dyn->length -= chunk;
      if (dyn->offset == DBUF_SIZE || dyn->length == 0)
        {
          d = dyn->head;
          dyn->head = d->next;
          dyn->offset = 0;
          dbuf_free(d);
        }
      chunk = DBUF_SIZE;
    }
  if (dyn->head == NULL)
    {
      dyn->length = 0;
      dyn->tail = 0;
    }
}

int dbuf_get(struct DBuf *dyn, char *buf, int length)
{
  int   moved = 0;
  int   chunk;
  char  *b;

  while (length > 0 && (b = dbuf_map(dyn, &chunk)) != NULL)
    {
      if (chunk > length)
        chunk = length;
      memcpy(buf, b, (int)chunk);
      dbuf_delete(dyn, chunk);
      buf += chunk;
      length -= chunk;
      moved += chunk;
    }
  return moved;
}

/*
** dbuf_getmsg
**
** Check the buffers to see if there is a string which is terminated with
** either a \r or \n present.  If so, copy as much as possible (determined by
** length) into buf and return the amount copied - else return 0.
*/
int     dbuf_getmsg(struct DBuf *dyn,char *buf,int length)
{
  struct DBufBuffer       *d;
  register char *s;
  register int  dlen;
  register int  i;
  int   copy;
  
getmsg_init:
  d = dyn->head;
  dlen = dyn->length;
  i = DBUF_SIZE - dyn->offset;
  if (i <= 0)
    return -1;
  copy = 0;
  if (d && dlen)
    s = dyn->offset + d->data;
  else
    return 0;

  if (i > dlen)
    i = dlen;
  while (length > 0 && dlen > 0)
    {
      dlen--;
      if (*s == '\n' || *s == '\r')
        {
          copy = dyn->length - dlen;
          /*
          ** Shortcut this case here to save time elsewhere.
          ** -avalon
          */
          if (copy == 1)
            {
              dbuf_delete(dyn, 1);
              goto getmsg_init;
            }
          break;
        }
      length--;
      if (!--i)
        {
          if ((d = d->next))
            {
              s = d->data;
              i = IRCD_MIN(DBUF_SIZE, dlen);
            }
        }
      else
        s++;
    }

  if (copy <= 0)
    return 0;

  /*
  ** copy as much of the message as wanted into parse buffer
  */
  i = dbuf_get(dyn, buf, IRCD_MIN(copy, length));
  /*
  ** and delete the rest of it!
  */
  if (copy - i > 0)
    dbuf_delete(dyn, copy - i);
  if (i >= 0)
    *(buf+i) = '\0';    /* mark end of messsage */
  
  return i;
}

#ifdef TEST_DBUF
void test_dbuf(void)
{

  /* mika@cs.caltech.edu 6/21/95
     test the size of the dbufs */
  int increment;
  int nextalloc;

  increment = -(int)dbuf_alloc()+(int)dbuf_alloc();
  nextalloc = -(int)dbuf_alloc()+(int)malloc(sizeof(char));
  printf("size: %d increment is %d, nextalloc is %d, pagesize is %d\n",sizeof(struct DBufBuffer),increment,nextalloc,getpagesize());
  return;
}
#endif /* TEST_DBUF */

