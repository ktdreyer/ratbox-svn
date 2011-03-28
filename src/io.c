/* src/io.c
 *   Contains code for handling input and output to sockets.
 *
 * Copyright (C) 2003-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2007 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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

#include "rserv.h"
#include "io.h"
#include "event.h"
#include "conf.h"

#include "client.h"
#include "log.h"
#include "service.h"
#include "hook.h"
#include "serno.h"
#include "watch.h"
#include "tools.h"

#define IO_HOST	0
#define IO_IP	1

rb_dlink_list connection_list;
struct lconn *server_p;

time_t last_connect_time;
time_t current_time;

static int make_sockaddrs(const char *host, int port, const char *vhost, int type, struct sockaddr_in *remoteaddr, struct sockaddr_in *localaddr);

static void send_queued(struct lconn *conn_p);
static void signon_server(struct lconn *conn_p); 
static void signon_client_in(rb_fde_t *F, int status, struct sockaddr *addr, rb_socklen_t addrlen, void *data);
static int signon_client_out(struct lconn *conn_p);

static void read_server(rb_fde_t *F, void *data);
static void read_client(rb_fde_t *F, void *data);
static void write_sendq(rb_fde_t *F, void *data);
static void parse_server(char *buf, int len);
static void parse_client(struct lconn *conn_p, char *buf, int len);
#ifdef HAVE_GETADDRINFO
static struct addrinfo *gethostinfo(char const *host, int port);
#endif


static void check_server_status(void *data)
{

	if(server_p == NULL)
		return;

	/* socket isnt dead.. */
	if(!ConnDead(server_p))
	{
		/* client struct is.  im not sure how this
		 * could happen, but..
		 */
		if(server_p->client_p != NULL && 
			IsDead(server_p->client_p))
		{
			mlog("Connection to server %s lost: (Server exited)",
				server_p->name);
			sendto_all("Connection to server %s lost: (Server exited)",
				server_p->name);
			signoff_server(server_p);
		}

		/* connection timed out.. */
		else if(ConnConnecting(server_p) &&
				((server_p->first_time + 30) <= rb_time()))
		{
			mlog("Connection to server %s timed out",
				server_p->name);
			sendto_all("Connection to server %s timed out",
				server_p->name);
			signoff_server(server_p);
		}

		/* authentication timed out.. */
		else if(ConnHandshake(server_p) &&
				((server_p->first_time + 60) <=
				 rb_time()))
		{
			mlog("Connection to server %s timed out",
				server_p->name);
			sendto_all("Connection to server %s timed out",
				server_p->name);
			signoff_server(server_p);
		}

		/* pinged out */
		else if(ConnSentPing(server_p) &&
			((server_p->last_time + config_file.ping_time*2)
			<= rb_time()))
		{
			mlog("Connection to server %s lost: (Ping timeout)",
				server_p->name);
			sendto_all("Connection to server %s lost: (Ping timeout)",
				server_p->name);
			signoff_server(server_p);
		}

		/* no data for a while.. send ping */
		else if(!ConnSentPing(server_p) &&
			((server_p->last_time + config_file.ping_time) 
			<= rb_time()))
		{
			sendto_server("PING :%s", MYUID);
			SetConnSentPing(server_p);
		}
	}
}


static void cleanup_exited_clients(void *data)
{
	rb_dlink_node *ptr, *next_ptr;
	struct lconn *conn_p;
	/* remove any timed out/dead connections */
	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, connection_list.head)
	{
		conn_p = ptr->data;
		if(ConnDead(conn_p))
		{
			rb_free(conn_p->name);
			rb_free(conn_p);
			rb_dlinkDelete(ptr, &connection_list);
		}
	}

	/* we can safely exit anything thats dead at this point */
	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, exited_list.head)
	{
		free_client(ptr->data);
	}
	exited_list.head = exited_list.tail = NULL;
	exited_list.length = 0;

}

