/*
 * resolver.c: dns resolving daemon for ircd-ratbox
 * Based on many things ripped from ratbox-services
 * and ircd-ratbox itself and who knows what else
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
#include "adns.h"

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

static void process_request(void);
static void resolve_ip(char **parv);
static void resolve_host(char **parv);


struct dns_request
{
	char reqid[REQIDLEN];
	int reqtype;
	int revfwd;
	adns_query query;
	union {
#ifdef IPV6
		struct sockaddr_in6 in6;
#endif
		struct sockaddr_in in;
	} sins;
#ifdef IPV6
	int fallback;
#endif
};

fd_set readfds;
fd_set writefds;
fd_set exceptfds;

adns_state dns_state;
int fd = -1;


static void
restart_resolver(int sig)
{
        /* Rehash dns configuration */
        adns__rereadconfig(dns_state);
}

static void
setup_signals(void)
{
        struct sigaction act;
        act.sa_flags = 0;
        act.sa_handler = SIG_IGN;
        sigemptyset(&act.sa_mask);
        act.sa_handler = restart_resolver;
        sigaddset(&act.sa_mask, SIGHUP);
        sigaction(SIGHUP, &act, 0);
}

static void report_error(char *errstr)
{
	char buf[512];
	sprintf(buf, "ERR %s: %s", errstr, strerror(errno)); 
	send(cfd, buf, strlen(buf), 0);
	exit(-1);
}

static void *MyMalloc(size_t size)
{
	void *ptr;
	ptr = calloc(1, size);
	if(ptr == NULL)
	{
		report_error("MyMalloc failed, giving up");
		exit(1);
	}
	return(ptr);
}

static  void MyFree(void *ptr)
{
	if(ptr != NULL)
		free(ptr);
}

/* stolen from squid */
static int
ignore_errno(int ierrno)
{
	switch(ierrno)
	{
		case EINPROGRESS:
		case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
		case EAGAIN:
#endif
		case EALREADY:
		case EINTR:
#ifdef ERESTART
		case ERESTART:
#endif
			return 1;

		default:
			return 0;
	}
}

/* get_line()
 *   Gets a line of data from a given connection
 *
 * inputs	- connection to get line for, buffer to put in, size of buffer
 * outputs	- characters read
 */
static int
get_line(char *buf, int bufsize)
{
	char *p;
	int n = bufsize;

	if((n = read(fd, buf, n)) <= 0)
	{
		if(n == -1 && ignore_errno(errno))
			return 0;
		return -1;	
	}

        if((p = memchr(buf, '\n', n)) != NULL)
        {
                n = p - buf + 1;
                *p = '\0';
        }        
        else 
                return 0;
	
	return n;
}

static void send_answer(struct dns_request *req, adns_answer *reply)
{
	char buf[512];
	char response[64];
	int result = 0;
	int aftype = 0;
	if(reply && reply->status == adns_s_ok)
	{
		switch(req->revfwd)
		{
			case REQREV:
			{
				if(strlen(*reply->rrs.str) < 63)
				{
					strcpy(response, *reply->rrs.str);
					result = 1;
				} else {
					strcpy(response, "HOSTTOOLONG");
					result = 0;
				}
				break;
			}

			case REQFWD:
			{
				switch(reply->type)
				{
#ifdef IPV6
					case adns_r_addr6:
					{
						char tmpres[65];
						inet_ntop(AF_INET6, &reply->rrs.addr->addr.inet6.sin6_addr, tmpres, sizeof(tmpres)-1);
						aftype = 6;
						if(*tmpres == ':')
						{
							strcpy(response, "0");
							strcat(response, tmpres);
						} else
							strcpy(response, tmpres);
						result = 1;
						break;
					}
#endif
					case adns_r_addr:
					{
						result = 1;
						aftype = 4;
						strcpy(response, inet_ntoa(reply->rrs.addr->addr.inet.sin_addr));
						break;
					} 
					default:
					{
						strcpy(response, "FAILED");
						result = 0;
						aftype = 0;
						break;
					}						
				}
				break;
			}
			default:
			{
				snprintf(buf, sizeof(buf), "I have an revfwd type of %d, and I don't know what to do!", req->revfwd);
				report_error(buf);				
				break;				
			}				
		}

	} 
	else
	{
#ifdef IPV6
		if(req->revfwd == REQREV && req->reqtype == REVIPV6FALLBACK && req->fallback == 0)
		{
			req->fallback = 1;
		        result = adns_submit_reverse(dns_state,
                                    (struct sockaddr *) &req->sins.in6,
                                    adns_r_ptr_ip6_old,
                                    adns_qf_owner | adns_qf_cname_loose |
                                    adns_qf_quoteok_anshost, req, &req->query);
              		MyFree(reply);
			if(result != 0)
			{
				snprintf(buf, sizeof(buf), "%s 0 FAILED\n", req->reqid);
				send(fd, buf, strlen(buf));
				MyFree(reply);
				MyFree(req);

			}							
			return;
		}
#endif
		strcpy(response, "FAILED");
		result = 0;
	}
	snprintf(buf, sizeof(buf), "%s %d %d %s\n", req->reqid, result, aftype, response);
	if(send(fd, buf, strlen(buf), 0) == -1)
		report_error("send failure");
	MyFree(reply);
	MyFree(req);
}


