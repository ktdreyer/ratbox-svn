/* src/io.c
 *   Contains code for handling input and output to sockets.
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
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
static void signon_client_in(struct connection_entry *conn_p);
static int signon_client_out(struct connection_entry *conn_p);

static void signoff_server(struct connection_entry *conn_p);
static void signoff_client(struct connection_entry *conn_p);

static void read_server(struct connection_entry *conn_p);
static void read_client(struct connection_entry *conn_p);
static int write_sendq(struct connection_entry *conn_p);
static void parse_server(char *buf, int len);
static void parse_client(struct connection_entry *conn_p, char *buf, int len);
#ifdef HAVE_GETADDRINFO
static struct addrinfo *gethostinfo(char const *host, int port);
#endif

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
		if(!ConnDead(server_p))
		{
			/* client struct is.  im not sure how this
			 * could happen, but..
			 */
			if(server_p->client_p != NULL && 
				IsDead(server_p->client_p))
			{
				slog("Connection to server %s lost: "
					"(Server exited)",
					server_p->name);
				sendto_all(UMODE_SERVER,
					"Connection to server %s lost: "
					"(Server exited)",
					server_p->name);
				(server_p->io_close)(server_p);
			}

			/* connection timed out.. */
			else if(ConnConnecting(server_p) &&
					((server_p->first_time + 30) <= CURRENT_TIME))
			{
				slog("Connection to server %s timed out",
					server_p->name);
				sendto_all(UMODE_SERVER,
					"Connection to server %s timed out",
					server_p->name);
				(server_p->io_close)(server_p);
			}

			/* authentication timed out.. */
			else if(ConnHandshake(server_p) &&
					((server_p->first_time + 60) <=
					 CURRENT_TIME))
			{
				slog("Connection to server %s timed out",
					server_p->name);
				sendto_all(UMODE_SERVER,
					"Connection to server %s timed out",
					server_p->name);
				(server_p->io_close)(server_p);
			}

			/* pinged out */
			else if(ConnSentPing(server_p) &&
				((server_p->last_time + config_file.ping_time*2)
				<= CURRENT_TIME))
			{
				slog("Connection to server %s "
					"lost: (Ping timeout)",
					server_p->name);
				sendto_all(UMODE_SERVER,
					"Connection to server %s "
					"lost: (Ping timeout)",
					server_p->name);
				(server_p->io_close)(server_p);
			}

			/* no data for a while.. send ping */
			else if(!ConnSentPing(server_p) &&
				((server_p->last_time + config_file.ping_time) 
				<= CURRENT_TIME))
			{
				sendto_server("PING :%s", MYNAME);
				SetConnSentPing(server_p);
			}
		}

		if(ConnDead(server_p))
		{
			/* connection is dead, uplinks still here. byebye */
			if(server_p->client_p != NULL && 
			   !IsDead(server_p->client_p))
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
		if(ConnConnecting(conn_p) &&
		   ((conn_p->first_time + 30) <= CURRENT_TIME))
			(conn_p->io_close)(conn_p);

		if(ConnDead(conn_p))
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
		if(ConnConnecting(server_p))
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

		if(ConnConnecting(conn_p))
		{
			if(ConnDccIn(conn_p))
				FD_SET(conn_p->fd, &readfds);
			else
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
		if(server_p != NULL && !ConnDead(server_p))
		{
			/* data from server to read */
			if(FD_ISSET(server_p->fd, &readfds) &&
					server_p->io_read != NULL)
			{
				server_p->last_time = CURRENT_TIME;
				ClearConnSentPing(server_p);
				(server_p->io_read)(server_p);
			}

			/* couldve died during read.. */
			if(!ConnDead(server_p) &&
			   FD_ISSET(server_p->fd, &writefds) &&
			   server_p->io_write != NULL)
			{
				(server_p->io_write)(server_p);
			}
		}

		DLINK_FOREACH(ptr, connection_list.head)
		{
			conn_p = ptr->data;

			if(ConnDead(conn_p))
				continue;

			if(FD_ISSET(conn_p->fd, &readfds) &&
					conn_p->io_read != NULL)
			{
				conn_p->last_time = CURRENT_TIME;
				(conn_p->io_read)(conn_p);
			}

			if(!ConnDead(conn_p) && 
			   FD_ISSET(conn_p->fd, &writefds) &&
			   conn_p->io_write != NULL)
			{
				(conn_p->io_write)(conn_p);
			}
		}
	}
	}
}

