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
#include "scommand.h"
#include "ucommand.h"
#include "tools.h"
#include "rserv.h"
#include "io.h"
#include "event.h"
#include "conf.h"
#include "tools.h"
#include "client.h"
#include "log.h"
#include "service.h"
#include "serno.h"

#define IO_HOST	0
#define IO_IP	1

dlink_list connection_list;
struct connection_entry *server_p;

time_t last_connect_time;
time_t current_time;

fd_set readfds;
fd_set writefds;

static int signon_server(struct connection_entry *conn_p);
static int signon_client(struct connection_entry *conn_p);
static void read_server(struct connection_entry *conn_p);
static void read_client(struct connection_entry *conn_p);
static int write_sendq(struct connection_entry *conn_p);
static void parse_server(char *buf, int len);
static void parse_client(struct connection_entry *conn_p, char *buf, int len);

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
get_line(struct connection_entry *conn_p, char *buf, int bufsize)
{
	char *p;
	int n;
	int term;
	
	if((n = recv(conn_p->fd, buf, bufsize, MSG_PEEK)) <= 0)
	{
		if(n == -1 && ignore_errno(errno))
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

	if((n = read(conn_p->fd, buf, n)) <= 0)
	{
		if(n == -1 && ignore_errno(errno))
			return 0;

		return -1;
	}

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

/* read_io()
 *   The main IO loop for reading/writing data.
 *
 * inputs	-
 * outputs	-
 */
void
read_io(void)
{
	struct connection_entry *conn_p;
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
			/* socket isnt dead.. */
			if((server_p->flags & CONN_DEAD) == 0)
			{
				/* client struct is.  im not sure how this
				 * could happen, but..
				 */
				if(server_p->client_p != NULL && 
				   IsDead(server_p->client_p))
				{
					slog_send("Connection to server %s lost: (Server exited)",
						server_p->name);
					(server_p->io_close)(server_p);
				}

				/* connection timed out.. */
				else if((server_p->flags & CONN_CONNECTING) &&
					((server_p->first_time + 30) <= CURRENT_TIME))
				{
					slog_send("Connection to server %s timed out",
						server_p->name);
					(server_p->io_close)(server_p);
				}
			}

			if(server_p->flags & CONN_DEAD)
			{
				/* connection is dead, uplinks still here. byebye */
				if(server_p->client_p != NULL && !IsDead(server_p->client_p))
					exit_client(server_p->client_p);

				my_free(server_p->name);
				my_free(server_p);
				server_p = NULL;
			}
		}

		/* remove any timed out/dead connections */
		DLINK_FOREACH_SAFE(ptr, next_ptr, connection_list.head)
		{
			conn_p = ptr->data;

			/* connection timed out.. */
			if(conn_p->flags & CONN_CONNECTING &&
			   ((conn_p->first_time + 30) <= CURRENT_TIME))
				(conn_p->io_close)(conn_p);

			if(conn_p->flags & CONN_DEAD)
			{
				my_free(conn_p->name);
				my_free(conn_p);
				dlink_delete(ptr, &connection_list);
			}
		}

		/* we can safely exit anything thats dead at this point */
		DLINK_FOREACH_SAFE(ptr, next_ptr, exited_list.head)
		{
			free_client(ptr->data);
		}
		exited_list.head = exited_list.tail = NULL;
		exited_list.length = 0;

		if(server_p != NULL)
		{
			if(server_p->flags & CONN_CONNECTING)
			{
				FD_SET(server_p->fd, &writefds);
			}
			else
			{
				if(dlink_list_length(&server_p->sendq) > 0)
					FD_SET(server_p->fd, &writefds);
				FD_SET(server_p->fd, &readfds);
			}
		}

		DLINK_FOREACH(ptr, connection_list.head)
		{
			conn_p = ptr->data;

			if(conn_p->flags & CONN_CONNECTING)
			{
				FD_SET(conn_p->fd, &writefds);
			}
			else
			{
				if(dlink_list_length(&conn_p->sendq) > 0)
					FD_SET(conn_p->fd, &writefds);
				FD_SET(conn_p->fd, &readfds);
			}
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
			if(server_p != NULL && (server_p->flags & CONN_DEAD) == 0)
			{
				/* data from server to read */
				if(FD_ISSET(server_p->fd, &readfds) &&
				   server_p->io_read != NULL)
				{
					server_p->last_time = CURRENT_TIME;
					(server_p->io_read)(server_p);
				}

				/* couldve died during read.. */
				if((server_p->flags & CONN_DEAD) == 0 &&
				   FD_ISSET(server_p->fd, &writefds) &&
				   server_p->io_write != NULL)
				{
					(server_p->io_write)(server_p);
				}
			}

			DLINK_FOREACH(ptr, connection_list.head)
			{
				conn_p = ptr->data;

				if(conn_p->flags & CONN_DEAD)
					continue;

				if(FD_ISSET(conn_p->fd, &readfds) &&
				   conn_p->io_read != NULL)
				{
					conn_p->last_time = CURRENT_TIME;
					(conn_p->io_read)(conn_p);
				}

				if((conn_p->flags & CONN_DEAD) == 0 &&
				   FD_ISSET(conn_p->fd, &writefds) &&
				   conn_p->io_write != NULL)
				{
					(conn_p->io_write)(conn_p);
				}
			}
		}
	}
}

