# $Id$

AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = ident

ident_CFLAGS = -DIDENT_BUILD -DMAX_CLIENTS=256 -DNO_IRCD
# we recycle a lot of code from ircd itself..
# perhaps we should move this stuff into a lib someday
# oh well...
ident_SOURCES = ident.c 		\
		../commio.c 		\
		../snprintf.c 		\
		../irc_string.c 	\
		../match.c 		\
		../memory.c 		\
		../linebuf.c 		\
		../tools.c 		\
		../balloc.c		\
		../event.c		\
		../@SELECT_TYPE@.c

