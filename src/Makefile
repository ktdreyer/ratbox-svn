
# a simple makefile for compiling alis under U*X

CC=gcc
LD=gcc
CFLAGS=-c -g -O -Wall
LDFLAGS=
SRCS=			\
	c_mode.c	\
	channel.c	\
	client.c	\
	command.c	\
	conf.c		\
	event.c		\
	fileio.c	\
	log.c		\
	match.c		\
	io.c		\
	rserv.c		\
	tools.c

OBJS=${SRCS:.c=.o}

BKUPS=*~
PROG=rserv
RM=rm
RMOPTS=-f

all: $(PROG)

$(PROG): $(OBJS)
	$(LD) $(LDFLAGS) -o $(PROG) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -I ../include -O0 -g -o $@ $< 

cleanobj:
	$(RM) $(RMOPTS) $(PROG) *.o

cleanbk:	
	$(RM) $(RMOPTS) $(BKUPS)

mrproper: cleanobj cleanbk

clean: cleanobj

distclean: mrproper

install:

uninstall:

#depend:
#	$(CC) -MM $(CFLAGS) $(SRCS) > .depend

#include .depend
