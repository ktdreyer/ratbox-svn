/* src/io.c
 *  Contains code for handling input and output to sockets.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "stdinc.h"
#include "tools.h"
#include "rserv.h"
#include "io.h"
#include "event.h"
#include "conf.h"
#include "tools.h"
#include "client.h"
#include "log.h"
#include "service.h"

struct connection_entry *server_p;
time_t last_connect_time;
time_t current_time;

fd_set readfds;
fd_set writefds;

static void signon_server(struct connection_entry *conn_p);
static void read_server(struct connection_entry *conn_p);
static int write_sendq(struct connection_entry *conn_p);

static int
get_line(struct connection_entry *conn_p, char *buf, int bufsize)
{
	char *p;
	int n;
	int term;
	
	if((n = recv(conn_p->fd, buf, bufsize, MSG_PEEK)) <= 0)
	{
		if(errno == EINTR)
			return 0;

		return -1;
	}

	if((p = memchr(buf, '\n', n)) != NULL)
	{
		n = p - buf + 1;
		term = 1;
	}
	else
		term = 0;

	if((n = read(conn_p->fd, buf, n)) == -1)
		return -1;

	/* we're allowed to parse this line.. */
	if((conn_p->flags & FLAGS_UNTERMINATED) == 0)
	{
		if(n >= BUFSIZE)
			buf[BUFSIZE-1] = '\0';
		else
			buf[n] = '\0';

		if(term)
			conn_p->flags |= FLAGS_UNTERMINATED;

		return n;
	}

	/* found a \n, can begin parsing again.. */
	if(term)
		conn_p->flags &= ~FLAGS_UNTERMINATED;

	return n;
}

void
read_io(void)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	struct timeval read_time_out;
	int select_result;

	while(1)
	{
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);

		read_time_out.tv_sec = 1L;
		read_time_out.tv_usec = 0L;

		if(server_p != NULL)
		{
			/* socket isnt dead, client struct is.. */
			if((server_p->flags & CONN_DEAD) == 0)
			{
				if(server_p->client_p != NULL && 
				   IsDead(server_p->client_p))
					(server_p->io_close)(server_p);
			}

			if(server_p->flags & CONN_DEAD)
			{
				/* connection is dead, uplinks still here. byebye */
				if(server_p->client_p != NULL && !IsDead(server_p->client_p))
				{
					slog("exiting server due to deadness..");
					exit_client(server_p->client_p);
				}

				my_free(server_p->name);
				my_free(server_p);
				server_p = NULL;
			}
		}

		DLINK_FOREACH_SAFE(ptr, next_ptr, exited_list.head)
		{
			free_client(ptr->data);
		}

		exited_list.head = exited_list.tail = NULL;
		exited_list.length = 0;

		if(server_p != NULL)
		{
			if(dlink_list_length(&server_p->sendq) > 0)
				FD_SET(server_p->fd, &writefds);
			FD_SET(server_p->fd, &readfds);
		}

		set_time();
		eventRun();

		select_result = select(FD_SETSIZE, &readfds, &writefds, NULL,
				       &read_time_out);

		if(select_result == 0)
			continue;

		/* have data to parse */
		if(select_result > 0)
		{
			/* data from server to read */
			if(FD_ISSET(server_p->fd, &readfds))
			{
				if(server_p->io_read != NULL)
				{
					server_p->last_time = CURRENT_TIME;
					(server_p->io_read)(server_p);
				}
			}

			if(server_p != NULL && (server_p->flags & CONN_DEAD) == 0)
			{
				if(FD_ISSET(server_p->fd, &writefds))
				{
					if(server_p->io_write != NULL)
						(server_p->io_write)(server_p);
				}
			}
		}
	}
}

void
connect_to_server(void *unused)
{
	struct conf_server *conf_p;
	struct connection_entry *conn_p;
	dlink_node *ptr;
	int serv_fd;

	if(server_p != NULL)
		return;

	if(dlink_list_length(&conf_server_list) <= 0)
		return;

	ptr = conf_server_list.head;
	conf_p = ptr->data;

	serv_fd = sock_open(conf_p->host, conf_p->port, conf_p->vhost);

	if(serv_fd < 0)
		return;

	conn_p = my_malloc(sizeof(struct connection_entry));
	conn_p->name = my_strdup(conf_p->name);
	conn_p->fd = serv_fd;
	conn_p->flags = CONN_CONNECTING;
	conn_p->first_time = conn_p->last_time = CURRENT_TIME;

	conn_p->io_read = signon_server;
	conn_p->io_write = write_sendq;
	conn_p->io_close = sock_close;

	server_p = conn_p;
}

static void
signon_server(struct connection_entry *conn_p)
{
	conn_p->flags &= ~CONN_CONNECTING;
	conn_p->io_read = read_server;

	sendto_server("PASS test TS");
	sendto_server("CAPAB :QS TB");
	sendto_server("SERVER %s 1 :%s", MYNAME, config_file.my_gecos);

	introduce_services();
}

