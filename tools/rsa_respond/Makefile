# $Id$

all: genkey respond
genkey: genkey.c
	gcc -O2 -Wall -pedantic -o genkey genkey.c -lcrypto

respond: respond.c
	gcc -O3 -Wall -pedantic -o respond respond.c -lcrypto -ggdb

clean:
	rm -f genkey respond *.core *~
