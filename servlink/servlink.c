/************************************************************************
 *   IRC - Internet Relay Chat, servlink/servlink.c
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

#include "setup.h"                                                   

#include <sys/types.h>
#include <sys/time.h>

#include <sys/socket.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef HAVE_LIBCRYPTO
#include <openssl/evp.h>
#include <openssl/err.h>
#endif
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#include "servlink.h"
#include "io.h"
#include "control.h"

static void usage(void);

struct slink_state       in_state;
struct slink_state       out_state;

struct fd_table          fds[5] =
        {
          { 0,  read_ctrl, NULL },
          { 0,       NULL, NULL },
          { 0,       NULL, NULL },
          { 0,       NULL, NULL },
          { 0,       NULL, NULL }
        };


/* usage();
 *
 * Display usage message
 */
static void usage(void)
{
  fprintf(stderr, "ircd-hybrid server link v1.0-beta\n");
  fprintf(stderr, "2001-05-23\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "This program is called by the ircd-hybrid ircd.\n");
  fprintf(stderr, "It cannot be used on its own.\n");
  exit(1);
}

int main(int argc, char *argv[])
{
  fd_set rfds;
  fd_set wfds;
  int max_fd = 0;
  int i;
#ifdef SERVLINK_DEBUG
  int GDBAttached = 0;

  while (!GDBAttached)
    sleep(1);
#endif

#ifdef HAVE_LIBCRYPTO
  /* load error strings */
  ERR_load_crypto_strings();
#endif

  /* Make sure we are running under hybrid.. */
  if (argc != 6 || strcmp(argv[0], "-slink"))
    usage(); /* exits */

  for (i = 0; i < 5; i++ )
  {
    fds[i].fd = atoi(argv[i+1]);
    if (fds[i].fd < 0)
      exit(1);
    if (fds[i].fd > max_fd)
      max_fd = fds[i].fd;
  }
  
  /* set file descriptors to nonblocking mode */
  for (i = 0; i < 5; i++)
  {
    fcntl(fds[i].fd, F_SETFL, O_NONBLOCK);
  }

  /* The following FDs should be open:
   *
   * 3 - bi/uni-directional pipe for control commands
   * 4 - bi/uni-directional pipe for data
   * 5 - bi-directional socket connected to remote server
   * maybe:
   * 6 - uni-directional (write) pipe for control commands
   * 7 - uni-directional (write) pipe for data
   */

  /* loop forever */
  while (1)
  {
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    
    for (i = 0; i < 5; i++)
    {
      if (fds[i].read_cb)
        FD_SET(i, &rfds);
      if (fds[i].write_cb)
        FD_SET(i, &wfds);
    }
      
    /* we have <6 fds ever, so I don't think select is too painful */
    if (select((max_fd + 1), &rfds, &wfds, NULL, NULL))
    {
     /* call any callbacks */
      for (i = 0; i <= max_fd; i++)
      {
        if (FD_ISSET(i, &rfds) && fds[i].read_cb)
          (*fds[i].read_cb)();
        if (FD_ISSET(i, &wfds) && fds[i].write_cb)
          (*fds[i].write_cb)();
      }
    }
  }

  /* NOTREACHED */
  exit(0);
} /* main() */