static struct conf_server *
next_autoconn(void)
{
        struct conf_server *conf_p = NULL;
        struct conf_server *tmp_p;
        dlink_node *ptr;

        if(dlink_list_length(&conf_server_list) <= 0)
                die("No servers to connect to");

        DLINK_FOREACH(ptr, conf_server_list.head)
        {
                tmp_p = ptr->data;

                /* negative port == no autoconn */
                if(tmp_p->defport <= 0)
                        continue;

                if(conf_p == NULL || tmp_p->last_connect < conf_p->last_connect)
                        conf_p = tmp_p;
        }

        if(conf_p != NULL)
        {
                conf_p->port = conf_p->defport;
                conf_p->last_connect = CURRENT_TIME;
        }

        return conf_p;
}

/* connect_to_server()
 *   Connects to given server, or next autoconn.
 *
 * inputs       - optional server to connect to
 * outputs      -
 * requirements - if target_server is specified, it must set the port
 */
void
connect_to_server(void *target_server)
{
	struct conf_server *conf_p;
	struct connection_entry *conn_p;
	int serv_fd;

	if(server_p != NULL)
		return;

        if(target_server == NULL)
        {
                /* no autoconnect? */
                if((conf_p = next_autoconn()) == NULL)
                        die("No server to autoconnect to.");
        }
        else
                conf_p = target_server;

	slog_send("Connection to server %s/%d activated",
                  conf_p->name, conf_p->port);

	serv_fd = sock_open(conf_p->host, conf_p->port, conf_p->vhost, IO_HOST);

	if(serv_fd < 0)
		return;

	conn_p = my_malloc(sizeof(struct connection_entry));
	conn_p->name = my_strdup(conf_p->name);
	conn_p->fd = serv_fd;
	conn_p->flags = CONN_CONNECTING;
	conn_p->first_time = conn_p->last_time = CURRENT_TIME;
        conn_p->pass = my_strdup(conf_p->pass);

	conn_p->io_read = NULL;
	conn_p->io_write = signon_server;
	conn_p->io_close = sock_close;

	server_p = conn_p;
}

void
connect_to_client(struct client *client_p, const char *host, int port)
{
	struct connection_entry *conn_p;
	int client_fd;

	client_fd = sock_open(host, port, config_file.vhost, IO_IP);

	if(client_fd < 0)
		return;

	conn_p = my_malloc(sizeof(struct connection_entry));
	conn_p->name = my_strdup(client_p->name);
        conn_p->username = my_strdup(client_p->user->username);
        conn_p->host = my_strdup(client_p->user->host);

	conn_p->fd = client_fd;
	conn_p->flags = CONN_CONNECTING;
	conn_p->first_time = conn_p->last_time = CURRENT_TIME;

	conn_p->io_read = NULL;
	conn_p->io_write = signon_client;
	conn_p->io_close = sock_close;

	dlink_add_alloc(conn_p, &connection_list);
}

static int
signon_server(struct connection_entry *conn_p)
{
	conn_p->flags &= ~CONN_CONNECTING;
	conn_p->io_read = read_server;
	conn_p->io_write = write_sendq;

	/* ok, if connect() failed, this will cause an error.. */
	sendto_server("PASS %s TS", conn_p->pass);

	/* ..so we need to return. */
	if(conn_p->flags & CONN_DEAD)
		return -1;

	slog_send("Connection to server %s completed", conn_p->name);

	sendto_server("CAPAB :QS TB");
	sendto_server("SERVER %s 1 :%s", MYNAME, config_file.my_gecos);

	introduce_services();
	return 1;
}

