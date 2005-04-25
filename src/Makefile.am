# $Id$

AUTOMAKE_OPTIONS = foreign

SUBDIRS = ident resolver

bin_PROGRAMS = ircd

INCLUDES = -I../adns $(INCLTDL)

version.c: 
	$(SHELL) ./version.c.SH 

version.o: version.c
	$(COMPILE) -c version.c -o version.o
	$(MV) version.c version.c.last


# Note GNU bison uses <file>.tab.c not y.tab.c
y.tab.c: ircd_parser.y
	$(YACC) -d ircd_parser.y

lex.yy.o: lex.yy.c ircd_lexer.l 
	$(COMPILE) -c lex.yy.c 

lex.yy.c: ircd_lexer.l y.tab.c
	$(LEX) ircd_lexer.l
	
ircd_SOURCES =                     \
  dns.c                        \
  balloc.c                      \
  cache.c                       \
  channel.c                     \
  class.c                       \
  client.c                      \
  commio.c                      \
  event.c                       \
  getopt.c                      \
  hash.c                        \
  hook.c                        \
  hostmask.c                    \
  irc_string.c                  \
  ircd.c                        \
  ircd_signal.c                 \
  kdparse.c                     \
  linebuf.c                     \
  listener.c                    \
  match.c                       \
  memory.c                      \
  modules.c                     \
  monitor.c                     \
  newconf.c                     \
  numeric.c                     \
  packet.c                      \
  parse.c                       \
  patricia.c                    \
  reject.c                      \
  restart.c                     \
  s_auth.c                      \
  s_conf.c                      \
  s_newconf.c                   \
  s_log.c                       \
  s_serv.c                      \
  s_user.c                      \
  scache.c                      \
  send.c                        \
  snprintf.c                    \
  tools.c                       \
  whowas.c			\
  version.c			\
  @SELECT_TYPE@.c		

if STATIC_MODULES
ircd_LDADD = lex.yy.o y.tab.o @SSL_LIBS@ ../modules/libmodules.a @CRYPT_LIB@
else
ircd_LDADD = lex.yy.o y.tab.o $(LIBLTDL) @SSL_LIBS@ @CRYPT_LIB@
endif

ircd_LDFLAGS = -export-dynamic

if STATIC_MODULES
ircd_DEPENDENCIES = version.o ../modules/libmodules.a  lex.yy.o y.tab.o
else
ircd_DEPENDENCIES = $(LIBLTDLC) version.o lex.yy.o y.tab.o
endif

