# MMS/MMK Makefile for OpenVMS
# Copyright (c) 2001 Edward Brocklesby
# $Id$

CC=	CC
CFLAGS=	/INCLUDE_DIRECTORY=[-.INCLUDE]/STANDARD=ISOC94
LDFLAGS=

OBJECTS=	SERVLINK,IO

SERVLINK.EXE : SERVLINK.OLB($(OBJECTS))
	$(LINK)$(LDFLAGS)/EXECUTABLE=SERVLINK $(OBJECTS)

CLEAN : 
	DELETE *.OBJ;*