/* next_autoconn()
 *   finds the next entry to autoconnect to
 *
 * inputs       -
 * outputs      - struct conf_server to connect to, or NULL
 */
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
                if(!ConfServerAutoconn(tmp_p))
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

        /* no specific server, so try autoconn */
        if(target_server == NULL)
        {
                /* no autoconnect? */
                if((conf_p = next_autoconn()) == NULL)
                        die("No server to autoconnect to.");
        }
        else
                conf_p = target_server;

	slog("Connection to server %s/%d activated", 
             conf_p->name, conf_p->port);
	sendto_all(UMODE_SERVER, "Connection to server %s/%d activated",
                   conf_p->name, conf_p->port);

	serv_fd = sock_open(conf_p->host, conf_p->port, conf_p->vhost, IO_HOST);

	if(serv_fd < 0)
		return;

	conn_p = my_malloc(sizeof(struct connection_entry));
	conn_p->name = my_strdup(conf_p->name);
	conn_p->fd = serv_fd;
	conn_p->first_time = conn_p->last_time = CURRENT_TIME;
        conn_p->pass = my_strdup(conf_p->pass);

	conn_p->io_read = NULL;
	conn_p->io_write = signon_server;
	conn_p->io_close = signoff_server;

	SetConnConnecting(conn_p);

	server_p = conn_p;
}

/* connect_to_client()
 *   connects to a client
 *
 * inputs       - client requesting dcc, host/port to connect to
 * outputs      -
 */
void
connect_to_client(struct client *client_p, struct conf_oper *oper_p,
			const char *host, int port)
{
	struct connection_entry *conn_p;
	int client_fd;

	client_fd = sock_open(host, port, config_file.dcc_vhost, IO_IP);

	if(client_fd < 0)
		return;

	conn_p = my_malloc(sizeof(struct connection_entry));
	conn_p->name = my_strdup(client_p->name);

	conn_p->oper = oper_p;
	oper_p->refcount++;

	conn_p->fd = client_fd;
	conn_p->first_time = conn_p->last_time = CURRENT_TIME;

	conn_p->io_read = NULL;
	conn_p->io_write = signon_client_out;
	conn_p->io_close = signoff_client;

	SetConnConnecting(conn_p);
	SetConnDccOut(conn_p);

	dlink_add_alloc(conn_p, &connection_list);
}

void
connect_from_client(struct client *client_p, struct conf_oper *oper_p,
			const char *servicenick)
{
	struct connection_entry *conn_p;
	struct sockaddr_in addr;
	struct hostent *local_addr;
	unsigned long local_ip;
	int client_fd;
	int port;
	int res;

	client_fd = sock_create(AF_INET);

	if(config_file.dcc_vhost == NULL ||
	   (local_addr = gethostbyname(config_file.dcc_vhost)) == NULL)
		return;

	/* XXX ERROR */
	if(client_fd < 0)
		return;

	for(port = config_file.dcc_low_port; port < config_file.dcc_high_port; 
	    port++)
	{
		memset(&addr, 0, sizeof(struct sockaddr_in));
		memcpy(&addr.sin_addr, local_addr->h_addr,
			local_addr->h_length);
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);

		res = bind(client_fd, (struct sockaddr *) &addr,
			sizeof(struct sockaddr_in));

		if(res >= 0)
			break;
	}

	if(res < 0)
	{
		close(client_fd);
		return;
	}

	if(listen(client_fd, 1) < 0)
	{
		close(client_fd);
		return;
	}

	conn_p = my_malloc(sizeof(struct connection_entry));
	conn_p->name = my_strdup(client_p->name);
        conn_p->oper = oper_p;

	conn_p->fd = client_fd;
	conn_p->first_time = conn_p->last_time = CURRENT_TIME;

	conn_p->io_read = signon_client_in;
	conn_p->io_close = signoff_client;

	SetConnConnecting(conn_p);
	SetConnDccIn(conn_p);

	dlink_add_alloc(conn_p, &connection_list);

	memcpy(&local_ip, local_addr->h_addr, local_addr->h_length);
	local_ip = htonl(local_ip);

	sendto_server(":%s PRIVMSG %s :\001DCC CHAT chat %lu %d\001",
		      servicenick, client_p->name, local_ip, port);
}

/* signon_server()
 *   sends our connection information to a server
 *
 * inputs       - connection entry to send to
 * outputs      - 1 on success, -1 on failure
 */
