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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "../../include/setup.h"
#include "defs.h"
#include "tools.h"
#include "commio.h"
#include "mem.h"

/* data fd from ircd */
int fd;
/* control fd from ircd */
int cfd; 

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


fd_set readfds;
fd_set writefds;
fd_set exceptfds;

int fd = -1;

static void report_error(char *errstr)
{
	char buf[512];
	sprintf(buf, "ERR %s: %s", errstr, strerror(errno)); 
	send(cfd, buf, strlen(buf), 0);
	exit(-1);
}


/* get_line()
 *   Gets a line of data from a given connection
 *
 * inputs	- connection to get line for, buffer to put in, size of buffer
 * outputs	- characters read
 */
static int
get_line(int fd, char *buf, int bufsize)
{
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


struct auth_request
{
	union
	{
		struct sockaddr_in in;
#ifdef IPV6
		struct sockaddr_in6 in6;
#endif
	} bindaddr;
	union
	{
		struct sockaddr_in in;
#ifdef IPV6		
		struct sockaddr_in6 in6;
#endif
	} destaddr;
	int srcport;
	int dstport;
	char reqid[REQIDLEN];
	int authfd;
};

static void
read_auth(int fd, void *data)
{
	char buf[512];
	int len;
	len = recv(fd, buf, sizeof(buf), 0);
	if(len < 0 && ignoreErrno(errno))
	{
		comm_setselect(fd, FDLIST_SERVICE, COMM_SELECT_READ, read_auth, NULL, 15);
		return;
	} else
		return;
	
}

static void
connect_callback(int fd, int status, void *data)
{
	char buf[512];
	if(status == COMM_OK)
	{
		sprintf(buf, "%u, %u\r\n", 1, 2);
		send(fd, buf, strlen(buf), 0);
		read_auth(fd, NULL);
	} else {
		fprintf(stderr, "callback got: %d\n", status);
		comm_close(fd);
	}
}

void
check_identd(const char *id, const char *bindaddr, const char *aft, const char *destaddr, const char *srcport, const char *dstport)
{
	struct auth_request *auth;
	char buf[512];
	auth = MyMalloc(sizeof(struct auth_request));
	int aftype = AF_INET;
#ifdef IPV6
	if(*aft == '6') 
	{
		aftype = AF_INET6;
		inet_pton(AF_INET6, bindaddr, &auth->bindaddr.in6.sin6_addr);
		auth->bindaddr.in6.sin6_family = AF_INET6;
#ifdef SOCKADDR_IN_HAS_LEN
		auth->binaddr.in6.sin6_len = sizeof(struct sockaddr_in6);
#endif
	}
	else 
#else /* ipv6*/
	{
		inet_aton(bindaddr, &auth->bindaddr.in.sin_addr);
		auth->bindaddr.in.sin_family = AF_INET;
#ifdef SOCKADDR_IN_HAS_LEN
		auth->bindaddr.in.sin_len = sizeof(struct sockaddr_in);
#endif
	}
#endif /* ipv6*/

#ifdef IPV6
	if(*aft == '6')
	{
		aftype = AF_INET6;
		inet_pton(AF_INET6, destaddr, &auth->destaddr.in6.sin6_addr);
		auth->destaddr.in6.sin6_family = AF_INET6;
#ifdef SOCKADDR_IN_HAS_LEN
		auth->destaddr.in6.sin6_len = sizeof(struct sockaddr_in6);
#endif
		auth->destaddr.in6.sin6_port = htons(113);

	} else
#endif
	{
		inet_aton(destaddr, &auth->destaddr.in.sin_addr);
		auth->destaddr.in.sin_family = AF_INET;
#ifdef SOCKADDR_IN_HAS_LEN
		auth->destaddr.in.sin_len = sizeof(struct sockaddr_in);
#endif
		auth->destaddr.in.sin_port = htons(113);
	}		
	auth->srcport = atoi(srcport);
	auth->dstport = atoi(dstport);

	auth->authfd = comm_socket(aftype, SOCK_STREAM, 0);
	comm_connect_tcp(auth->authfd, (struct sockaddr *)&auth->destaddr, 
		(struct sockaddr *)&auth->bindaddr, sizeof(struct sockaddr_in), connect_callback, NULL, 1);
                                  
}



static void
process_request(int fd, void *data)
{
	char *p;
	char buf[512];
	int parc;
	static char *parv[MAXPARA + 1];
	int n;

	while(1)
	{
		if((n = read(fd, buf, sizeof(buf))) <= 0)
		{
			if(n == -1 && ignoreErrno(errno))
				break;
			/* report error */
		}

	        if((p = memchr(buf, '\n', n)) != NULL)
	        {
	                n = p - buf + 1;
	                *p = '\0';
		
			parc = io_to_array(buf, parv);
			check_identd(parv[0], parv[1], parv[2], parv[3], parv[4], parv[5]);
	        }        
//	        else report error
	}

	comm_setselect(fd, FDLIST_SERVICE, COMM_SELECT_READ, process_request, NULL, 0);
	return;	
}


int main(int argc, char **argv)
{
	char *tfd;
	char *tcfd;
	int res;
	int x = 2, i;
	init_netio();	
	tfd = getenv("FD");
	tcfd = getenv("CFD");
	if(tfd == NULL && tcfd == NULL)
		exit(0);
	fd = atoi(tfd);
	cfd = atoi(tcfd);

	comm_open(fd, FD_SOCKET);
//	comm_open(cfd, FD_SOCKET);

	comm_set_nb(fd);
//	comm_set_nb(cfd);

	process_request(fd, NULL);
	
//	check_identd(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);	
	while(1) {
		comm_select(1000);
		comm_checktimeouts(NULL);
	}
//	check_identd("1", "10.123.2.101", "4", "207.188.202.227", "0", "1");
	exit(0);
}


