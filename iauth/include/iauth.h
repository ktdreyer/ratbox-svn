#ifndef INCLUDED_iauth_h
#define INCLUDED_iauth_h

typedef struct ServerConnection Server;

#define    ICONF        "./iauth.conf"

#define    BUFSIZE      512

struct ServerConnection
{
	struct ServerConnection *next, *prev;

	int sockfd; /* socket descriptor for their connection */
};

#endif /* INCLUDED_iauth_h */