static void process_adns_incoming(void)
{
	adns_query q, r;
	adns_answer *answer;
	struct dns_request *req;
	int failure = 0;
		
	adns_forallqueries_begin(dns_state);
	while(   (q = adns_forallqueries_next(dns_state, (void *)&r)) != NULL)
	{
		switch(adns_check(dns_state, &q, &answer, (void **)&req))
		{
			case EAGAIN:
				continue;
			case 0:
				send_answer(req, answer);
				continue;
			default:
                        	if(answer != NULL && answer->status == adns_s_systemfail)
					failure = 1;
				send_answer(req, NULL);
				break;
		}

	}
}


/* read_io()
 *   The main IO loop for reading/writing data.
 *
 * inputs	-
 * outputs	-
 */
static void
read_io(void)
{
	struct timeval *tv, tvbuf, now, tvx;
	int select_result;
	int maxfd = -1;

	while(1)
	{
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);

		FD_SET(fd, &readfds);
		FD_SET(cfd, &exceptfds);
		FD_SET(cfd, &readfds);
		
		if(fd > cfd)
			maxfd = fd+1;
		else
			maxfd = cfd+1;

		gettimeofday(&now, 0);
		adns_beforeselect(dns_state, &maxfd, &readfds, &writefds, &exceptfds, &tv, &tvbuf, &now);
		tvx.tv_sec = 1;
		tvx.tv_usec = 0;
		select_result = select(maxfd, &readfds, &writefds, &exceptfds, &tvx);
		adns_afterselect(dns_state, maxfd, &readfds,&writefds,&exceptfds, &now);
		process_adns_incoming();

		if(select_result == 0)
			continue;

		/* have data to parse */
		if(select_result > 0)
		{
			if(FD_ISSET(fd, &readfds))
			{
				process_request();
				/* incoming requests */
			}
			if(FD_ISSET(cfd, &exceptfds) || FD_ISSET(cfd, &readfds))
			{
				/* its dead cap'n */
				exit(1);
			}
		}
		
	}
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

IPTYPE:    4, 5,  6, ipv4, ipv6.int/arpa, ipv6 respectively
requestid: identifier of the request
 

RESIP  requestid IPTYPE IP 
RESHST requestid IPTYPE hostname

OUTPUTS:
ERR error string = daemon failed and is going to shutdown
otherwise

FWD requestid PASS/FAIL hostname or reason for failure
REV requestid PASS/FAIL IP or reason

*/

static void
process_request(void)
{
	char buf[512];
	int parc;
	static char *parv[MAXPARA + 1];


	if(get_line(buf, sizeof(buf)) <= 0)
	{
		report_error("Failed reading line");
	}
	parc = io_to_array(buf, parv);

	if(parc != 4)
	{
		report_error("Server didn't send me the right amount of arguments, I quit");
	}

	switch(*parv[0])
	{
	
		case 'I':
			resolve_ip(parv);
			break;
		case 'H':
			resolve_host(parv);
			break;
		default:
			return;
			
	}
}

