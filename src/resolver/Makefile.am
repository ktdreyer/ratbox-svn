# $Id$

AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = resolver

resolver_CFLAGS = -DNO_IRCD -I../../adns
# we recycle a lot of code from ircd itself..
# perhaps we should move this stuff into a lib someday
# oh well...
resolver_SOURCES = resolver.c 		\
		../snprintf.c 		\
		../irc_string.c 	\
		../match.c 		\
		../memory.c 		\
		../linebuf.c 		\
		../balloc.c		\
		../tools.c		\
		../event.c

resolver_LDADD = -L../../adns -ladns