/* $Id$ */
#ifndef INCLUDED_io_h
#define INCLUDED_io_h

struct client;
struct conf_oper;

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
        char *pass;

	int fd;
	int flags;
	time_t first_time;
	time_t last_time;

        struct conf_oper *oper;

	void (*io_read)(struct connection_entry *);
	int (*io_write)(struct connection_entry *);
	void (*io_close)(struct connection_entry *);

	dlink_list sendq;
};

extern struct connection_entry *server_p;
extern dlink_list connection_list;

#define CONN_CONNECTING		0x0001
#define CONN_DCCIN		0x0002
#define CONN_DCCOUT		0x0004
#define CONN_HANDSHAKE          0x0008
#define CONN_DEAD		0x0010
#define CONN_SENTPING           0x0020

#define ConnConnecting(x)	((x)->flags & CONN_CONNECTING)
#define ConnDccIn(x)		((x)->flags & CONN_DCCIN)
#define ConnDccOut(x)		((x)->flags & CONN_DCCOUT)
#define ConnHandshake(x)	((x)->flags & CONN_HANDSHAKE)
#define ConnDead(x)		((x)->flags & CONN_DEAD)
#define ConnSentPing(x)		((x)->flags & CONN_SENTPING)

#define SetConnConnecting(x)	((x)->flags |= CONN_CONNECTING)
#define SetConnDccIn(x)		((x)->flags |= CONN_DCCIN)
#define SetConnDccOut(x)	((x)->flags |= CONN_DCCOUT)
#define SetConnHandshake(x)	((x)->flags |= CONN_HANDSHAKE)
#define SetConnDead(x)		((x)->flags |= CONN_DEAD)
#define SetConnSentPing(x)	((x)->flags |= CONN_SENTPING)

#define ClearConnConnecting(x)	((x)->flags &= ~CONN_CONNECTING)
#define ClearConnHandshake(x)	((x)->flags &= ~CONN_HANDSHAKE)
#define ClearConnSentPing(x)	((x)->flags &= ~CONN_SENTPING)

/* server flags */
#define FLAGS_UNTERMINATED	0x0100
#define FLAGS_EOB               0x0200

#define finished_bursting	((server_p) && (server_p->flags & FLAGS_EOB))

/* user flags */
#define FLAGS_AUTH		0x0100

#define UserAuth(x)	((x)->flags & FLAGS_AUTH)
#define SetUserAuth(x)	((x)->flags |= FLAGS_AUTH)

/* usermodes */
#define UMODE_CHAT              0x1000
#define UMODE_AUTH              0x2000
#define UMODE_SERVER            0x4000

#define UMODE_ALL               (UMODE_CHAT|UMODE_AUTH|UMODE_SERVER)
#define UMODE_DEFAULT           UMODE_ALL

#define IsUmodeChat(x)          ((x)->flags & UMODE_CHAT)

extern void read_io(void);

extern void connect_to_server(void *unused);
extern void connect_to_client(struct client *client_p, struct conf_oper *oper_p,
				const char *host, int port);
extern void connect_from_client(struct client *client_p, struct conf_oper *oper_p,
				const char *servicenick);

extern void sendto_server(const char *format, ...);
extern void sendto_one(struct connection_entry *, const char *format, ...);
extern void sendto_all(int umode, const char *format, ...);
extern void sendto_all_butone(struct connection_entry *, int umode, 
                              const char *format, ...);

extern int sock_create(int);
extern int sock_open(const char *host, int port, const char *vhost, int type);
extern void sock_close(struct connection_entry *conn_p);
extern int sock_write(struct connection_entry *conn_p, const char *buf, int len);

extern unsigned long get_sendq(struct connection_entry *conn_p);

#endif
