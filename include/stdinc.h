/* $Id$ */
#ifndef INCLUDED_stdinc_h
#define INCLUDED_stdinc_h

#include "setup.h"

#include <stdio.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_ERRNO_H
#include <errno.h>
#else
extern int errno;
#endif

#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <time.h>
#include <sys/time.h>

#include <assert.h>

#include "config.h"
#include "tools.h"

#endif
