# $Id$

all: rsa_respond genkey respond
rsa_respond: buffer.c buffer.h rsa_respond.c
	gcc -O2 -Wall -pedantic -o rsa_respond buffer.c rsa_respond.c -lcrypto -ggdb

genkey: genkey.c
	gcc -O2 -Wall -pedantic -o genkey genkey.c -lcrypto

respond: respond.c
	gcc -O3 -Wall -pedantic -o respond respond.c -lcrypto -ggdb

clean:
	rm -f rsa_respond *.core *~
