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
int cfd; /* control fd is blocking from our end */

#define MAXPARA 10
#define REQIDLEN 10

#define REQREV 0
#define REQFWD 1

#define REVIPV4 0
#define REVIPV6 1
#define REVIPV6INT 2
#define FWDHOST 3


#define DLINK_FOREACH(pos, head) for (pos = (head); pos != NULL; pos = pos->next)
#define DLINK_FOREACH_SAFE(pos, n, head) for (pos = (head), n = pos ? pos->next : NULL; pos != NULL; pos = n, n = pos ? pos->next : NULL) 
#define dlink_list_length(list) (list)->length
#define dlink_add_alloc(data, list) dlink_add(data, MyMalloc(sizeof(dlink_node)), list)
#define dlink_add_tail_alloc(data, list) dlink_add_tail(data, MyMalloc(sizeof(dlink_node)), list)
#define dlink_destroy(node, list) do { dlink_delete(node, list); MyFree(node); } while(0)
#define EmptyString(x) (!(x) || (*(x) == '\0'))

typedef struct _dlink_node dlink_node;
typedef struct _dlink_list dlink_list;

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

struct _dlink_node
{
        void *data;
        dlink_node *prev;
        dlink_node *next;

};

struct _dlink_list
{
        dlink_node *head;
        dlink_node *tail;
        unsigned long length;
};

fd_set readfds;
fd_set writefds;
fd_set exceptfds;

dlink_list sendq_list;
adns_state dns_state;
int fd = -1;

struct send_queue
{
        const char *buf;
        int len;
        int pos;
};
                        

static int write_sendq(void);
static int sock_write(const char *buf, int len);

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

static char *MyStrdup(const char *str)
{
	char *ptr;
	ptr = malloc(strlen(str)+1);
	if(ptr == NULL)
	{
		report_error("MyStrdup failed, giving up");
	}
	strcpy(ptr, str);
	return(ptr);
}

static void
dlink_add_tail(void *data, dlink_node * m, dlink_list * list)
{
        m->data = data;
        m->next = NULL;
        m->prev = list->tail;

        /* Assumption: If list->tail != NULL, list->head != NULL */
        if(list->tail != NULL)
                list->tail->next = m;
        else if(list->head == NULL)  
                list->head = m;

        list->tail = m;
        list->length++;
}

static void
dlink_delete(dlink_node * m, dlink_list * list)
{
        /* Assumption: If m->next == NULL, then list->tail == m
         *      and:   If m->prev == NULL, then list->head == m
         */
        if(m->next)
                m->next->prev = m->prev;
        else
                list->tail = m->prev;

        if(m->prev)
                m->prev->next = m->next;
        else
                list->head = m->next;

        /* Set this to NULL does matter */
        m->next = m->prev = NULL;
        list->length--;
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
                n = p - buf + 1;
        else 
                return 0;
	
	return n;
}

static void send_answer(struct dns_request *req, adns_answer *reply)
{
	char buf[512];
	char response[64];
	char rtype[4];
	int result;
	if(reply && reply->status == adns_s_ok)
	{
		switch(req->revfwd)
		{
			case REQREV:
			{
				strcpy(rtype, "REV");
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
				strcpy(rtype, "FWD");
				switch(reply->type)
				{
#ifdef IPV6
					case adns_r_addr6:
					{
						char tmpres[64];
						inet_ntop(AF_INET6, reply->rrs.addr->addr.inet6.sin6_addr, tmpres, sizeof(struct in6_addr));
						if(*tmpres == ':')
						{
							strcpy(response, "0");
							strcat(response, tmpres);
						} else
							strcpy(response, tmpres);
						break;
					}
#endif
					case adns_r_addr:
					{
						strcpy(response, inet_ntoa(reply->rrs.addr->addr.inet.sin_addr));
						break;
					} 
					default:
					{
						strcpy(response, "FAILED");
						result = 0;
					}						
				}
			}
			default:
			{
				snprintf(buf, sizeof(buf), "I have an request type of %d, and I don't know what to do!", req->reqtype);
				report_error(buf);				
				break;				
			}				
		}

	} 
	else
	{
#ifdef IPV6
		if(req->revfwd == REQREV && req->reqtype == 5 && req->fallback == 0)
		{
			req->fallback = 1;
		        result = adns_submit_reverse(dns_state,
                                    (struct sockaddr *) req->sins.in6,
                                    flags,
                                    adns_qf_owner | adns_qf_cname_loose |
                                    adns_qf_quoteok_anshost, req, &req->query);
                                    
			if(result == -1)
			{
				snprintf(buf, sizeof(buf), "REV %s 0 FAILED\n", req->reqid);
				sock_write(buf, strlen(buf));	
				MyFree(reply);
				MyFree(req);

			}							
			return;
		}
#endif
		switch(req->revfwd)
		{
			case REQREV:
				strcpy(rtype, "REV");
				break;
			case REQFWD:
				strcpy(rtype, "FWD");
				break;
			default:
				snprintf(buf, sizeof(buf), "I have an request type of %d, and I don't know what to do!", req->reqtype);
				report_error(buf);
		}
		strcpy(response, "FAILED");
		result = 0;
	}
	snprintf(buf, sizeof(buf), "%s %s %d %s\n", rtype, req->reqid, result, response);
	sock_write(buf, strlen(buf));	
	MyFree(reply);
	MyFree(req);
}