static int
signon_server(struct connection_entry *conn_p)
{
        ClearConnConnecting(conn_p);
        SetConnHandshake(conn_p);

	conn_p->io_read = read_server;
	conn_p->io_write = write_sendq;

	/* ok, if connect() failed, this will cause an error.. */
	sendto_server("PASS %s TS", conn_p->pass);

	/* ..so we need to return. */
	if(ConnDead(conn_p))
		return -1;

	slog("Connection to server %s established", conn_p->name);
	sendto_all(UMODE_SERVER, "Connection to server %s established",
                   conn_p->name);

	sendto_server("CAPAB :QS TB");
	sendto_server("SERVER %s 1 :%s", MYNAME, config_file.gecos);

	introduce_services();

        sendto_server("PING :%s", MYNAME);

	return 1;
}

static void
signon_client_in(struct connection_entry *conn_p)
{
	struct sockaddr_in addr;
	int sock;
	int addrlen = sizeof(struct sockaddr);

	memset(&addr, 0, sizeof(struct sockaddr_in));

	if((sock = accept(conn_p->fd, (struct sockaddr *) &addr,
			  (socklen_t *) &addrlen)) < 0)
	{
		if(ignore_errno(errno))
			return;

		/* XXX FAILED NOTICE */
		shutdown(conn_p->fd, SHUT_RDWR);

		(conn_p->io_close)(conn_p);
		return;
		
	}

	ClearConnConnecting(conn_p);
	conn_p->io_read = read_client;
	conn_p->io_write = write_sendq;

	shutdown(conn_p->fd, SHUT_RDWR);
	close(conn_p->fd);

	conn_p->fd = sock;

	sendto_one(conn_p, "Welcome to %s, version ratbox-services-%s",
		   MYNAME, RSERV_VERSION);

	if(ConnDead(conn_p))
		return;

        sendto_one(conn_p, "Please login via .login <username> <password>");

	return;

}

/* signon_client_out()
 *   sends the initial connection info to a new client of ours
 *
 * inputs       - connection entry to send to
 * outputs      - 1 on success, -1 on failure
 */
static int
signon_client_out(struct connection_entry *conn_p)
{
	ClearConnConnecting(conn_p);
	conn_p->io_read = read_client;
	conn_p->io_write = write_sendq;

	/* ok, if connect() failed, this will cause an error.. */
	sendto_one(conn_p, "Welcome to %s, version ratbox-services-%s",
		   MYNAME, RSERV_VERSION);

	if(ConnDead(conn_p))
		return -1;

        sendto_one(conn_p, "Please login via .login <username> <password>");

	return 1;
}

static void
signoff_client(struct connection_entry *conn_p)
{
	if(ConnDead(conn_p))
		return;

	if(UserAuth(conn_p))
                sendto_all(UMODE_AUTH, "%s has disconnected", conn_p->name);

	if(conn_p->oper != NULL)
		deallocate_conf_oper(conn_p->oper);

	sock_close(conn_p);
	SetConnDead(conn_p);
}

static void
signoff_server(struct connection_entry *conn_p)
{
	if(ConnDead(conn_p))
		return;

	if(conn_p == server_p)
	{
		eventAddOnce("connect_to_server", connect_to_server, NULL, 
				config_file.reconnect_time);

		if(server_p->client_p != NULL)
			exit_client(server_p->client_p);
	}

	sock_close(conn_p);
	SetConnDead(conn_p);
}


/* read_server()
 *   reads some data from the server, exiting it on read error
 *
 * inputs       - connection entry to read from [unused]
 * outputs      -
 */
static void
read_server(struct connection_entry *conn_p)
{
	char buf[BUFSIZE*2];
	int n;

	if((n = get_line(server_p, buf, sizeof(buf))) > 0)
		parse_server(buf, n);

        /* we had a fatal error.. close the socket */
	else if(n < 0)
	{
                if(ignore_errno(errno))
                {
                        slog("Connection to server %s lost", conn_p->name);
                        sendto_all(UMODE_SERVER,
                                   "Connection to server %s lost", 
                                   conn_p->name);
                }
                else
                {
                        slog("Connection to server %s lost: (Read error: %s)",
                             conn_p->name, strerror(errno));
                        sendto_all(UMODE_SERVER,
                                   "Connection to server %s lost: "
                                   "(Read error: %s)",
                                   conn_p->name, strerror(errno));
                }

		(conn_p->io_close)(conn_p);
	}
	/* n == 0 we can safely ignore */
}

