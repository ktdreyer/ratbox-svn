/*
 *  dns.c: An interface to the resolver daemon
 *  Copyright (C) 2005 Aaron Sethman <androsyn@ratbox.org>
 *  Copyright (C) 2005 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */



#include "stdinc.h"
#include "tools.h"
#include "struct.h"
#include "ircd_defs.h"
#include "parse.h"
#include "commio.h"
#include "res.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "event.h"
#include "s_log.h"
#include "s_conf.h"
#include "client.h"
#include "send.h"

#define IDTABLE 0xffff

#define DNS_HOST 	((char)'H')
#define DNS_REVERSE 	((char)'I')

static void submit_dns(const char, int id, int aftype, const char *addr);
static void fork_resolver(void);

static pid_t res_pid;

struct dnsreq
{
	DNSCB *callback;
	void *data;
};

static struct dnsreq querytable[IDTABLE];

static u_int16_t id = 1;
static int dns_fd = -1;
static int ctrl_fd = -1;

static u_int16_t 
assign_id(void)
{
	if(id < IDTABLE-1)
		id++;
	else
		id = 1;
	return(id);	
}


void
cancel_lookup(u_int16_t xid)
{
	struct dnsreq *req;
	req = &querytable[xid];
	req->callback = NULL;
	req->data = NULL;
}

u_int16_t
lookup_hostname(const char *hostname, int aftype, DNSCB *callback, void *data)
{
	struct dnsreq *req;
	int aft;
	u_int16_t nid;
	
	nid = assign_id();
	req = &querytable[nid];

	req->callback = callback;
	req->data = data;
	
#ifdef IPV6
	if(aftype == AF_INET6)
		aft = 6;
	else
#endif
		aft = 4;	
	
	submit_dns(DNS_HOST, nid, aft, hostname); 		
	return(id);
}

u_int16_t
lookup_ip(const char *addr, int aftype, DNSCB *callback, void *data)
{
	struct dnsreq *req;
	int aft;
	u_int16_t nid;
	
	nid = assign_id();
	req = &querytable[nid];

	req->callback = callback;
	req->data = data;
	
#ifdef IPV6
	if(aftype == AF_INET6)
	{
		if(ConfigFileEntry.fallback_to_ip6_int)
			aft = 5;
		else
			aft = 6;
	}
	else
#endif
		aft = 4;	
	
	submit_dns(DNS_REVERSE, nid, aft, addr); 		
	return(nid);
}


static void
results_callback(const char *callid, const char *status, const char *aftype, const char *results)
{
	struct dnsreq *req;
	u_int16_t nid;
	int st;
	int aft;
	nid = strtol(callid, NULL, 16);
	req = &querytable[nid];
	st = atoi(status);
	aft = atoi(aftype);
	if(req->callback == NULL)
	{
		/* got cancelled..oh well */
		req->data = NULL;
		return;
	}
#ifdef IPV6
	if(aft == 6 || aft == 5)
		aft = AF_INET6;
	else
#endif
		aft = AF_INET;
		
	req->callback(results, st, aft, req->data);
	req->callback = NULL;
	req->data = NULL;
}

static int fork_count = 0;
static int spin_restart = 0;
static void
restart_spinning_resolver(void *unused)
{
	if(spin_restart > 10)
	{
		ilog(L_MAIN, "Tried to wait and restart the resolver %d times, giving up", spin_restart);
		sendto_realops_flags(UMODE_ALL, L_ALL, "Tried to wait and restart the resolver %d times, giving up", spin_restart);
		sendto_realops_flags(UMODE_ALL, L_ALL, "Try a manual restart with /rehash dns");
		spin_restart = 0;
		fork_count = 0;
		return;
	}
	fork_count = 0; /* reset the fork_count to 0 to let it try again */
	spin_restart++;
	fork_resolver();
}

