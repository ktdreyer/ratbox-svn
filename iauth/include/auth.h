#ifndef INCLUDED_auth_h
#define INCLUDED_auth_h

#ifndef INCLUDED_sock_h
#include "sock.h" /* USERLEN/HOSTLEN */
#endif

/* auth.c prototypes */

void AcceptAuthRequests();

#define  MAXPARAMS       15

#define  AM_ID_FAILED    0x00001   /* ident request failed */

struct AuthRequest
{
	struct AuthRequest *next, *prev;

	unsigned int flags;     /* current state of request */
	time_t       timeout;   /* time when query expires */
	int          identfd;   /* ident query socket descriptor */
	int          serverfd;  /* fd of server the request came from */
	char         clientid[100];  /* unique client ID */

	char         username[USERLEN + 1]; /* user's ident */
	char         hostname[HOSTLEN + 1]; /* user's hostname */
};

#endif /* INCLUDED_auth_h */
