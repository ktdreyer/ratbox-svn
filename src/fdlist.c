/* 
 *
 * fdlist.c   maintain lists of certain important fds 
 *
 *
 * $Id$
 */
#include "fdlist.h"
#include "client.h"  /* struct Client */
#include "ircd.h"    /* GlobalSetOptions */
#include "s_bsd.h"   /* highest_fd */
#include "config.h"  /* option settings */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

fde_t *fd_table = NULL;

void fdlist_init(void)
{
  static int initialized = 0;
  assert(0 == initialized);
  if (!initialized) {
    /* Since we're doing this once .. */
    fd_table = calloc(MAXCONNECTIONS + 1, sizeof(fde_t));
    /* XXXX I HATE THIS CHECK. Can someone please fix? */
    if (!fd_table)
        exit(69);
    initialized = 1;
  }
}

void fdlist_add(int fd, unsigned char mask)
{
  assert(fd < MAXCONNECTIONS + 1);
  fd_table[fd].mask |= mask;
}
 
void fdlist_delete(int fd, unsigned char mask)
{
  assert(fd < MAXCONNECTIONS + 1);
  fd_table[fd].mask &= ~mask;
}

#ifndef NO_PRIORITY
#ifdef CLIENT_SERVER
#define BUSY_CLIENT(x) \
    (((x)->priority < 55) || (!GlobalSetOptions.lifesux && ((x)->priority < 75)))
#else
#define BUSY_CLIENT(x) \
    (((x)->priority < 40) || (!GlobalSetOptions.lifesux && ((x)->priority < 60)))
#endif
#define FDLISTCHKFREQ  2

/*
 * This is a pretty expensive routine -- it loops through
 * all the fd's, and finds the active clients (and servers
 * and opers) and places them on the "busy client" list
 */
void fdlist_check(time_t now)
{
  struct Client* cptr;
  int            i;

  for (i = highest_fd; i >= 0; --i)
    {

      if (!(cptr = local[i])) 
        continue;
      if (IsServer(cptr) || IsAnOper(cptr))
          continue;

      fd_table[i].mask &= ~FDL_BUSY;
      if (cptr->receiveM == cptr->lastrecvM)
        {
          cptr->priority += 2;  /* lower a bit */
          if (90 < cptr->priority) 
            cptr->priority = 90;
          else if (BUSY_CLIENT(cptr))
            {
              fd_table[i].mask |= FDL_BUSY;
            }
          continue;
        }
      else
        {
          cptr->lastrecvM = cptr->receiveM;
          cptr->priority -= 30; /* active client */
          if (cptr->priority < 0)
            {
              cptr->priority = 0;
              fd_table[i].mask |= FDL_BUSY;
            }
          else if (BUSY_CLIENT(cptr))
            {
              fd_table[i].mask |= FDL_BUSY;
            }
        }
    }
}
#endif

/* Called to open a given filedescriptor */
void
fd_open(int fd, unsigned int type, const char *desc)
{
    fde_t *F = &fd_table[fd];
    assert(fd >= 0);
    if (F->flags.open) {
#ifdef NOTYET
        debug(51, 1) ("WARNING: Closing open FD %4d\n", fd);
#endif
        fd_close(fd);
    }
    assert(!F->flags.open);
#ifdef NOTYET
    debug(51, 3) ("fd_open FD %d %s\n", fd, desc);
#endif
    F->type = type;
    F->flags.open = 1;
#ifdef NOTYET
    F->defer.until = 0;
    F->defer.n = 0;
    F->defer.handler = NULL;
    fdUpdateBiggest(fd, 1);
#endif
    if (desc)
        strncpy(F->desc, desc, FD_DESC_SZ);
#ifdef NOTYET
    Number_FD++;
#endif
}


/* Called to close a given filedescriptor */
void
fd_close(int fd)
{
    fde_t *F = &fd_table[fd];
    /* All disk fd's MUST go through file_close() ! */
    assert(F->type != FD_FILE);
    if (F->type == FD_FILE) {
        assert(F->read_handler == NULL);
        assert(F->write_handler == NULL);
    }
#ifdef NOTYET
    debug(51, 3) ("fd_close FD %d %s\n", fd, F->desc);
#endif
    comm_setselect(fd, COMM_SELECT_WRITE|COMM_SELECT_READ, NULL, NULL, 0);
    F->flags.open = 0;
#if NOTYET
    fdUpdateBiggest(fd, 0);
    Number_FD--;
#endif
    memset(F, '\0', sizeof(fde_t));
    F->timeout = 0;
}