static void
resolve_host(char **parv)
{
	struct dns_request *req;
	char *requestid = parv[1];
	char *iptype = parv[2];
	char *rec = parv[3];
	int result;
	int flags;
	req = MyMalloc(sizeof(struct dns_request));
	strcpy(req->reqid, requestid);

	req->revfwd = REQFWD;
	req->reqtype = FWDHOST; 
	switch(*iptype)
	{
#ifdef IPV6
		case '5': /* I'm not sure why somebody would pass a 5 here, but okay */
		case '6':
			flags = adns_r_addr6;
			break;
#endif
		default:
			flags = adns_r_addr;		
			break;
	}
	result = adns_submit(dns_state, rec, flags, adns_qf_owner, req, &req->query);
	if(result != 0)
	{
		/* Failed to even submit */
		send_answer(req, NULL);
	}

}


static void
resolve_ip(char **parv)
{
	char *requestid = parv[1];
	char *iptype = parv[2];
	char *rec = parv[3];			
	struct dns_request *req;

	int result; 
	int flags = adns_r_ptr;


	if(strlen(requestid) >= REQIDLEN)
	{
		report_error("Server sent me a requestid that was too long");
	}
	req = MyMalloc(sizeof(struct dns_request));
	req->revfwd = REQREV;
	strcpy(req->reqid, requestid);
	switch(*iptype)
	{
		case '4':
			flags = adns_r_ptr;
			req->reqtype = REVIPV4;
			if(!inet_aton(rec, &req->sins.in.sin_addr))
				report_error("Invalid address passed");
			req->sins.in.sin_family = AF_INET;

			break;
#ifdef IPV6
		case '5': /* This is the case of having to fall back to "ip6.int" */
			req->reqtype = REVIPV6FALLBACK;
			flags = adns_r_ptr_ip6;
			inet_pton(AF_INET6, rec, &req->sins.in6.sin6_addr);
			req->sins.in6.sin6_family = AF_INET6;
			req->fallback = 0;
			break;
		case '6':
			req->reqtype = REVIPV6;
			flags = adns_r_ptr_ip6;
			inet_pton(AF_INET6, rec, &req->sins.in6.sin6_addr);
			req->sins.in6.sin6_family = AF_INET6;
			break;
#endif
		default:
			report_error("Server sent invalid IP type");
	}

        result = adns_submit_reverse(dns_state,
                                    (struct sockaddr *) &req->sins,
                                    flags,
                                    adns_qf_owner | adns_qf_cname_loose |
                                    adns_qf_quoteok_anshost, req, &req->query);
		
	if(result != 0)
	{
		send_answer(req, NULL);		
	}
	
}


int main(int argc, char **argv)
{
	char *tfd;
	char *tcfd;
	int res;
	int x = 2, i;
	
	tfd = getenv("FD");
	tcfd = getenv("CFD");
	
	if(tfd == NULL && tcfd == NULL)
		exit(0);
	fd = atoi(tfd);
	cfd = atoi(tcfd);

	while(x++ < 65535)
	{
		if(x != fd && x != cfd)
			close(x);
	}


	if(fd > 255)
	{
		for(i = 3; i < 255; i++)
		{
			if(i != cfd) 
			{
				if(dup2(fd, i) < 0)
					exit(1);
				close(fd);
				fd = i;
				break;
			}
		}
	}

	if(cfd > 255)
	{
		for(i = 3; i < 255; i++)
		{
			if(i != fd) 
			{
				if(dup2(cfd, i) < 0)
					exit(1);
				close(cfd);
				cfd = i;
				break;
			}
		}
	}
	
	res = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, res | O_NONBLOCK);

	res = fcntl(cfd, F_GETFL, 0);
	fcntl(cfd, F_SETFL, res | O_NONBLOCK);
	
	adns_init(&dns_state, adns_if_noautosys, 0);
	setup_signals();
	read_io();	
	return 1;
}