static int
signon_client(struct connection_entry *conn_p)
{
	conn_p->flags &= ~CONN_CONNECTING;
	conn_p->io_read = read_client;
	conn_p->io_write = write_sendq;

	/* ok, if connect() failed, this will cause an error.. */
	sendto_connection(conn_p, "Welcome to %s, version ratbox-services-%s",
			  MYNAME, RSERV_VERSION);

	if(conn_p->flags & CONN_DEAD)
		return -1;

        sendto_connection(conn_p, "Please login via .login <username> <password>");

	return 1;
}

static void
read_server(struct connection_entry *conn_p)
{
	char buf[BUFSIZE*2];
	int n;

	if((n = get_line(server_p, buf, sizeof(buf))) > 0)
		parse_server(buf, n);
	else if(n < 0)
	{
		if(conn_p == server_p)
		{
			if(ignore_errno(errno))
				slog_send("Connection to server %s lost",
					conn_p->name);
			else
				slog_send("Connection to server %s lost: (Read error: %s)",
					conn_p->name, strerror(errno));
		}

		(conn_p->io_close)(conn_p);
	}
	/* n == 0 we can safely ignore */
}

static void
read_client(struct connection_entry *conn_p)
{
	char buf[BUFSIZE*2];
	int n;

	if((n = get_line(conn_p, buf, sizeof(buf))) > 0)
		parse_client(conn_p, buf, n);
	else if(n < 0)
		(conn_p->io_close)(conn_p);
	/* n == 0 we can safely ignore */
}

/* string_to_array()
 *   Changes a given buffer into an array of parameters.
 *   Taken from ircd-ratbox.
 *
 * inputs	- string to parse, array to put in
 * outputs	- number of parameters
 */
static inline int
string_to_array(char *string, char *parv[MAXPARA])
{
	char *p, *buf = string;
	int x = 1;

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

/* parse_server()
 *   parses a given buffer into a command and calls handler
 *
 * inputs	- buffer to parse, length of buffer
 * outputs	-
 */
void
parse_server(char *buf, int len)
{
	static char *parv[MAXPARA + 1];
	const char *command;
	char *s;
	char *ch;
	int parc;

	if(len > BUFSIZE)
		buf[BUFSIZE-1] = '\0';

	if((s = strchr(buf, '\n')) != NULL)
		*s = '\0';

	if((s = strchr(buf, '\r')) != NULL)
		*s = '\0';

	/* skip leading spaces.. */
	for(ch = buf; *ch == ' '; ch++)
		;

	parv[0] = server_p->name;

	if(*ch == ':')
	{
		ch++;

		parv[0] = ch;

		if((s = strchr(ch, ' ')) != NULL)
		{
			*s++ = '\0';
			ch = s;
		}

		while(*ch == ' ')
			ch++;
	}

	if(EmptyString(ch))
		return;

	command = ch;

	if((s = strchr(ch, ' ')) != NULL)
	{
		*s++ = '\0';
		ch = s;

        	while(*ch == ' ')
	        	ch++;

	}
        else
                ch = NULL;

	parc = string_to_array(ch, parv);

	handle_scommand(command, parv, parc);
}

void
parse_client(struct connection_entry *conn_p, char *buf, int len)
{
	static char *parv[MAXPARA + 1];
	const char *command;
	char *s;
	char *ch;
	int parc;

	if(len > BUFSIZE)
		buf[BUFSIZE-1] = '\0';

	if((s = strchr(buf, '\n')) != NULL)
		*s = '\0';

	if((s = strchr(buf, '\r')) != NULL)
		*s = '\0';

	/* skip leading spaces.. */
	for(ch = buf; *ch == ' '; ch++)
		;

	parv[0] = conn_p->name;

	if(*ch != '.')
                return;

        ch++;

	if(EmptyString(ch))
		return;

	command = ch;

        /* command with params? */
	if((s = strchr(ch, ' ')) != NULL)
	{
		*s++ = '\0';
		ch = s;

        	while(*ch == ' ')
	        	ch++;

	}
        else
                ch = NULL;

	parc = string_to_array(ch, parv);

	handle_ucommand(conn_p, command, parv, parc);
}

/* sendto_server()
 *   attempts to send the given data to our server
 *
 * inputs	- string to send
 * outputs	-
 */
void
sendto_server(const char *format, ...)
{
	char buf[BUFSIZE];
	va_list args;
	
	if(server_p == NULL || server_p->flags & CONN_DEAD)
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf)-3, format, args);
	va_end(args);

	strcat(buf, "\r\n");

	if(sock_write(server_p, buf, strlen(buf)) < 0)
	{
		slog_send("Connection to server %s lost: (Write error: %s)",
			server_p->name, strerror(errno));
		(server_p->io_close)(server_p);
	}
}

