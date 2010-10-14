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
 
struct lconn
{
	struct client *client_p;
	char *name;
	char *sid;
        char *pass;

	rb_fde_t *F;
	int flags;
	unsigned int privs;		/* privs as an oper */
	unsigned int sprivs;		/* privs on services */
	int watchflags;
	time_t first_time;
	time_t last_time;

        struct conf_oper *oper;

	buf_head_t lb_recvq;
	buf_head_t lb_sendq;
};

extern struct lconn *server_p;
extern rb_dlink_list connection_list;

#define CONN_CONNECTING		0x0001
#define CONN_DCCIN		0x0002
#define CONN_DCCOUT		0x0004
#define CONN_HANDSHAKE          0x0008
#define CONN_DEAD		0x0010
#define CONN_SENTPING           0x0020
#define CONN_TS			0x0040
#define CONN_CAP_SERVICE	0x0080
#define CONN_CAP_RSFNC		0x0100
#define CONN_CAP_TB		0x0200
/* CONTINUES ... */

#define ConnConnecting(x)	((x)->flags & CONN_CONNECTING)
#define ConnDccIn(x)		((x)->flags & CONN_DCCIN)
#define ConnDccOut(x)		((x)->flags & CONN_DCCOUT)
#define ConnHandshake(x)	((x)->flags & CONN_HANDSHAKE)
#define ConnDead(x)		((x)->flags & CONN_DEAD)
#define ConnSentPing(x)		((x)->flags & CONN_SENTPING)
#define ConnTS(x)		((x)->flags & CONN_TS)
#define ConnCapService(x)	((x)->flags & CONN_CAP_SERVICE)
#define ConnCapRSFNC(x)		((x)->flags & CONN_CAP_RSFNC)
#define ConnCapTB(x)		((x)->flags & CONN_CAP_TB)

#define SetConnConnecting(x)	((x)->flags |= CONN_CONNECTING)
#define SetConnDccIn(x)		((x)->flags |= CONN_DCCIN)
#define SetConnDccOut(x)	((x)->flags |= CONN_DCCOUT)
#define SetConnHandshake(x)	((x)->flags |= CONN_HANDSHAKE)
#define SetConnDead(x)		((x)->flags |= CONN_DEAD)
#define SetConnSentPing(x)	((x)->flags |= CONN_SENTPING)
#define SetConnTS(x)		((x)->flags |= CONN_TS)

#define ClearConnConnecting(x)	((x)->flags &= ~CONN_CONNECTING)
#define ClearConnHandshake(x)	((x)->flags &= ~CONN_HANDSHAKE)
#define ClearConnSentPing(x)	((x)->flags &= ~CONN_SENTPING)

/* server flags */
#define CONN_FLAGS_EOB		0x02000
#define CONN_FLAGS_SENTBURST	0x04000
/* CONTINUES ... */

#define SetConnSentBurst(x)	((x)->flags |= CONN_FLAGS_SENTBURST)
#define SetConnEOB(x)		((x)->flags |= CONN_FLAGS_EOB)

#define finished_bursting	((server_p) && (server_p->flags & CONN_FLAGS_EOB))
#define sent_burst		((server_p) && (server_p->flags & CONN_FLAGS_SENTBURST))

/* user flags */
#define CONN_FLAGS_AUTH		0x10000
#define CONN_FLAGS_CHAT		0x20000

#define UserAuth(x)		((x)->flags & CONN_FLAGS_AUTH)
#define SetUserAuth(x)		((x)->flags |= CONN_FLAGS_AUTH)
#define UserChat(x)		((x)->flags & CONN_FLAGS_CHAT)
#define SetUserChat(x)		((x)->flags |= CONN_FLAGS_CHAT)
#define ClearUserChat(x)	((x)->flags &= ~CONN_FLAGS_CHAT)

extern void add_server_events(void);
extern void signoff_server(struct lconn *);
extern void signoff_client(struct lconn *);
extern void connect_to_server(void *unused);
extern void connect_to_client(struct client *client_p, struct conf_oper *oper_p,
				const char *host, int port);
extern void connect_from_client(struct client *client_p, struct conf_oper *oper_p,
				const char *servicenick);

extern void PRINTFLIKE(1, 2) sendto_server(const char *format, ...);
extern void PRINTFLIKE(2, 3) sendto_one(struct lconn *, const char *format, ...);
extern void PRINTFLIKE(1, 2) sendto_all(const char *format, ...);
extern void PRINTFLIKE(2, 3) sendto_all_chat(struct lconn *, const char *format, ...);

extern rb_fde_t *sock_create(int);
extern rb_fde_t *sock_open(const char *host, int port, const char *vhost, int type);
extern rb_fde_t *sock_close(struct lconn *conn_p);
extern rb_fde_t *sock_write(struct lconn *conn_p, const char *buf, size_t len);

extern unsigned long get_sendq(struct lconn *conn_p);

#endif
