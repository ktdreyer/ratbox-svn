#ifndef INCLUDED_headers_h
#define INCLUDED_headers_h

/*
 * Include standard POSIX headers as well as iauth
 * headers
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <signal.h>

/*
 * Socket headers
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

/*
 * IAuth headers
 */

#include "auth.h"
#include "conf.h"
#include "iauth.h"
#include "misc.h"
#include "res.h"
#include "sock.h"
#include "setup.h"

#endif /* INCLUDED_headers_h */