void add_server_events(void)
{
	rb_event_add("check_server_status", check_server_status, NULL, 5);
	rb_event_add("cleanup_exited_clients", cleanup_exited_clients, NULL, 60);
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
        rb_dlink_node *ptr;

        if(rb_dlink_list_length(&conf_server_list) <= 0)
                die(0, "No servers to connect to");

        RB_DLINK_FOREACH(ptr, conf_server_list.head)
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
                conf_p->last_connect = rb_time();
        }

        return conf_p;
}

static void
connect_callback(rb_fde_t *F, int status, void *data)
{
	struct lconn *conn_p = data;
	if(status != RB_OK)
	{	
		if(conn_p == server_p) 
		{
			/* RB_ERR_TIMEOUT doesn't have an errno */
			if(status == RB_ERR_TIMEOUT)
			{
				mlog("Connection to server %s timed out", server_p->name);
				sendto_all("Connection to server %s timed out", server_p->name);
				signoff_server(conn_p);
			} else {
				const char *errstr = strerror(rb_get_sockerr(F));
				mlog("Connection to server %s failed: %s", server_p->name, errstr);
				sendto_all("Connection to server %s failed: %s", server_p->name, errstr);
			}
		}
		else
			signoff_client(conn_p); 
		return;
	}

	if(conn_p == server_p)
	{
		signon_server(conn_p);
	} else {
		signon_client_out(conn_p);
	}
}



static void
connect_to_generic(struct lconn *conn_p, const char *host, int port, const char *vhost, int flag, int ssl)
{
	int x, lsize;
	rb_fde_t *F;
	struct sockaddr_in localaddr, remoteaddr, *laddr = NULL;
	
	x = make_sockaddrs(host, port, vhost, flag, &remoteaddr, &localaddr);
	if(x <= 0)
		return;
	if(x > 2) 
	{
		laddr = &localaddr;
		lsize = GET_SS_LEN(laddr);
	}

	if((F = rb_socket(AF_INET, SOCK_STREAM, 0, "server connection")) == NULL)
	{
		mlog("Error opening socket for %s/%d: %s", host, port, strerror(errno));
		return;
	}

	conn_p->F = F;
	if(ssl)
		rb_connect_tcp_ssl(conn_p->F, (struct sockaddr *)&remoteaddr, (struct sockaddr *)laddr, lsize, connect_callback, conn_p, 30);
	else
		rb_connect_tcp(conn_p->F, (struct sockaddr *)&remoteaddr, (struct sockaddr *)laddr, lsize, connect_callback, conn_p, 30);
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
	struct lconn *conn_p;

	if(server_p != NULL)
		return;

        /* no specific server, so try autoconn */
        if(target_server == NULL)
        {
                /* no autoconnect? */
                if((conf_p = next_autoconn()) == NULL)
                        die(1, "No server to autoconnect to.");
        }
        else
                conf_p = target_server;

	mlog("Connection to server %s/%d activated", 
             conf_p->name, conf_p->port);
	sendto_all("Connection to server %s/%d activated",
                   conf_p->name, conf_p->port);

	conn_p = rb_malloc(sizeof(struct lconn));
	conn_p->name = rb_strdup(conf_p->name);
	conn_p->first_time = conn_p->last_time = rb_time();
        conn_p->pass = rb_strdup(conf_p->pass);
	SetConnConnecting(conn_p);
	server_p = conn_p;
	connect_to_generic(conn_p, conf_p->host, conf_p->port, conf_p->vhost, IO_HOST, ConfServerSSL(conf_p));
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
	struct lconn *conn_p;

	conn_p = rb_malloc(sizeof(struct lconn));
	conn_p->name = rb_strdup(oper_p->name);

	conn_p->oper = oper_p;
	oper_p->refcount++;

	conn_p->first_time = conn_p->last_time = rb_time();

	SetConnConnecting(conn_p);
	SetConnDccOut(conn_p);
	rb_dlinkAddAlloc(conn_p, &connection_list);
	connect_to_generic(conn_p, host, port, config_file.dcc_vhost, IO_IP, 0);

}