void
sendto_connection(struct connection_entry *conn_p, const char *format, ...)
{
	char buf[BUFSIZE];
	va_list args;

	if(conn_p == NULL || conn_p->flags & CONN_DEAD)
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf)-3, format, args);
	va_end(args);

	strcat(buf, "\r\n");

	if(sock_write(conn_p, buf, strlen(buf)) < 0)
		(conn_p->io_close)(conn_p);
}

void
sendto_connections(const char *format, ...)
{
        struct connection_entry *conn_p;
        char buf[BUFSIZE];
        dlink_node *ptr;
        va_list args;

        va_start(args, format);
        vsnprintf(buf, sizeof(buf)-3, format, args);
        va_end(args);

        DLINK_FOREACH(ptr, connection_list.head)
        {
                conn_p = ptr->data;

                if(conn_p->oper == NULL)
                        continue;

                sendto_connection(conn_p, "%s", buf);
        }
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
			if(n == -1 && ignore_errno(errno))
				return 0;

			return -1;
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

/* sendq_add()
 *   adds a given buffer to a connections sendq
 *
 * inputs	- connection to add to, buffer to add, length of buffer,
 *		  offset at where to start writing
 * outputs	-
 */
static void
sendq_add(struct connection_entry *conn_p, const char *buf, int len, int offset)
{
	struct send_queue *sendq = my_calloc(1, sizeof(struct send_queue));
	sendq->buf = my_strdup(buf);
	sendq->len = len - offset;
	sendq->pos = offset;
	dlink_add_tail_alloc(sendq, &conn_p->sendq);
}

/* sock_open()
 *   attempts to open a connection
 *
 * inputs	- host/port to connect to, vhost to use
 * outputs	- fd of socket, -1 on error
 */
int
sock_open(const char *host, int port, const char *vhost, int type)
{
	struct sockaddr_in raddr;
	struct hostent *host_addr;
	int fd = -1;
	int flags;
	int optval = 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);

	if(fd < 0)
	{
		slog_send("Connection to %s/%d failed: (socket(): %s)",
			host, port, strerror(errno));
		return -1;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	flags = fcntl(fd, F_GETFL, 0);
	flags |= O_NONBLOCK;

	if(fcntl(fd, F_SETFL, flags) == -1)
	{
		slog_send("Connection to %s/%d failed: (fcntl(): %s)",
			host, port, strerror(errno));
		return -1;
	}

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
			{
				slog_send("Connection to %s/%d failed: (unable to bind to %s: %s)",
					host, port, vhost, strerror(errno));
				return -1;
			}
		}
	}

	memset(&raddr, 0, sizeof(struct sockaddr_in));
	raddr.sin_family = AF_INET;
	raddr.sin_port = htons(port);

	if(type == IO_HOST)
	{
		if((host_addr = gethostbyname(host)) == NULL)
		{
			slog_send("Connection to %s/%d failed: (unable to resolve: %s)",
				host, port, host);
			return -1;
		}

		memcpy(&raddr.sin_addr, host_addr->h_addr, host_addr->h_length);
	}
	else
	{
		unsigned long hl = strtoul(host, NULL, 10);
		raddr.sin_addr.s_addr = htonl(hl);
	}

	connect(fd, (struct sockaddr *)&raddr, sizeof(struct sockaddr_in));
	return fd;
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

		/* got a partial write, add the new line to the sendq */
		if(n == 0)
		{
			sendq_add(conn_p, buf, len, 0);
			return 0;
		}
		else if(n == -1)
			return -1;
	}

	if((n = write(conn_p->fd, buf, len)) < 0)
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