static void process_adns_incoming(void)
{
	adns_query q, r;
	adns_answer *answer;
	struct dns_request *req;
	
	adns_forallqueries_begin(dns_state);
	while(   (q = adns_forallqueries_next(dns_state, (void *)&r)) != NULL)
	{
		switch(adns_check(dns_state, &q, &answer, (void **)&req))
		{
			case EAGAIN:
				break;
			case 0:
				send_answer(req, answer);
				break;
			default:
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
//	dlink_node *ptr;
//	dlink_node *next_ptr;
	struct timeval *tv, tvbuf, tvx;
	int select_result;
	int maxfd = -1;
	while(1)
	{
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);

		FD_SET(fd, &readfds);
		if(sendq_list.length > 0)
			FD_SET(fd, &writefds);
		tv = &tvx;
		maxfd = fd;

		adns_beforeselect(dns_state, &maxfd, &readfds, &writefds, &exceptfds, &tv, &tvbuf, 0);

		tvx.tv_sec = 1L;
		tvx.tv_usec = 0L;

		select_result = select(FD_SETSIZE, &readfds, &writefds, &exceptfds,
				tv);

		if(select_result == 0)
			continue;

		/* have data to parse */
		if(select_result > 0)
		{
			adns_afterselect(dns_state, maxfd, &readfds,&writefds,&exceptfds, 0);
			
			process_adns_incoming();
			
			if(FD_ISSET(fd, &readfds))
			{
				process_request();
				/* incoming requests */
			}
			if(FD_ISSET(fd, &writefds))
			{
				/* process outgoing */
				write_sendq();
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


/* get_sendq()
 *   gets the sendq of a given connection
 *
 * inputs       - connection entry to get sendq for
 * outputs      - sendq
 */
unsigned long
get_sendq(void)
{
        struct send_queue *sendq_ptr;
        dlink_node *ptr;
        unsigned long sendq = 0;

        DLINK_FOREACH(ptr, sendq_list.head)
        {
                sendq_ptr = ptr->data;

                sendq += sendq_ptr->len;
        }

        return sendq;
}

/* write_sendq()
 *   write()'s as much of a given users sendq as possible
 *
 * inputs	- connection to flush sendq of
 * outputs	- -1 on fatal error, 0 on partial write, otherwise 1
 */
static int
write_sendq(void)
{
	struct send_queue *sendq;
	dlink_node *ptr;
	dlink_node *next_ptr;
	int n;

	DLINK_FOREACH_SAFE(ptr, next_ptr, sendq_list.head)
	{
		sendq = ptr->data;

		/* write, starting at the offset */
		if((n = write(fd, sendq->buf + sendq->pos, sendq->len)) < 0)
		{
			if(n == -1 && ignore_errno(errno))
				return 0;

			return -1;
		}

		/* wrote full sendq? */
		if(n == sendq->len)
		{
			dlink_destroy(ptr, &sendq_list);
			MyFree((void *)sendq->buf);
			MyFree(sendq);
		}
		else
		{
			sendq->pos += n;
			sendq->len -= n;
			return 0;
		}
	}

	return 1;
}

/* sendq_add()
 *   adds a given buffer to a connections sendq
 *
 * inputs	- connection to add to, buffer to add, length of buffer,
 *		  offset at where to start writing
 * outputs	-
 */
static void
sendq_add(const char *buf, int len, int offset)
{
	struct send_queue *sendq;
	sendq = MyMalloc(sizeof(struct send_queue));
	sendq->buf = MyStrdup(buf);
	sendq->len = len - offset;
	sendq->pos = offset;
	dlink_add_tail_alloc(sendq, &sendq_list);
}


/* sock_write()
 *   Writes a buffer to a given user, flushing sendq first.
 *
 * inputs	- connection to write to, buffer, length of buffer
 * outputs	- -1 on fatal error, 0 on partial write, otherwise 1
 */
static int
sock_write(const char *buf, int len)
{
	int n;

	if(dlink_list_length(&sendq_list) > 0)
	{
		n = write_sendq();

		/* got a partial write, add the new line to the sendq */
		if(n == 0)
		{
			sendq_add(buf, len, 0);
			return 0;
		}
		else if(n == -1)
			return -1;
	}

	if((n = write(fd, buf, len)) < 0)
	{
		if(!ignore_errno(errno))
			return -1;

		/* write wouldve blocked so wasnt done, we didnt write
		 * anything so reset n to zero and carry on.
		 */
		n = 0;
	}

	/* partial write.. add this line to sendq with offset of however
	 * much we wrote
	 */
	if(n != len)
		sendq_add(buf, len, n);

	return 1;
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
		if(!ignore_errno(errno))
			report_error("Failed reading line");
		else
			return;
	}
	parc = io_to_array(buf, parv);
#if 0
	if(parc != 3)
	{
		report_error("Server didn't send me the right amount of arguments, I quit");
	}
#endif	
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
			req->fallback = 0;
			break;
		case '6':
			req->reqtype = REVIPV6;
			flags = adns_r_ptr_ip6;
			inet_pton(AF_INET6, rec, &req->sins.in6.sin6_addr);
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
		
	if(result == -1)
	{
		send_answer(req, NULL);		
	}
	
}


int main(int argc, char **argv)
{
	char *tfd;
	char *tcfd;
	int res;
	tfd = getenv("FD");
	tcfd = getenv("CFD");

	if(tfd == NULL || tcfd == NULL)
		exit(0);
	fd = atoi(tfd);
	cfd = atoi(tcfd);
	res = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, res | O_NONBLOCK);

	res = fcntl(cfd, F_GETFL, 0);
	fcntl(cfd, F_SETFL, res | O_NONBLOCK);

	adns_init(&dns_state, adns_if_noautosys, 0);
	setup_signals();
	read_io();	
	return 1;
}