void
connect_from_client(struct client *client_p, struct conf_oper *oper_p,
			const char *servicenick)
{
	struct lconn *conn_p;
	struct sockaddr_in addr;
	struct hostent *local_addr;
	unsigned long local_ip;
	rb_fde_t *F;
	int port;
	int res = -1;

	F = rb_socket(AF_INET, SOCK_STREAM, 0, "dcc listener");

	/* XXX ERROR */
	if(F == NULL)
		return;

	if(config_file.dcc_vhost == NULL ||
	   (local_addr = gethostbyname(config_file.dcc_vhost)) == NULL)
		return;


	for(port = config_file.dcc_low_port; port < config_file.dcc_high_port; 
	    port++)
	{
		memset(&addr, 0, sizeof(struct sockaddr_in));
		memcpy(&addr.sin_addr, local_addr->h_addr,
			local_addr->h_length);
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);

		res = bind(rb_get_fd(F), (struct sockaddr *) &addr,
			sizeof(struct sockaddr_in));

		if(res >= 0)
			break;
	}

	if(res < 0)
	{
		rb_close(F);
		return;
	}

	if(rb_listen(F, 1) < 0)
	{
		rb_close(F);
		return;
	}

	conn_p = rb_malloc(sizeof(struct lconn));
	conn_p->name = rb_strdup(oper_p->name);
        conn_p->oper = oper_p;
	oper_p->refcount++;

	conn_p->F = F;
	conn_p->first_time = conn_p->last_time = rb_time();

	SetConnConnecting(conn_p);
	SetConnDccIn(conn_p);

	rb_dlinkAddAlloc(conn_p, &connection_list);

	memcpy(&local_ip, local_addr->h_addr, local_addr->h_length);
	local_ip = htonl(local_ip);


	sendto_server(":%s PRIVMSG %s :\001DCC CHAT chat %lu %d\001",
		      servicenick, UID(client_p), local_ip, port);

	rb_accept_tcp(conn_p->F, NULL, signon_client_in, conn_p);
}

/* signon_server()
 *   sends our connection information to a server
 *
 */
static void
signon_server(struct lconn *conn_p)
{
        ClearConnConnecting(conn_p);
        SetConnHandshake(conn_p);

	/* ok, if connect() failed, this will cause an error.. */
	sendto_server("PASS %s TS 6 %s", conn_p->pass, config_file.sid);

	/* ..so we need to return. */
	if(ConnDead(conn_p))
		return;

	mlog("Connection to server %s established", conn_p->name);
	sendto_all("Connection to server %s established",
                   conn_p->name);

	sendto_server("CAPAB :QS TB EX IE ENCAP SERVICES");
	sendto_server("SERVER %s 1 :%s", MYNAME, config_file.gecos);
	read_server(conn_p->F, conn_p);
	return;
}


static void
signon_client_in(rb_fde_t *F, int status, struct sockaddr *addr, rb_socklen_t addrlen, void *data)
{
	struct lconn *conn_p = data;
	
	if(status != RB_OK)
	{
		rb_close(conn_p->F);
		conn_p->F = NULL;
		signoff_client(conn_p);
		return;
	}
	ClearConnConnecting(conn_p);
	rb_close(conn_p->F);
	conn_p->F = F;
	

	sendto_one(conn_p, "Welcome to %s, version ratbox-services-%s",
		   MYNAME, RSERV_VERSION);

        sendto_one(conn_p, "Please login via .login <username> <password>");
        read_client(conn_p->F, conn_p);

	return;

}

/* signon_client_out()
 *   sends the initial connection info to a new client of ours
 *
 * inputs       - connection entry to send to
 * outputs      - 1 on success, -1 on failure
 */
static int
signon_client_out(struct lconn *conn_p)
{
	ClearConnConnecting(conn_p);

	sendto_one(conn_p, "Welcome to %s, version ratbox-services-%s",
		   MYNAME, RSERV_VERSION);

	if(ConnDead(conn_p))
		return -1;

        sendto_one(conn_p, "Please login via .login <username> <password>");
        read_client(conn_p->F, conn_p);
	return 1;
}

void
signoff_client(struct lconn *conn_p)
{
	if(ConnDead(conn_p))
		return;

	hook_call(HOOK_DCC_EXIT, conn_p, NULL);

	/* Mark it as dead right away to avoid infinite calls! -- jilles */
	SetConnDead(conn_p);

	if(UserAuth(conn_p))
		watch_send(WATCH_AUTH, NULL, conn_p, 1, "has logged out (dcc)");

	if(conn_p->oper != NULL)
		deallocate_conf_oper(conn_p->oper);

	rb_close(conn_p->F);
	conn_p->F = NULL;
}

