#ifndef INCLUDED_auth_h
#define INCLUDED_auth_h

#ifndef INCLUDED_sock_h
#include "sock.h" /* USERLEN/HOSTLEN */
#endif

struct AuthRequest
{
	struct AuthRequest *next, *prev;

	unsigned int flags;      /* current state of request */
	time_t       timeout;    /* time when query expires */
	int          identfd;    /* ident query socket descriptor */
	int          serverfd;   /* fd of server the request came from */
	char         clientid[IDLEN + 1];  /* unique client ID */

	struct in_addr ip;       /* ip address of client */
	unsigned int remoteport; /* client's remote port */
	unsigned int localport;  /* server's local port */

	char         username[USERLEN + 1]; /* user's ident */
	char         hostname[HOSTLEN + 1]; /* user's hostname */
};

/* auth.c prototypes */

void StartAuth(int sockfd, int parc, char **parv);
void SendIdentQuery(struct AuthRequest *auth);
void ReadIdentReply(struct AuthRequest *auth);

/*
 * External declarations
 */

extern struct AuthRequest *AuthPollList;
extern struct AuthRequest *AuthIncompleteList;

/* Maximum parameters an ircd server will send us */
#define MAXPARAMS       15

/* Buffer size for ident reply */
#define ID_BUFSIZE      128

/*
 * AM_* - Authentication Module Flags
 */

#define AM_IDENT_CONNECTING  (1 << 0) /* connecting to their ident port */
#define AM_IDENT_PENDING     (1 << 1) /* ident reply is pending */
#define AM_DNS_PENDING       (1 << 2) /* dns reply is pending */

/*
 * Authentication Module Macros
 */

#define SetDNSPending(x)     ((x)->flags |= AM_DNS_PENDING)
#define ClearDNSPending(x)   ((x)->flags &= ~AM_DNS_PENDING)
#define IsDNSPending(x)      ((x)->flags &  AM_DNS_PENDING)

#define SetIdentConnect(x)   ((x)->flags |= AM_IDENT_CONNECTING)
#define ClearIdentConnect(x) ((x)->flags &= ~AM_IDENT_CONNECTING)
#define IsIdentConnect(x)    ((x)->flags &  AM_IDENT_CONNECTING)

#define SetIdentPending(x)   ((x)->flags |= AM_IDENT_PENDING)
#define ClearIdentPending(x) ((x)->flags &= AM_IDENT_PENDING)
#define IsIdentPending(x)    ((x)->flags &  AM_IDENT_PENDING)

#define ClearAuth(x)         ((x)->flags &= ~(AM_IDENT_PENDING | AM_IDENT_CONNECTING))
#define IsDoingAuth(x)       ((x)->flags &  (AM_IDENT_PENDING | AM_IDENT_CONNECTING))

#endif /* INCLUDED_auth_h */
