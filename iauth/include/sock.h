#ifndef INCLUDED_sock_h
#define INCLUDED_sock_h

typedef struct PortInfo AuthPort;

/* sock.c prototypes */

void tosock(int sockfd, char *format, ...);
void InitListenPorts();
int SetNonBlocking(int sockfd);

#define  NOSOCK        (-1)

#define  USERLEN       10   /* length of username */
#define  HOSTLEN       63   /* length of hostname */

struct PortInfo
{
	struct PortInfo *next;

	int port; /* port to listen on */
	int sockfd; /* socket file descriptor */
};

#endif /* INCLUDED_sock_h */