void
signoff_server(struct lconn *conn_p)
{
	struct client *service_p;
	rb_dlink_node *ptr;

	if(ConnDead(conn_p))
		return;

	/* Mark it as dead right away to avoid infinite calls! -- jilles */
	SetConnDead(conn_p);

	/* clear any introduced status */
	RB_DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		ClearServiceIntroduced(service_p);
		del_client(service_p);
	}

	if(conn_p == server_p)
	{
		rb_event_addonce("connect_to_server", connect_to_server, NULL, 
				config_file.reconnect_time);

		if(server_p->client_p != NULL)
			exit_client(server_p->client_p, 0);
	}

	rb_close(conn_p->F);
	conn_p->F = NULL;
}

static char readbuf[8192];


static void
read_any(struct lconn *conn_p, int server)
{
	int length = 0;
	unsigned long total_read = 0;
	while(1)
	{
		length = rb_read(conn_p->F, readbuf, sizeof(readbuf));
		if(length < 0)
		{
			if(length < 0 && rb_ignore_errno(errno))
			{
				if(server)
					rb_setselect(conn_p->F, RB_SELECT_READ, read_server, conn_p);
				else 
					rb_setselect(conn_p->F, RB_SELECT_READ, read_client, conn_p);
				return;		
			} else {
				if(server) 
				{
					mlog("Connection to server %s lost: (Read error: %s)", conn_p->name, rb_strerror(errno));
					sendto_all("Connect to server %s lost: (Read error: %s)", conn_p->name, rb_strerror(errno));
					signoff_server(conn_p);
				} else {
					signoff_client(conn_p);
				}
				
				return;
			}
		} 
		if(length == 0)
		{
			if(server) 
			{
				mlog("Connection to server %s lost", conn_p->name);
				sendto_all("Connection to server %s lost", conn_p->name);
				signoff_server(conn_p);
			} else {
				signoff_client(conn_p);
			}
			
			return;
		}
		
		total_read += length;
		rb_linebuf_parse(&conn_p->lb_recvq, readbuf, length, 0); 
	}
	if(total_read > 0)
		conn_p->last_time = rb_time();
}



/* read_server()
 *   reads some data from the server, exiting it on read error
 *
 * inputs       - connection entry to read from [unused]
 * outputs      -
 */
static void
read_server(rb_fde_t *F, void *data)
{
	struct lconn *conn_p = data;
	int len;
	read_any(conn_p, 1);

	if(IsDead(conn_p))
		return;
	ClearConnSentPing(conn_p);
	while(1)
	{
		len = rb_linebuf_get(&conn_p->lb_recvq, readbuf, sizeof(readbuf), LINEBUF_COMPLETE, LINEBUF_PARSED);
		if(len <= 0 || IsDead(conn_p))
			return;
		
		parse_server(readbuf, len);
		if(IsDead(conn_p))
			return;
	}	
}

/* read_client()
 *   reads some data from a client, exiting it on read errors
 *
 * inputs       - connection entry to read from
 * outputs      -
 */
