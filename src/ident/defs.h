/*
 * defs.h $Id$
 */


#ifndef DEFS_H
#define DEFS_H 1



#include "../../include/setup.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>  
#include <time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h> 
#include <unistd.h>
#include <errno.h> 
#include <assert.h>

void set_time(void);

#ifdef IPV6
#define irc_sockaddr_storage sockaddr_storage
#else 
#define irc_sockaddr_storage sockaddr
#define ss_family sa_family
#ifdef SOCKADDR_IN_HAS_LEN
#define ss_len sa_len
#endif
#endif


#ifdef SOCKADDR_IN_HAS_LEN
#define SET_SS_LEN(x, y) ((struct irc_sockaddr_storage)(x)).ss_len = y
#define GET_SS_LEN(x) x.ss_len
#else
#define SET_SS_LEN(x, y)
#ifdef IPV6
#define GET_SS_LEN(x) x.ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)
#else
#define GET_SS_LEN(x) sizeof(struct sockaddr_in)
#endif
#endif

#endif
