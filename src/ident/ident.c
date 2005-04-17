/*
 * ident.c: identd check daemon for ircd-ratbox
 * Based on many things ripped from ratbox-services
 * and ircd-ratbox itself and who knows what else
 *
 * Strangely this looks like resolver.c but I can't 
 * seem to figure out why...
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 * Copyright (C) 2005 Aaron Sethman <androsyn@ratbox.org>
 *
 * $Id$
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

struct timeval SystemTime;

#include "setup.h"
#include "memory.h"
#include "defs.h"
#include "commio.h"
#include "tools.h"
#include "balloc.h"
#include "linebuf.h"
#include "irc_string.h"
#define USERLEN 10

/* data fd from ircd */
int irc_ifd;
/* control fd from ircd */
int irc_ofd; 

#define MAXPARA 10
#define REQIDLEN 10

#define REQREV 0
#define REQFWD 1

#define REVIPV4 0
#define REVIPV6 1
#define REVIPV6INT 2
#define REVIPV6FALLBACK 3
#define FWDHOST 4

#define EmptyString(x) (!(x) || (*(x) == '\0'))

struct auth_request
{
	struct irc_sockaddr_storage bindaddr;
	struct irc_sockaddr_storage destaddr;
	int srcport;
	int dstport;
	char reqid[REQIDLEN];
	int authfd;
};

static BlockHeap *authheap;

static char buf[512]; /* scratch buffer */
static char readBuf[READBUF_SIZE];

static buf_head_t recvq;
static buf_head_t sendq;

static int
send_sprintf(int fd, const char *format, ...)
{
        va_list args; 
        va_start(args, format);
        vsprintf(buf, format, args);
        va_end(args); 
        return(send(fd, buf, strlen(buf), 0));
}

void ilog(char *errstr, ...)
{
	exit(2);
}

void restart(char *msg)
{
	exit(1);
}



/* io_to_array()
 *   Changes a given buffer into an array of parameters.
 *   Taken from ircd-ratbox.
 *
 * inputs	- string to parse, array to put in
 * outputs	- number of parameters
 */
static inline int
io_to_array(char *string, char *parv[MAXPARA])
{
	char *p, *buf = string;
	int x = 0;

	parv[x] = NULL;

        if(EmptyString(string))
                return x;

	while (*buf == ' ')	/* skip leading spaces */
		buf++;
	if(*buf == '\0')	/* ignore all-space args */
		return x;

	do
	{
		if(*buf == ':')	/* Last parameter */
		{
			buf++;
			parv[x++] = buf;
			parv[x] = NULL;
			return x;
		}
		else
		{
			parv[x++] = buf;
			parv[x] = NULL;
			if((p = strchr(buf, ' ')) != NULL)
			{
				*p++ = '\0';
				buf = p;
			}
			else
				return x;
		}
		while (*buf == ' ')
			buf++;
		if(*buf == '\0')
			return x;
	}
	while (x < MAXPARA - 1);

	if(*p == ':')
		p++;

	parv[x++] = p;
	parv[x] = NULL;
	return x;
}


/*
request protocol:

INPUTS:

ID bindhost 4/6 destaddr srcport dstport

Note that the srcport/dsport parameters are for the identd request

OUTPUTS:

ID success/failure username

*/

static void
write_sendq(int fd, void *unused)
{
        int retlen;
        if(linebuf_len(&sendq) > 0)
        {
                while((retlen = linebuf_flush(irc_ofd, &sendq)) > 0);
                if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
                {
                        exit(1);
                }
        }
         
        if(linebuf_len(&sendq) > 0)
        {
                comm_setselect(irc_ofd, FDLIST_SERVICE, COMM_SELECT_WRITE,
                               write_sendq, NULL, 0);
        }
}
 


static void
read_auth_timeout(int fd, void *data)
{
	struct auth_request *auth = data;
	linebuf_put(&sendq, "%s 0", auth->reqid);
	write_sendq(irc_ofd, NULL);
	BlockHeapFree(authheap, auth);
	comm_close(fd);
}


static char *
GetValidIdent(char *buf)
{
        int remp = 0;
        int locp = 0;     
        char *colon1Ptr;
        char *colon2Ptr;
        char *colon3Ptr;
        char *commaPtr;
        char *remotePortString;

        /* All this to get rid of a sscanf() fun. */
        remotePortString = buf;

        colon1Ptr = strchr(remotePortString, ':');
        if(!colon1Ptr)   
                return NULL;

        *colon1Ptr = '\0';
        colon1Ptr++;
        colon2Ptr = strchr(colon1Ptr, ':');
        if(!colon2Ptr)   
                return NULL;

        *colon2Ptr = '\0';
        colon2Ptr++;
        commaPtr = strchr(remotePortString, ',');

        if(!commaPtr)
                return NULL;

        *commaPtr = '\0';
        commaPtr++;

        remp = atoi(remotePortString);
        if(!remp)
                return NULL;

        locp = atoi(commaPtr);
        if(!locp)
                return NULL;

        /* look for USERID bordered by first pair of colons */
        if(!strstr(colon1Ptr, "USERID"))
                return NULL;

        colon3Ptr = strchr(colon2Ptr, ':');
        if(!colon3Ptr)
                return NULL;

        *colon3Ptr = '\0';
        colon3Ptr++;
        return (colon3Ptr);
}