static void
read_client(rb_fde_t *F, void *data)
{
	struct lconn *conn_p = data;
	int len;
	read_any(conn_p, 0);
	if(IsDead(conn_p))
		return;
	
	while(1)
	{
		len = rb_linebuf_get(&conn_p->lb_recvq, readbuf, sizeof(readbuf), LINEBUF_COMPLETE, LINEBUF_PARSED);
		if(len <= 0 || IsDead(conn_p))
			return;
		parse_client(conn_p, readbuf, len);
		if(IsDead(conn_p))
			return;
	}
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
	const char *source;
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

	source = server_p->name;

	if(*ch == ':')
	{
		ch++;

		source = ch;

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

	parc = rb_string_to_array(ch, parv, MAXPARA);

	handle_scommand(source, command, (const char **) parv, parc);
}

/* parse_client()
 *   parses a given buffer and calls command handlers
 *
 * inputs       - connection who sent data, buffer to parse, length of buffer
 * outputs      -
 */
void
parse_client(struct lconn *conn_p, char *buf, int len)
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

        /* partyline */
	if(*ch != '.')
        {
                if(!UserAuth(conn_p))
                {
                        sendto_one(conn_p, "You must .login first");
                        return;
                }

		if(!UserChat(conn_p))
                {
                        sendto_one(conn_p, "You must '.chat on' first.");
                        return;
                }

                sendto_all_chat(conn_p, "<%s> %s", conn_p->name, ch);
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

	parc = rb_string_to_array(ch, parv, MAXPARA);

        /* pass it off to the handler */
	handle_ucommand(conn_p, command, (const char **) parv, parc);
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
	va_list args;
	buf_head_t linebuf;
	if(server_p == NULL || ConnDead(server_p))
		return;
	rb_linebuf_newbuf(&linebuf);
	va_start(args, format);
	rb_linebuf_putmsg(&linebuf, format, &args, NULL);
	va_end(args);
	rb_linebuf_attach(&server_p->lb_sendq, &linebuf);
	rb_linebuf_donebuf(&linebuf);
	send_queued(server_p);
}

/* sendto_one()
 *   attempts to send the given data to a given connection
 *
 * inputs       - connection to send to, data to send
 * outputs      -
 */
void
sendto_one(struct lconn *conn_p, const char *format, ...)
{
	va_list args;
	buf_head_t linebuf;
	if(conn_p == NULL || ConnDead(conn_p))
		return;

	rb_linebuf_newbuf(&linebuf);
	va_start(args, format);
	rb_linebuf_putmsg(&linebuf, format, &args, NULL);
	va_end(args);
	rb_linebuf_attach(&conn_p->lb_sendq, &linebuf);
	rb_linebuf_donebuf(&linebuf);
	send_queued(conn_p);
}

/* sendto_all()
 *   attempts to send the given data to all clients connected
 *
 * inputs       - umode required [0 for none], data to send
 * outputs      -
 */
void
sendto_all(const char *format, ...)
{
        struct lconn *conn_p;
        buf_head_t linebuf;
        rb_dlink_node *ptr;
        va_list args;

        rb_linebuf_newbuf(&linebuf);
 
        va_start(args, format);
        rb_linebuf_putmsg(&linebuf, format, &args, NULL);
        va_end(args);

        RB_DLINK_FOREACH(ptr, connection_list.head)
        {
                conn_p = ptr->data;

                if(!UserAuth(conn_p))
                        continue;
		rb_linebuf_attach(&conn_p->lb_sendq, &linebuf);
		send_queued(conn_p);                
        }
}

/* sendto_all_chat()
 *   attempts to send the given chat to all dcc clients except the
 *   originator
 *
 * inputs       - client sending this message, data
 * outputs      -
 */
void
sendto_all_chat(struct lconn *one, const char *format, ...)
{
        struct lconn *conn_p;
        buf_head_t linebuf;
        rb_dlink_node *ptr;
        va_list args;

        rb_linebuf_newbuf(&linebuf);
 
        va_start(args, format);
        rb_linebuf_putmsg(&linebuf, format, &args, NULL);
        va_end(args);

        RB_DLINK_FOREACH(ptr, connection_list.head)
        {
                conn_p = ptr->data;

                if(!UserAuth(conn_p) || !UserChat(conn_p))
                        continue;

                /* the one we shouldnt be sending to.. */
                if(conn_p == one)
                        continue;

		rb_linebuf_attach(&conn_p->lb_sendq, &linebuf);
		send_queued(conn_p);                
        }
}

/* get_sendq()
 *   gets the sendq of a given connection
 *
 * inputs       - connection entry to get sendq for
 * outputs      - sendq
 */
unsigned long
get_sendq(struct lconn *conn_p)
{
        return rb_linebuf_len(&conn_p->lb_sendq);
}

static void
send_queued(struct lconn *conn_p)
{
	int retlen;
	while((retlen = rb_linebuf_flush(conn_p->F, &conn_p->lb_sendq)) > 0)
	{
		/* nothing to do here currently...update stats or something later? */	
	}
	
	if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
	{
		SetConnDead(conn_p);
		return;
	}
	if(rb_linebuf_len(&conn_p->lb_sendq))
		rb_setselect(conn_p->F, RB_SELECT_WRITE, write_sendq, conn_p);
}

/* write_sendq()
 *   write()'s as much of a given users sendq as possible
 */
static void
write_sendq(rb_fde_t *F, void *data) 
{
	struct lconn *conn_p = data;
	send_queued(conn_p);
}


#ifdef HAVE_GETADDRINFO

static int
make_sockaddrs(const char *host, int port, const char *vhost, int type, struct sockaddr_in *remoteaddr, struct sockaddr_in *localaddr)
{
	struct addrinfo *hostres;
	int q = 0;
	unsigned long hl;

	/* no specific vhost, try default */
	if(vhost == NULL)
		vhost = config_file.vhost;

	
	if(vhost != NULL)
	{
		struct addrinfo *bindres;

		if((bindres = gethostinfo(vhost, 0)) == NULL)
		{
			mlog("Connection to %s/%d failed: (gethostinfo failed: %s)", host, port, strerror(errno));
			return 0;
		}
		memcpy(localaddr, bindres->ai_addr, bindres->ai_addrlen);
		freeaddrinfo(bindres);
		q = 1;
	}

	if(type == IO_HOST)
	{
		if((hostres = gethostinfo(host, port)) == NULL)
		{
			mlog("Connection to %s/%d failed: "
                             "(unable to resolve: %s)",
			     host, port, host);
			sendto_all("Connection to %s/%d failed: (unable to resolve: %s)",
					host, port, host);
			return -1;
		}
		memcpy(remoteaddr, hostres->ai_addr, hostres->ai_addrlen);
		freeaddrinfo(hostres);
		return q + 2;
	}
	else
	{
		memset(remoteaddr, 0, sizeof(struct sockaddr_in));
		remoteaddr->sin_family = AF_INET;
		remoteaddr->sin_port = htons(port);
		hl = strtoul(host, NULL, 10);
		remoteaddr->sin_addr.s_addr = htonl(hl);
		return q + 2;
	}
}
#else
static int
make_sockaddrs(const char *host, int port, const char *vhost, int type, struct sockaddr_in *remoteaddr, struct sockaddr_in *localaddr)
{
	struct hostent *host_addr;
	int q = 0;

	/* no specific vhost, try default */
	if(vhost == NULL)
		vhost = config_file.vhost;

	if(vhost != NULL)
	{
		struct sockaddr_in laddr;

		if((host_addr = gethostbyname(vhost)) != NULL)
		{
			memset(localaddr, 0, sizeof(struct sockaddr_in));
			memcpy(localaddr->sin_addr, host_addr->h_addr, 
			       host_addr->h_length);
			localaddr->sin_family = AF_INET;
			localaddr->sin_port = 0;
			q = 1;
		}
	}

	memset(remoteaddr, 0, sizeof(struct sockaddr_in));
	remoteaddr->sin_family = AF_INET;
	remoteaddr->sin_port = htons(port);

	if(type == IO_HOST)
	{
		if((host_addr = gethostbyname(host)) == NULL)
		{
			mlog("Connection to %s/%d failed: "
                             "(unable to resolve: %s)",
			     host, port, host);
			sendto_all("Connection to %s/%d failed: (unable to resolve: %s)",
					host, port, host);
			return 0;
		}

		memcpy(remoteaddr->sin_addr, host_addr->h_addr, host_addr->h_length);
	}
	else
	{
		unsigned long hl = strtoul(host, NULL, 10);
		remoteaddr->sin_addr.s_addr = htonl(hl);
	}

	return q + 2;
}
#endif


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
	snprintf(portbuf, sizeof(portbuf), "%d", port);
	error = getaddrinfo(host, portbuf, &hints, &res);
	if (error)
	{
		mlog("gethostinfo error: %s: %s", host, gai_strerror(error));
		return (NULL);
	}
	return (res);
}
#endif