/* read_client()
 *   reads some data from a client, exiting it on read errors
 *
 * inputs       - connection entry to read from
 * outputs      -
 */
static void
read_client(struct connection_entry *conn_p)
{
	char buf[BUFSIZE*2];
	int n;

	if((n = get_line(conn_p, buf, sizeof(buf))) > 0)
		parse_client(conn_p, buf, n);

        /* fatal error */
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

	handle_scommand(command, (const char **) parv, parc);
}

/* parse_client()
 *   parses a given buffer and calls command handlers
 *
 * inputs       - connection who sent data, buffer to parse, length of buffer
 * outputs      -
 */
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

        /* partyline */
	if(*ch != '.')
        {
                if(!UserAuth(conn_p))
                {
                        sendto_one(conn_p, "You must .login first");
                        return;
                }

                if(!IsUmodeChat(conn_p))
                {
                        sendto_one(conn_p, "You must '.flags +chat' first.");
                        return;
                }

                sendto_all_butone(conn_p, UMODE_CHAT, "<%s> %s", 
                                  conn_p->name, ch);
                return;
        }

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

        /* pass it off to the handler */
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
	
	if(server_p == NULL || ConnDead(server_p))
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf)-3, format, args);
	va_end(args);

	strcat(buf, "\r\n");

	if(sock_write(server_p, buf, strlen(buf)) < 0)
	{
		slog("Connection to server %s lost: (Write error: %s)",
		     server_p->name, strerror(errno));
		sendto_all(UMODE_SERVER,
                           "Connection to server %s lost: (Write error: %s)",
			   server_p->name, strerror(errno));
		(server_p->io_close)(server_p);
	}
}

/* sendto_one()
 *   attempts to send the given data to a given connection
 *
 * inputs       - connection to send to, data to send
 * outputs      -
 */
void
sendto_one(struct connection_entry *conn_p, const char *format, ...)
{
	char buf[BUFSIZE];
	va_list args;

	if(conn_p == NULL || ConnDead(conn_p))
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf)-3, format, args);
	va_end(args);

	strcat(buf, "\r\n");

	if(sock_write(conn_p, buf, strlen(buf)) < 0)
		(conn_p->io_close)(conn_p);
}

/* sendto_all()
 *   attempts to send the given data to all clients connected
 *
 * inputs       - umode required [0 for none], data to send
 * outputs      -
 */
void
sendto_all(int umode, const char *format, ...)
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

                if(!UserAuth(conn_p))
                        continue;

                if(umode && !(conn_p->flags & umode))
                        continue;

                sendto_one(conn_p, "%s", buf);
        }
}

/* sendto_all_butone()
 *   attempts to send the given data to all clients connected but one
 *
 * inputs       - client not to send to, umode required [0 for none], data
 * outputs      -
 */
void
sendto_all_butone(struct connection_entry *one, int umode,
                  const char *format, ...)
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

                if(!UserAuth(conn_p))
                        continue;

                /* the one we shouldnt be sending to.. */
                if(conn_p == one)
                        continue;

                if(umode && !(conn_p->flags & umode))
                        continue;

                sendto_one(conn_p, "%s", buf);
        }
}

/* get_sendq()
 *   gets the sendq of a given connection
 *
 * inputs       - connection entry to get sendq for
 * outputs      - sendq
 */