static void
read_server(struct connection_entry *conn_p)
{
	char buf[BUFSIZE*2];
	int n;

	if((n = get_line(server_p, buf, sizeof(buf))) > 0)
		parse_server(buf, n);
	else if(n < 0)
		(conn_p->io_close)(conn_p);
	/* n == 0 we can safely ignore */
}

void
sendto_server(const char *format, ...)
{
	char buf[BUFSIZE];
	va_list args;
	
	if(server_p == NULL)
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf)-3, format, args);
	va_end(args);

	strcat(buf, "\r\n");

	if(sock_write(server_p, buf, strlen(buf)) < 0)
		(server_p->io_close)(server_p);
}

int
sock_open(const char *host, int port, const char *vhost)
{
	struct sockaddr_in raddr;
	struct hostent *host_addr;
	int fd = -1;
	int flags;
	int optval = 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);

	if(fd < 0)
		return -1;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	flags = fcntl(fd, F_GETFL, 0);
	flags |= O_NONBLOCK;

	if(fcntl(fd, F_SETFL, flags) == -1)
		return -1;

	/* no specific vhost, try default */
	if(vhost == NULL)
		vhost = config_file.vhost;

	if(vhost != NULL)
	{
		struct sockaddr_in addr;

		if((host_addr = gethostbyname(vhost)) != NULL)
		{
			memset(&addr, 0, sizeof(struct sockaddr_in));
			memcpy(&addr.sin_addr, host_addr->h_addr, 
			       host_addr->h_length);
			addr.sin_family = AF_INET;
			addr.sin_port = 0;

			if(bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0)
				return -1;
		}
	}

	if((host_addr = gethostbyname(host)) == NULL)
		return -1;

	memset(&raddr, 0, sizeof(struct sockaddr_in));
	memcpy(&raddr.sin_addr, host_addr->h_addr, host_addr->h_length);
	raddr.sin_family = AF_INET;
	raddr.sin_port = htons(port);

	connect(fd, (struct sockaddr *)&raddr, sizeof(struct sockaddr_in));
	return fd;
}

static void
sendq_add(struct connection_entry *conn_p, const char *buf, int len, int offset)
{
	struct send_queue *sendq = my_calloc(1, sizeof(struct send_queue));
	sendq->buf = my_strdup(buf);
	sendq->len = len - offset;
	sendq->pos = offset;
	dlink_add_tail_alloc(sendq, &conn_p->sendq);
}

/* write_sendq()
 *   write()'s as much of a given users sendq as possible
 *
 * inputs	- connection to flush sendq of
 * outputs	- -1 on fatal error, 0 on partial write, otherwise 1
 */
static int
write_sendq(struct connection_entry *conn_p)
{
	struct send_queue *sendq;
	dlink_node *ptr;
	dlink_node *next_ptr;
	int n;

	DLINK_FOREACH_SAFE(ptr, next_ptr, conn_p->sendq.head)
	{
		sendq = ptr->data;

		/* write, starting at the offset */
		if((n = write(conn_p->fd, sendq->buf + sendq->pos, sendq->len)) < 0)
		{
			if(errno != EAGAIN)
				return -1;
			return 0;
		}

		/* wrote full sendq? */
		if(n == sendq->len)
		{
			dlink_destroy(ptr, &conn_p->sendq);
			my_free((void *)sendq->buf);
			my_free(sendq);
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

/* sock_write()
 *   Writes a buffer to a given user, flushing sendq first.
 *
 * inputs	- connection to write to, buffer, length of buffer
 * outputs	- -1 on fatal error, 0 on partial write, otherwise 1
 */
int
sock_write(struct connection_entry *conn_p, const char *buf, int len)
{
	int n;

	if(dlink_list_length(&conn_p->sendq) > 0)
	{
		n = (conn_p->io_write)(conn_p);

		if(n == -1)
		{
			(conn_p->io_close)(conn_p);
			return -1;
		}
		else if(n == 0)
		{
			sendq_add(conn_p, buf, len, n);
			return 0;
		}
	}

	if((n = write(conn_p->fd, buf, len)) < 0)
	{
		if(errno != EAGAIN)
			return -1;
	}

	if(n != len)
		sendq_add(conn_p, buf, len, n);

	return 1;
}

void
sock_close(struct connection_entry *conn_p)
{
	if(conn_p->flags & CONN_DEAD)
		return;

	if(conn_p == server_p)
	{
		eventAddOnce("connect_to_server", connect_to_server, NULL, RECONNECT_DELAY);

		if(server_p->client_p != NULL)
			exit_client(server_p->client_p);
	}

	close(conn_p->fd);
	conn_p->fd = -1;

	conn_p->flags = CONN_DEAD;
}