static void
fork_resolver(void)
{
	int fdx[2];
	int cfdx[2];
	pid_t pid;
	int i;
	char fx[5];
	char fy[5];

	if(fork_count > 10)
	{
		ilog(L_MAIN, "Resolver has forked %d times, waiting 15 seconds to restart it again", fork_count);
		ilog(L_MAIN, "DNS resolution will be unavailable during this time");
		sendto_realops_flags(UMODE_ALL, L_ALL, "Resolver has forked %d times waiting 30 seconds to restart it again", fork_count);
		sendto_realops_flags(UMODE_ALL, L_ALL, "DNS resolution will be unavailable during this time");
		eventAddOnce("restart_spinning_resolver", restart_spinning_resolver, NULL, 30);		
		return;
	}
	fork_count++;
	if(dns_fd > 0)
		comm_close(dns_fd);
	if(ctrl_fd > 0)
		comm_close(ctrl_fd);
	if(res_pid > 0)
		kill(res_pid, SIGKILL);

	socketpair(AF_UNIX, SOCK_DGRAM, 0, fdx);
	socketpair(AF_UNIX, SOCK_STREAM, 0, cfdx);

	ircsnprintf(fx, sizeof(fx), "%d", fdx[1]);
	ircsnprintf(fy, sizeof(fy), "%d", cfdx[1]);
	
	comm_set_nb(fdx[0]);
	comm_set_nb(fdx[1]);
	comm_set_nb(cfdx[0]);
	comm_set_nb(cfdx[1]);
	
	
	if(!(pid = fork()))
	{
		setenv("FD", fx,1);
		setenv("CFD", fy, 1);	
		close(fdx[0]);
		close(cfdx[0]);
		/* set our fds as non blocking and close everything else */
		for (i = 0; i < HARD_FDLIMIT; i++)
		{
                        if((i == fdx[1]) || (i == cfdx[1]))
                                comm_set_nb(i);
			else
				close(i);
		}
		execl(BINPATH "/resolver", "-resolver", NULL);
	} else
	if(pid == -1)
	{
		close(fdx[0]);
		close(fdx[1]);
		close(cfdx[0]);
		close(cfdx[1]);
		return;
	}
	close(fdx[1]);
	close(cfdx[1]);	
	comm_open(fdx[0], FD_SOCKET, "Resolver daemon socket");
	comm_open(cfdx[0], FD_SOCKET, "Resolver daemon control socket");
	dns_fd = fdx[0];
	ctrl_fd = cfdx[0];
	fork_count = 0;
	res_pid = pid;
	return;
}

static void
read_dns(int fd, void *unused)
{
	char buf[512];
	char *p;
	char *parv[MAXPARA];
	int parc;
	int res;

	while(1)
	{
		res = recv(dns_fd, buf, sizeof(buf), 0);	
		if(res < 0)
		{
			if(ignoreErrno(errno))
			{	
				comm_setselect(dns_fd, FDLIST_SERVICE, COMM_SELECT_READ, read_dns, NULL, 0);
				return;
			} else {
				fork_resolver();
				return;	
			}
		} else if(res == 0) {
			fork_resolver();
			return;
		}
	 	p = memchr(buf, '\n', sizeof(buf));	
		if(p != NULL)
			*p = '\0';	
		/* we should get a full packet here */
		parc = string_to_array(buf, parv); /* we shouldn't be using this here, but oh well */
#if 0
		if(parc != 4)
		{
			ilog(L_MAIN, "Resolver sent a result with wrong number of arguments");
			fork_resolver();
			return;
		}
#endif
		results_callback(parv[1], parv[2], parv[3], parv[4]);
	}
}

void 
submit_dns(char type, int nid, int aftype, const char *addr)
{
	char buf[512];
	int res, len;
	ircsnprintf(buf, sizeof(buf), "%c %x %d %s\n", type, nid, aftype, addr);
	len = strlen(buf);
	res = send(dns_fd, buf, len, 0);
	
	if(res <= 0)
	{
		fork_resolver();
		return;
	}
	/* try to read back results */
	read_dns(dns_fd, NULL);
}



void
init_resolver(void)
{
	fork_resolver();
	if(res_pid < 0)
	{
		ilog(L_MAIN, "Unable to fork resolver: %s", strerror(errno));		
	}
}

void 
restart_resolver(void)
{
	if(res_pid > 0)
		kill(res_pid, SIGHUP);
	else 
		fork_resolver();
}

void
resolver_sigchld(void)
{
	int status;
	if(waitpid(res_pid, &status, WNOHANG) == res_pid)
	{
		fork_resolver();		
	}
}