static void
read_auth(int fd, void *data)
{
	struct auth_request *auth = data;
	char username[USERLEN], *s, *t;
	int len, count;

	len = recv(fd, buf, sizeof(buf), 0);
	if(len < 0 && ignoreErrno(errno))
	{
		comm_settimeout(fd, 30, read_auth_timeout, auth);
		comm_setselect(fd, FDLIST_SERVICE, COMM_SELECT_READ, read_auth, auth, 30);
		return;
	} else {
		buf[len] = '\0';
		if((s = GetValidIdent(buf)))
		{
			t = username;
			while(*s == '~' || *s == '^')
				s++;
			for(count = USERLEN; *s && count; s++)
			{
				if(*s == '@')
					break;
				if(!isspace(*s) && *s != ':' && *s != '[')
				{
					*t++ = *s;
					count--;
				}
			}
			*t = '\0';
			linebuf_put(&sendq, "%s %s", auth->reqid, username);
		} else
			linebuf_put(&sendq, "%s 0", auth->reqid);
		write_sendq(irc_ofd, NULL);
		comm_close(fd);
		BlockHeapFree(authheap, auth);
	}
}

static void
connect_callback(int fd, int status, void *data)
{
	struct auth_request *auth = data;
	if(status == COMM_OK)
	{
		/* one shot at the send, socket buffers should be able to handle it
		 * if not, oh well, you lose
		 */
		if(send_sprintf(fd, "%u , %u\r\n", auth->srcport, auth->dstport) <= 0)
		{
			linebuf_put(&sendq, "%s 0", auth->reqid);
			write_sendq(irc_ofd, NULL);
			comm_close(fd);
			BlockHeapFree(authheap, auth);
			return;
		}
		read_auth(fd, auth);
	} else {
		linebuf_put(&sendq, "%s 0", auth->reqid);
		write_sendq(irc_ofd, NULL);
		comm_close(fd);
		BlockHeapFree(authheap, auth);
	}
}

void
check_identd(const char *id, const char *bindaddr, const char *destaddr, const char *srcport, const char *dstport)
{
	struct auth_request *auth;
	int aftype = AF_INET;
	auth = BlockHeapAlloc(authheap);

	inetpton_sock(bindaddr, (struct sockaddr *)&auth->bindaddr);
	inetpton_sock(destaddr, (struct sockaddr *)&auth->destaddr);

#ifdef IPV6
	if(((struct sockaddr *)&destaddr)->sa_family == AF_INET6)
		((struct sockaddr_in6 *)&auth->destaddr)->sin6_port = htons(113);
	else
#endif
		((struct sockaddr_in *)&auth->destaddr)->sin_port = htons(113);

	auth->srcport = atoi(srcport);
	auth->dstport = atoi(dstport);
	strcpy(auth->reqid, id);

	auth->authfd = comm_socket(aftype, SOCK_STREAM, 0, "auth fd");
	comm_connect_tcp(auth->authfd, (struct sockaddr *)&auth->destaddr, 
		(struct sockaddr *)&auth->bindaddr, sizeof(struct sockaddr_in), connect_callback, auth, 30);
                                  
}

static void
parse_auth_request(void)
{
        int len;
	static char *parv[MAXPARA + 1];
	int parc;
        while((len = linebuf_get(&recvq, readBuf, sizeof(readBuf),
                                 LINEBUF_COMPLETE, LINEBUF_PARSED)) > 0)
        {
		parc = io_to_array(readBuf, parv);
		if(parc != 5)
			exit(1);
		check_identd(parv[0], parv[1], parv[2], parv[3], parv[4]);
	}
}

static void
read_auth_request(int fd, void *data)
{
        int length;

        while((length = read(irc_ifd, readBuf, sizeof(readBuf))) > 0)
        {
                linebuf_parse(&recvq, readBuf, length, 0);
                parse_auth_request();
        }
         
        if(length == 0)
                exit(1);

        if(length == -1 && !ignoreErrno(errno))
                exit(1);

        comm_setselect(irc_ifd, FDLIST_SERVICE, COMM_SELECT_READ, read_auth_request, NULL, 0);
}

void
set_time(void)
{
	gettimeofday(&SystemTime, 0);
}

int main(int argc, char **argv)
{
	char *tifd;
	char *tofd;
	fdlist_init();
	init_netio();	
	tifd = getenv("IFD");
	tofd = getenv("OFD");
	if(tifd == NULL || tofd == NULL)
		exit(1);
	irc_ifd = atoi(tifd);
	irc_ofd = atoi(tofd);
	init_dlink_nodes();
	initBlockHeap();
	linebuf_init();	
	linebuf_newbuf(&sendq);
	linebuf_newbuf(&recvq);
	authheap = BlockHeapCreate(sizeof(struct auth_request), 2048);

	comm_open(irc_ifd, FD_PIPE, "ircd ifd");
	comm_open(irc_ofd, FD_PIPE, "ircd ofd");
	comm_set_nb(irc_ifd);
	comm_set_nb(irc_ofd);
	
	read_auth_request(irc_ifd, NULL);
	while(1) {
		comm_select(1000);
		comm_checktimeouts(NULL);
	}
	exit(0);
}