unsigned long
get_sendq(struct connection_entry *conn_p)
{
        struct send_queue *sendq_ptr;
        dlink_node *ptr;
        unsigned long sendq = 0;

        DLINK_FOREACH(ptr, conn_p->sendq.head)
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

int
sock_create(int domain)
{
	int fd = -1;
	int optval = 1;
	int flags;

	if((fd = socket(domain, SOCK_STREAM, 0)) < 0)
		return -1;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	flags= fcntl(fd, F_GETFL, 0);
	flags |= O_NONBLOCK;

	if(fcntl(fd, F_SETFL, flags) == -1)
		return -1;

	return fd;
}

#ifdef HAVE_GETADDRINFO
/* sock_open()
 *   attempts to open a connection
 *
 * inputs	- host/port to connect to, vhost to use
 * outputs	- fd of socket, -1 on error
 */
int
sock_open(const char *host, int port, const char *vhost, int type)
{
	struct addrinfo *hostres;
	int fd = -1;

	/* no specific vhost, try default */
	if(vhost == NULL)
		vhost = config_file.vhost;

	if(vhost != NULL)
	{
		struct addrinfo *bindres;

		if((bindres = gethostinfo(vhost, 0)) != NULL)
		{
			fd = sock_create(bindres->ai_family);
			if(fd < 0)
			{
				slog("Connection to %s/%d failed: (socket()/fcntl(): %s)",
				     host, port, strerror(errno));
				sendto_all(UMODE_SERVER,
					   "Connection to %s/%d failed: (socket()/fcntl(): %s)",
					   host, port, strerror(errno));
				return -1;
			}

			if((bind(fd, bindres->ai_addr, bindres->ai_addrlen)) < 0)
			{
				slog("Connection to %s/%d failed: "
                                     "(unable to bind to %s: %s)",
				     host, port, vhost, strerror(errno));
				sendto_all(UMODE_SERVER,
                                           "Connection to %s/%d failed: "
                                           "(unable to bind to %s: %s)",
				           host, port, vhost, strerror(errno));
				return -1;
			}
		}
		freeaddrinfo(bindres);
	}

	if(type == IO_HOST)
	{
		if((hostres = gethostinfo(host, port)) == NULL)
		{
			slog("Connection to %s/%d failed: "
                             "(unable to resolve: %s)",
			     host, port, host);
			sendto_all(UMODE_SERVER,
                                   "Connection to %s/%d failed: "
                                   "(unable to resolve: %s)",
				   host, port, host);
			return -1;
		}
		if(fd < 0)
			fd = sock_create(hostres->ai_family);
		if(fd < 0)
		{
			slog("Connection to %s/%d failed: (socket()/fcntl(): %s)",
			     host, port, strerror(errno));
			sendto_all(UMODE_SERVER,
				   "Connection to %s/%d failed: (socket()/fcntl(): %s)",
				   host, port, strerror(errno));
			return -1;
		}
		connect(fd, hostres->ai_addr, hostres->ai_addrlen);
		freeaddrinfo(hostres);
		return fd;
	}
	else
	{
		struct sockaddr_in raddr;
		unsigned long hl;

		if(fd < 0)
			fd = sock_create(AF_INET);
		if(fd < 0)
		{
			slog("Connection to %s/%d failed: (socket()/fcntl(): %s)",
			     host, port, strerror(errno));
			sendto_all(UMODE_SERVER,
				   "Connection to %s/%d failed: (socket()/fcntl(): %s)",
				   host, port, strerror(errno));
			return -1;
		}
		memset(&raddr, 0, sizeof(struct sockaddr_in));
		raddr.sin_family = AF_INET;
		raddr.sin_port = htons(port);
		hl = strtoul(host, NULL, 10);
		raddr.sin_addr.s_addr = htonl(hl);

		connect(fd, (struct sockaddr *) &raddr, sizeof(struct sockaddr_in));
		return fd;
	}
}
#else
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

	fd = sock_create();

	if(fd < 0)
	{
		slog("Connection to %s/%d failed: (socket()/fcntl(): %s)",
		     host, port, strerror(errno));
		sendto_all(UMODE_SERVER,
                           "Connection to %s/%d failed: (socket()/fcntl(): %s)",
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

			if(bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
			{
				slog("Connection to %s/%d failed: "
                                     "(unable to bind to %s: %s)",
				     host, port, vhost, strerror(errno));
				sendto_all(UMODE_SERVER,
                                           "Connection to %s/%d failed: "
                                           "(unable to bind to %s: %s)",
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
			slog("Connection to %s/%d failed: "
                             "(unable to resolve: %s)",
			     host, port, host);
			sendto_all(UMODE_SERVER,
                                   "Connection to %s/%d failed: "
                                   "(unable to resolve: %s)",
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

	connect(fd, (struct sockaddr *) &raddr, sizeof(struct sockaddr_in));
	return fd;
}
#endif

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
	close(conn_p->fd);
	conn_p->fd = -1;

}

#ifdef HAVE_GETADDRINFO
/* Stolen from FreeBSD's whois client, modified for rserv by W. Campbell */
static struct addrinfo *gethostinfo(char const *host, int port)
{
	struct addrinfo hints, *res;
	int error;
	char portbuf[6];

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = 0;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(portbuf, 6, "%d", port);
	error = getaddrinfo(host, portbuf, &hints, &res);
	if (error)
	{
		slog("gethostinfo error: %s: %s", host, gai_strerror(error));
		return (NULL);
	}
	return (res);
}
#endif

