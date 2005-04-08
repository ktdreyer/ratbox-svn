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


#include "../../include/setup.h"
#include "mem.h"
#include "defs.h"
#include "tools.h"
#include "commio.h"

#define USERLEN 10

/* data fd from ircd */
int irc_fd;
/* control fd from ircd */
int irc_cfd; 

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
static char buf[512]; /* scratch buffer */

fd_set readfds;
fd_set writefds;
fd_set exceptfds;


static int
send_sprintf(int fd, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsprintf(buf, format, args); 
	va_end(args);
	return(send(fd, buf, strlen(buf), 0));
}

static void report_error(char *errstr, ...)
{
	va_list ap;
	va_start(ap, errstr);
	vsprintf(buf, errstr, ap);
	va_end(ap);
	send(irc_cfd, buf, strlen(buf), 0);
	exit(-1);
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
read_auth_timeout(int fd, void *data)
{
	struct auth_request *auth = data;
	send_sprintf(irc_fd, "%s 0\n", auth->reqid);
	MyFree(auth);
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
		comm_settimeout(fd, 15, read_auth_timeout, auth);
		comm_setselect(fd, FDLIST_SERVICE, COMM_SELECT_READ, read_auth, auth, 15);
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
			send_sprintf(irc_fd, "%s %s\n", auth->reqid, username);	
		} else
			send_sprintf(irc_fd, "%s 0\n", auth->reqid);
		comm_close(fd);
		MyFree(auth);
	}
	return;
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
			send_sprintf(irc_fd, "%s 0\n", auth->reqid);
			comm_close(fd);
			MyFree(auth);
			return;
		}
		read_auth(fd, auth);
	} else {
		send_sprintf(irc_fd, "%s 0\n", auth->reqid);
		comm_close(fd);
		MyFree(auth);
	}
}

void
check_identd(const char *id, const char *bindaddr, const char *aft, const char *destaddr, const char *srcport, const char *dstport)
{
	struct auth_request *auth;
	int aftype = AF_INET;
	auth = MyMalloc(sizeof(struct auth_request));
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
	strcpy(auth->reqid, id);

	auth->authfd = comm_socket(aftype, SOCK_STREAM, 0);
	comm_connect_tcp(auth->authfd, (struct sockaddr *)&auth->destaddr, 
		(struct sockaddr *)&auth->bindaddr, sizeof(struct sockaddr_in), connect_callback, auth, 5);
                                  
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
			else
				report_error("ERR: read failed: %s\n", strerror(errno));
		}

	        if((p = memchr(buf, '\n', n)) != NULL)
	        {
	                n = p - buf + 1;
	                *p = '\0';
		
			parc = io_to_array(buf, parv);
			if(parc != 6)
				report_error("ERR: wrong number of arguments passed\n");
			check_identd(parv[0], parv[1], parv[2], parv[3], parv[4], parv[5]);
	        } else
			report_error("ERR: Got bogus data from server");       
	}

	comm_setselect(fd, FDLIST_SERVICE, COMM_SELECT_READ, process_request, NULL, 0);
	return;	
}

static void
process_ctrl(int fd, void *unused)
{
	char nbuf[1];
	int len;
	while(1)
	{
		if((len = recv(fd, nbuf, sizeof(buf), 0)) <= 0)
		{
			if(len == -1 && ignoreErrno(errno))
				break;
			else
				exit(0); /* control socket is gone..so are we */
		} 
		/* eventually *do* something with the data */
	}	
	comm_setselect(fd, FDLIST_SERVER, COMM_SELECT_READ, process_ctrl, NULL, 0);
}

int main(int argc, char **argv)
{
	char *tfd;
	char *tcfd;

	init_netio();	
	tfd = getenv("FD");
	tcfd = getenv("CFD");
	if(tfd == NULL && tcfd == NULL)
		exit(0);
	irc_fd = atoi(tfd);
	irc_cfd = atoi(tcfd);

	comm_open(irc_fd, FD_SOCKET);
	comm_open(irc_cfd, FD_SOCKET);

	comm_set_nb(irc_fd);
	comm_set_nb(irc_cfd);

	process_request(irc_fd, NULL);
	process_ctrl(irc_cfd, NULL);	
	while(1) {
		comm_select(1000);
		comm_checktimeouts(NULL);
	}
	exit(0);
}


