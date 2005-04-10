# $Id$

AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = ident

ident_CFLAGS = -DIDENT_BUILD -DMAX_CLIENTS=256
ident_SOURCES = ident.c ../commio.c ../snprintf.c ../irc_string.c ../match.c ../memory.c ../@SELECT_TYPE@.c

