#ifndef INCLUDED_sock_h
#define INCLUDED_sock_h

typedef struct PortInfo AuthPort;
typedef struct ServerConnection Server;

/* sock.c prototypes */

void tosock(int sockfd, char *format, ...);
void InitListenPorts();
int SetNonBlocking(int sockfd);
void CheckConnections();

/*
 * External variable declarations
 */

extern AuthPort *PortList;
extern Server   *ServerList;

#define  NOSOCK        (-1)

#define  USERLEN       10   /* length of username */
#define  HOSTLEN       63   /* length of hostname */
#define  IDLEN         50   /* should be ample for a client id */

struct PortInfo
{
	struct PortInfo *next;

	int port; /* port to listen on */
	int sockfd; /* socket file descriptor */
};

struct ServerConnection
{
	struct ServerConnection *next, *prev;

	int sockfd; /* socket descriptor for their connection */
};

#endif /* INCLUDED_sock_h */
