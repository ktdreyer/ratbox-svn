/* $Id$ */
#ifndef INCLUDED_io_h
#define INCLUDED_io_h

struct client;

struct send_queue
{
	const char *buf;
	int len;
	int pos;
};

struct connection_entry
{
	struct client *client_p;
	char *name;
	int fd;
	int flags;
	time_t first_time;
	time_t last_time;

	void (*io_read)(struct connection_entry *);
	int (*io_write)(struct connection_entry *);
	void (*io_close)(struct connection_entry *);

	dlink_list sendq;
};

extern struct connection_entry *server_p;
extern dlink_list connection_list;

#define CONN_CONNECTING		0x001
#define CONN_DEAD		0x002
#define FLAGS_UNTERMINATED	0x010

extern void read_io(void);

extern void connect_to_server(void *unused);
extern void connect_to_client(const char *name, const char *host, int port);

extern void sendto_server(const char *format, ...);
extern void sendto_connection(struct connection_entry *, const char *format, ...);

extern int sock_open(const char *host, int port, const char *vhost, int type);
extern void sock_close(struct connection_entry *conn_p);
extern int sock_write(struct connection_entry *conn_p, const char *buf, int len);

#endif
