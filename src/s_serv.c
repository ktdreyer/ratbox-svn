/*
 *  ircd-ratbox: A slightly useful ircd.
 *  s_serv.c: Server related functions.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
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

#ifdef HAVE_LIBCRYPTO
#include <openssl/rsa.h>
#include "rsa.h"
#endif

#include "tools.h"
#include "s_serv.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "event.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "numeric.h"
#include "packet.h"
#include "res.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_stats.h"
#include "s_user.h"
#include "scache.h"
#include "send.h"
#include "client.h"
#include "memory.h"
#include "channel.h"		/* chcap_usage_counts stuff... */
#include "hook.h"
extern char *crypt();

#define MIN_CONN_FREQ 300

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

int MaxConnectionCount = 1;
int MaxClientCount = 1;
int refresh_user_links = 0;

static char buf[BUFSIZE];

static void start_io(struct Client *server);

static SlinkRplHnd slink_error;
static SlinkRplHnd slink_zipstats;
/*
 * list of recognized server capabilities.  "TS" is not on the list
 * because all servers that we talk to already do TS, and the kludged
 * extra argument to "PASS" takes care of checking that.  -orabidoo
 */
struct Capability captab[] = {
/*  name     cap     */
	{ "QS",		CAP_QS },
	{ "EX",		CAP_EX },
	{ "CHW",	CAP_CHW},
	{ "IE", 	CAP_IE},
	{ "EOB",	CAP_EOB},
	{ "KLN",	CAP_KLN},
	{ "GLN",	CAP_GLN},
	{ "KNOCK",	CAP_KNOCK},
	{ "ZIP",	CAP_ZIP},
	{ "TBURST",	CAP_TBURST},
	{ "UNKLN",	CAP_UNKLN},
	{ "CLUSTER",	CAP_CLUSTER},
	{ "ENCAP",	CAP_ENCAP },
	{0, 0}
};

#ifdef HAVE_LIBCRYPTO
struct EncCapability CipherTable[] = {
#ifdef HAVE_EVP_BF_CFB
	{"BF/256", CAP_ENC_BF_256, 32, CIPHER_BF},
	{"BF/128", CAP_ENC_BF_128, 16, CIPHER_BF},
#endif
#ifdef HAVE_EVP_CAST5_CFB
	{"CAST/128", CAP_ENC_CAST_128, 16, CIPHER_CAST},
#endif
#ifdef HAVE_EVP_IDEA_CFB
	{"IDEA/128", CAP_ENC_IDEA_128, 16, CIPHER_IDEA},
#endif
#ifdef HAVE_EVP_RC5_32_12_16_CFB
	{"RC5.16/128", CAP_ENC_RC5_16_128, 16, CIPHER_RC5_16},
	{"RC5.12/128", CAP_ENC_RC5_12_128, 16, CIPHER_RC5_12},
	{"RC5.8/128", CAP_ENC_RC5_8_128, 16, CIPHER_RC5_8},
#endif
#ifdef HAVE_EVP_DES_EDE3_CFB
	{"3DES/168", CAP_ENC_3DES_168, 24, CIPHER_3DES},
#endif
#ifdef HAVE_EVP_DES_CFB
	{"DES/56", CAP_ENC_DES_56, 8, CIPHER_DES},
#endif

	{0, 0, 0, 0}
};
#endif

struct SlinkRplDef slinkrpltab[] = {
	{SLINKRPL_ERROR, slink_error, SLINKRPL_FLAG_DATA},
	{SLINKRPL_ZIPSTATS, slink_zipstats, SLINKRPL_FLAG_DATA},
	{0, 0, 0},
};

unsigned long nextFreeMask();
static unsigned long freeMask;
#ifndef __VMS
static int fork_server(struct Client *client_p);
#endif

static CNCB serv_connect_callback;


void
slink_error(unsigned int rpl, unsigned int len, unsigned char *data, struct Client *server_p)
{
	s_assert(rpl == SLINKRPL_ERROR);

	s_assert(len < 256);
	data[len - 1] = '\0';

	sendto_realops_flags(UMODE_ALL, L_ALL, "SlinkError for %s: %s", server_p->name, data);
	exit_client(server_p, server_p, &me, "servlink error -- terminating link");
}

void
slink_zipstats(unsigned int rpl, unsigned int len, unsigned char *data, struct Client *server_p)
{
	struct ZipStats zipstats;
	unsigned long in = 0, in_wire = 0, out = 0, out_wire = 0;
	int i = 0;

	s_assert(rpl == SLINKRPL_ZIPSTATS);
	s_assert(len == 16);
	s_assert(IsCapable(server_p, CAP_ZIP));

	/* Yes, it needs to be done this way, no we cannot let the compiler
	 * work with the pointer to the structure.  This works around a GCC
	 * bug on SPARC that affects all versions at the time of this writing.
	 * I will feed you to the creatures living in RMS's beard if you do
	 * not leave this as is, without being sure that you are not causing
	 * regression for most of our installed SPARC base.
	 * -jmallett, 04/27/2002
	 */
	memcpy(&zipstats, &server_p->localClient->zipstats, sizeof(struct ZipStats));

	in |= (data[i++] << 24);
	in |= (data[i++] << 16);
	in |= (data[i++] << 8);
	in |= (data[i++]);

	in_wire |= (data[i++] << 24);
	in_wire |= (data[i++] << 16);
	in_wire |= (data[i++] << 8);
	in_wire |= (data[i++]);

	out |= (data[i++] << 24);
	out |= (data[i++] << 16);
	out |= (data[i++] << 8);
	out |= (data[i++]);

	out_wire |= (data[i++] << 24);
	out_wire |= (data[i++] << 16);
	out_wire |= (data[i++] << 8);
	out_wire |= (data[i++]);

	/* This macro adds b to a if a plus b is not an overflow, and sets the
	 * value of a to b if it is.
	 * Add and Set if No Overflow.
	 */
#define	ASNO(a,b)	\
	a = (a + b >= a? a + b: b)

	ASNO(zipstats.in, in);
	ASNO(zipstats.out, out);
	ASNO(zipstats.in_wire, in_wire);
	ASNO(zipstats.out_wire, out_wire);

	if(zipstats.in > 0)
		zipstats.in_ratio =
			(((double) (zipstats.in - zipstats.in_wire) /
			  (double) zipstats.in) * 100.00);
	else
		zipstats.in_ratio = 0;

	if(zipstats.out > 0)
		zipstats.out_ratio =
			(((double) (zipstats.out - zipstats.out_wire) /
			  (double) zipstats.out) * 100.00);
	else
		zipstats.out_ratio = 0;

	memcpy(&server_p->localClient->zipstats, &zipstats, sizeof(struct ZipStats));
}

void
collect_zipstats(void *unused)
{
	dlink_node *ptr;
	struct Client *target_p;

	DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = ptr->data;
		if(IsCapable(target_p, CAP_ZIP))
		{
			/* only bother if we haven't already got something queued... */
			if(!target_p->localClient->slinkq)
			{
				target_p->localClient->slinkq = MyMalloc(1);	/* sigh.. */
				target_p->localClient->slinkq[0] = SLINKCMD_ZIPSTATS;
				target_p->localClient->slinkq_ofs = 0;
				target_p->localClient->slinkq_len = 1;
				send_queued_slink_write(target_p->localClient->ctrlfd, target_p);
			}
		}
	}
}


#ifdef HAVE_LIBCRYPTO
struct EncCapability *
check_cipher(struct Client *client_p, struct ConfItem *aconf)
{
	struct EncCapability *epref;

	/* Use connect{} specific info if available */
	if(aconf->cipher_preference)
		epref = aconf->cipher_preference;
	else
		epref = ConfigFileEntry.default_cipher_preference;

	/*
	 * If the server supports the capability in hand, return the matching
	 * conf struct.  Otherwise, return NULL (an error).
	 */
	if(IsCapableEnc(client_p, epref->cap))
		return (epref);

	return (NULL);
}
#endif /* HAVE_LIBCRYPTO */

/*
 * my_name_for_link - return wildcard name of my server name 
 * according to given config entry --Jto
 * XXX - this is only called with me.name as name
 */
const char *
my_name_for_link(const char *name, struct ConfItem *aconf)
{
	if(aconf->fakename)
		return (aconf->fakename);
	else
		return (name);
}

/*
 * hunt_server - Do the basic thing in delivering the message (command)
 *      across the relays to the specific server (server) for
 *      actions.
 *
 *      Note:   The command is a format string and *MUST* be
 *              of prefixed style (e.g. ":%s COMMAND %s ...").
 *              Command can have only max 8 parameters.
 *
 *      server  parv[server] is the parameter identifying the
 *              target server.
 *
 *      *WARNING*
 *              parv[server] is replaced with the pointer to the
 *              real servername from the matched client (I'm lazy
 *              now --msa).
 *
 *      returns: (see #defines)
 */
int
hunt_server(struct Client *client_p, struct Client *source_p,
	    const char *command, int server, int parc, const char *parv[])
{
	struct Client *target_p;
	int wilds;
	dlink_node *ptr;
	const char *old __unused = parv[server];
	char *new;

	/*
	 * Assume it's me, if no server
	 */
	if(parc <= server || EmptyString(parv[server]) ||
	   match(me.name, parv[server]) || match(parv[server], me.name))
		return (HUNTED_ISME);
	
	new = LOCAL_COPY(parv[server]);
	parv[server] = new;

	/*
	 * These are to pickup matches that would cause the following
	 * message to go in the wrong direction while doing quick fast
	 * non-matching lookups.
	 */
	if((target_p = find_client(parv[server])))
		if(target_p->from == source_p->from && !MyConnect(target_p))
			target_p = NULL;
	if(!target_p && (target_p = find_server(parv[server])))
		if(target_p->from == source_p->from && !MyConnect(target_p))
			target_p = NULL;

	collapse(new);
	wilds = (strchr(parv[server], '?') || strchr(parv[server], '*'));

	/*
	 * Again, if there are no wild cards involved in the server
	 * name, use the hash lookup
	 */
	if(!target_p)
	{
		if(!wilds)
		{
			if(!(target_p = find_server(parv[server])))
			{
				sendto_one(source_p,
					   form_str(ERR_NOSUCHSERVER),
					   me.name, parv[0], parv[server]);
				return (HUNTED_NOSUCH);
			}
		}
		else
		{

			DLINK_FOREACH(ptr, global_client_list.head)
			{
				target_p = NULL;
				if(match(parv[server], ((struct Client *) (ptr->data))->name))
				{
					target_p = ptr->data;
					break;
				}
			}
		}
	}

	if(target_p)
	{
		if(!IsRegistered(target_p))
		{
			sendto_one(source_p, form_str(ERR_NOSUCHSERVER),
				   me.name, parv[0], parv[server]);
			return HUNTED_NOSUCH;
		}

		if(IsMe(target_p) || MyClient(target_p))
			return HUNTED_ISME;

		if(!match(target_p->name, parv[server]))
			parv[server] = target_p->name;

/* BROKEN_TS6 */
		/* This is a little kludgy but should work... */
		if(IsTS6(target_p->from))
			parv[0] = use_id(source_p);

		sendto_one(target_p, command, parv[0],
			   parv[1], parv[2], parv[3], parv[4], parv[5], parv[6], parv[7], parv[8]);
		return (HUNTED_PASS);
	}

	sendto_one(source_p, form_str(ERR_NOSUCHSERVER), me.name, parv[0], parv[server]);
	return (HUNTED_NOSUCH);
}

/*
 * try_connections - scan through configuration and try new connections.
 * Returns the calendar time when the next call to this
 * function should be made latest. (No harm done if this
 * is called earlier or later...)
 */
void
try_connections(void *unused)
{
	struct ConfItem *aconf;
	struct Client *client_p;
	int connecting = FALSE;
	int confrq;
	time_t next = 0;
	struct Class *cltmp;
	struct ConfItem *con_conf = NULL;

	for (aconf = ConfigItemList; aconf; aconf = aconf->next)
	{
		/*
		 * Also when already connecting! (update holdtimes) --SRB 
		 */
		if(!(aconf->status & CONF_SERVER) || aconf->port <= 0 ||
		   !(aconf->flags & CONF_FLAGS_ALLOW_AUTO_CONN))
			continue;
		cltmp = ClassPtr(aconf);
		/*
		 * Skip this entry if the use of it is still on hold until
		 * future. Otherwise handle this entry (and set it on hold
		 * until next time). Will reset only hold times, if already
		 * made one successfull connection... [this algorithm is
		 * a bit fuzzy... -- msa >;) ]
		 */
		if(aconf->hold > CurrentTime)
		{
			if(next > aconf->hold || next == 0)
				next = aconf->hold;
			continue;
		}

		if((confrq = get_con_freq(cltmp)) < MIN_CONN_FREQ)
			confrq = MIN_CONN_FREQ;

		aconf->hold = CurrentTime + confrq;
		/*
		 * Found a CONNECT config with port specified, scan clients
		 * and see if this server is already connected?
		 */
		client_p = find_server(aconf->name);

		if(!client_p && (CurrUsers(cltmp) < MaxUsers(cltmp)) && !connecting)
		{
			con_conf = aconf;
			/* We connect only one at time... */
			connecting = TRUE;
		}
		if((next > aconf->hold) || (next == 0))
			next = aconf->hold;
	}


	/* TODO: change this to set active flag to 0 when added to event! --Habeeb */
	if(GlobalSetOptions.autoconn == 0)
		return;

	if(connecting)
	{
		if(con_conf->next)	/* are we already last? */
		{
			struct ConfItem **pconf;
			for (pconf = &ConfigItemList; (aconf = *pconf); pconf = &(aconf->next))
				/* 
				 * put the current one at the end and
				 * make sure we try all connections
				 */
				if(aconf == con_conf)
					*pconf = aconf->next;
			(*pconf = con_conf)->next = 0;
		}

		if(con_conf->flags & CONF_FLAGS_ALLOW_AUTO_CONN)
		{
			/*
			 * We used to only print this if serv_connect() actually
			 * suceeded, but since comm_tcp_connect() can call the callback
			 * immediately if there is an error, we were getting error messages
			 * in the wrong order. SO, we just print out the activated line,
			 * and let serv_connect() / serv_connect_callback() print an
			 * error afterwards if it fails.
			 *   -- adrian
			 */
#ifndef HIDE_SERVERS_IPS
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Connection to %s[%s] activated.",
					     con_conf->name, con_conf->host);
#else
			sendto_realops_flags(UMODE_ALL, L_ALL,
			    		     "Connection to %s activated",
					     con_conf->name);
#endif

			serv_connect(con_conf, 0);
		}
	}
}

int
check_server(const char *name, struct Client *client_p, int cryptlink)
{
	struct ConfItem *aconf = NULL;
	struct ConfItem *server_aconf = NULL;
	int error = -1;

	s_assert(NULL != client_p);

	if(client_p == NULL)
		return error;

	if(!(client_p->localClient->passwd))
		return -2;

	if(strlen(name) > HOSTLEN)
		return -4;

	/* loop through looking for all possible connect items that might work */
	for (aconf = ConfigItemList; aconf; aconf = aconf->next)
	{
		if((aconf->status & CONF_SERVER) == 0)
			continue;

		if(!match(name, aconf->name))
			continue;

		error = -3;

		/* XXX: Fix me for IPv6 */
		/* XXX sockhost is the IPv4 ip as a string */

		if(match(aconf->host, client_p->host) ||
		   match(aconf->host, client_p->sockhost))
		{
			error = -2;

#ifdef HAVE_LIBCRYPTO
			if(cryptlink && IsConfCryptLink(aconf))
			{
				if(aconf->rsa_public_key)
					server_aconf = aconf;
			}
			else if(!(cryptlink || IsConfCryptLink(aconf)))
#endif /* HAVE_LIBCRYPTO */
			{
				if(IsConfEncrypted(aconf))
				{
					if(strcmp(aconf->passwd,
						  crypt(client_p->
							localClient->passwd, aconf->passwd)) == 0)
						server_aconf = aconf;
				}
				else
				{
					if(strcmp
					   (aconf->passwd, client_p->localClient->passwd) == 0)
						server_aconf = aconf;
				}
			}
		}
	}

	if(server_aconf == NULL)
		return error;

	/* XXX -- is this detach_conf() needed? --fl */
	detach_conf(client_p);
	attach_conf(client_p, server_aconf);

#ifdef HAVE_LIBZ		/* otherwise, cleait unconditionally */
	if(!(server_aconf->flags & CONF_FLAGS_COMPRESSED))
#endif
		ClearCap(client_p, CAP_ZIP);
	if(!(server_aconf->flags & CONF_FLAGS_CRYPTLINK))
		ClearCap(client_p, CAP_ENC);

	if(aconf != NULL)
	{
#ifdef IPV6
		if(client_p->localClient->ip.ss_family == AF_INET6)
		{
			if(IN6_IS_ADDR_UNSPECIFIED(&((struct sockaddr_in6 *)&aconf->ipnum)->sin6_addr))
			{
				memcpy(&((struct sockaddr_in6 *)&aconf->ipnum)->sin6_addr, 
					&((struct sockaddr_in6 *)&client_p->localClient->ip)->sin6_addr, 
					sizeof(struct in6_addr)); 
			} 
		} else
#endif
		{
			if(((struct sockaddr_in *)&aconf->ipnum)->sin_addr.s_addr == INADDR_NONE)
			{
				((struct sockaddr_in *)&aconf->ipnum)->sin_addr.s_addr = 
					((struct sockaddr_in *)&client_p->localClient->ip)->sin_addr.s_addr;
			}

		}
	}
	return 0;
}

/*
 * send_capabilities
 *
 * inputs	- Client pointer to send to
 *		- int flag of capabilities that this server has
 * output	- NONE
 * side effects	- send the CAPAB line to a server  -orabidoo
 *
 */
void
send_capabilities(struct Client *client_p, struct ConfItem *aconf,
		  int cap_can_send, int enc_can_send)
{
	struct Capability *cap;
	char msgbuf[BUFSIZE];
	char *t;
	int tl;

#ifdef HAVE_LIBCRYPTO
	struct EncCapability *epref;
	char *capend;
	int sent_cipher = 0;
#endif

	t = msgbuf;

	for (cap = captab; cap->name; ++cap)
	{
		if(cap->cap & cap_can_send)
		{
			tl = ircsprintf(t, "%s ", cap->name);
			t += tl;
		}
	}

#ifdef HAVE_LIBCRYPTO
	if(enc_can_send)
	{
		capend = t;
		strcpy(t, "ENC:");
		t += 4;

		/* use connect{} specific info if available */
		if(aconf->cipher_preference)
			epref = aconf->cipher_preference;
		else
			epref = ConfigFileEntry.default_cipher_preference;

		if((epref->cap & enc_can_send))
		{
			/* Leave the space -- it is removed later. */
			tl = ircsprintf(t, "%s ", epref->name);
			t += tl;
			sent_cipher = 1;
		}

		if(!sent_cipher)
			t = capend;	/* truncate string before ENC:, below */
	}
#endif

	t--;
	*t = '\0';
	sendto_one(client_p, "CAPAB :%s", msgbuf);
}

/* burst_modes_TS5()
 *
 * input	- client to burst to, channel name, list to burst, mode flag
 * output	-
 * side effects - client is sent a list of +b, or +e, or +I modes
 */
static void
burst_modes_TS5(struct Client *client_p, char *chname, dlink_list *list, char flag)
{
	dlink_node *ptr;
	struct Ban *banptr;
	char mbuf[MODEBUFLEN];
	char pbuf[BUFSIZE];
	int tlen;
	int mlen;
	int cur_len;
	char *mp;
	char *pp;
	int count = 0;

	mlen = ircsprintf(buf, ":%s MODE %s +", me.name, chname);
	cur_len = mlen;

	mp = mbuf;
	pp = pbuf;

	DLINK_FOREACH(ptr, list->head)
	{
		banptr = ptr->data;
		tlen = strlen(banptr->banstr) + 3;

		/* uh oh */
		if(tlen > MODEBUFLEN)
			continue;

		if((count >= MAXMODEPARAMS) || ((cur_len + tlen + 2) > (BUFSIZE - 3)))
		{
			sendto_one(client_p, "%s%s %s", buf, mbuf, pbuf);

			mp = mbuf;
			pp = pbuf;
			cur_len = mlen;
			count = 0;
		}

		*mp++ = flag;
		*mp = '\0';
		pp += ircsprintf(pp, "%s ", banptr->banstr);
		cur_len += tlen;
		count++;
	}

	if(count != 0)
		sendto_one(client_p, "%s%s %s", buf, mbuf, pbuf);
}

/* burst_modes_TS6()
 *
 * input	- client to burst to, channel name, list to burst, mode flag
 * output	-
 * side effects - client is sent a list of +b, +e, or +I modes
 */
static void
burst_modes_TS6(struct Client *client_p, struct Channel *chptr, 
		dlink_list *list, char flag)
{
	dlink_node *ptr;
	struct Ban *banptr;
	char *t;
	int tlen;
	int mlen;
	int cur_len;

	cur_len = mlen = ircsprintf(buf, ":%s BMASK %lu %s %c :",
				    me.id, chptr->channelts,
				    chptr->chname, flag);
	t = buf + mlen;

	DLINK_FOREACH(ptr, list->head)
	{
		banptr = ptr->data;

		tlen = strlen(banptr->banstr) + 1;

		/* uh oh */
		if(cur_len + tlen > BUFSIZE - 3)
		{
			/* the one we're trying to send doesnt fit at all! */
			if(cur_len == mlen)
			{
				s_assert(0);
				continue;
			}

			/* chop off trailing space and send.. */
			*(t-1) = '\0';
			sendto_one(client_p, "%s", buf);
			cur_len = mlen;
			t = buf + mlen;
		}

		ircsprintf(t, "%s ", banptr->banstr);
		t += tlen;
		cur_len += tlen;
	}

	/* cant ever exit the loop above without having modified buf,
	 * chop off trailing space and send.
	 */
	*(t-1) = '\0';
	sendto_one(client_p, "%s", buf);
}

/*
 * burst_TS5
 * 
 * inputs	- client (server) to send nick towards
 * 		- client to send nick for
 * output	- NONE
 * side effects	- NICK message is sent towards given client_p
 */
static void
burst_TS5(struct Client *client_p)
{
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	static char ubuf[12];
	struct Client *target_p;
	struct Channel *chptr;
	struct membership *msptr;
	struct hook_burst_channel hinfo;
	dlink_node *ptr;
	dlink_node *uptr;
	char *t;
	int tlen, mlen;
	int cur_len = 0;

	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = ptr->data;

		if(!IsPerson(target_p))
			continue;

		send_umode(NULL, target_p, 0, SEND_UMODES, ubuf);
		if(!*ubuf)
		{
			ubuf[0] = '+';
			ubuf[1] = '\0';
		}

		sendto_one(client_p, "NICK %s %d %lu %s %s %s %s :%s",
			   target_p->name, target_p->hopcount + 1,
			   (unsigned long) target_p->tsinfo, ubuf,
			   target_p->username, target_p->host,
			   target_p->user->server, target_p->info);

		if(ConfigFileEntry.burst_away && !EmptyString(target_p->user->away))
			sendto_one(client_p, ":%s AWAY :%s",
				   target_p->name, target_p->user->away);
	}

	DLINK_FOREACH(ptr, global_channel_list.head)
	{
		chptr = ptr->data;

		s_assert(dlink_list_length(&chptr->members) > 0);
		if(dlink_list_length(&chptr->members) <= 0)
			continue;

		if(*chptr->chname != '#')
			return;

		hinfo.chptr = chptr;
		hinfo.client = client_p;
		hook_call_event(h_burst_channel_id, &hinfo);

		*modebuf = *parabuf = '\0';
		channel_modes(chptr, client_p, modebuf, parabuf);

		cur_len = mlen = ircsprintf(buf, ":%s SJOIN %lu %s %s %s:", me.name,
				(unsigned long) chptr->channelts,
				chptr->chname, modebuf, parabuf);

		t = buf + mlen;

		DLINK_FOREACH(uptr, chptr->members.head)
		{
			msptr = uptr->data;

			tlen = strlen(msptr->client_p->name) + 1;
			if(is_chanop(msptr))
				tlen++;
			if(is_voiced(msptr))
				tlen++;

			if(cur_len + tlen >= BUFSIZE - 3)
			{
				t--;
				*t = '\0';
				sendto_one(client_p, "%s", buf);
				cur_len = mlen;
				t = buf + mlen;
			}

			ircsprintf(t, "%s%s ", find_channel_status(msptr, 1), 
				   msptr->client_p->name);

			cur_len += tlen;
			t += tlen;
		}

		/* remove trailing space */
		t--;
		*t = '\0';
		sendto_one(client_p, "%s", buf);

		burst_modes_TS5(client_p, chptr->chname, &chptr->banlist, 'b');

		if(IsCapable(client_p, CAP_EX))
			burst_modes_TS5(client_p, chptr->chname, &chptr->exceptlist, 'e');

		if(IsCapable(client_p, CAP_IE))
			burst_modes_TS5(client_p, chptr->chname, &chptr->invexlist, 'I');
	}
}

/*
 * burst_TS6
 * 
 * inputs	- client (server) to send nick towards
 * 		- client to send nick for
 * output	- NONE
 * side effects	- NICK message is sent towards given client_p
 */
static void
burst_TS6(struct Client *client_p)
{
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	static char ubuf[12];
	struct Client *target_p;
	struct Channel *chptr;
	struct membership *msptr;
	struct hook_burst_channel hinfo;
	dlink_node *ptr;
	dlink_node *uptr;
	char *t;
	int tlen, mlen;
	int cur_len = 0;

	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = ptr->data;

		if(!IsPerson(target_p))
			continue;

		send_umode(NULL, target_p, 0, SEND_UMODES, ubuf);
		if(!*ubuf)
		{
			ubuf[0] = '+';
			ubuf[1] = '\0';
		}

		if(has_id(target_p))
			sendto_one(client_p, ":%s UID %s %d %lu %s %s %s %s %s :%s",
				   target_p->servptr->id, target_p->name,
				   target_p->hopcount + 1, 
				   (unsigned long) target_p->tsinfo, ubuf,
				   target_p->username, target_p->host,
				   IsIPSpoof(target_p) ? "0" : target_p->sockhost,
				   target_p->id, target_p->info);
		else
			sendto_one(client_p, "NICK %s %d %lu %s %s %s %s :%s",
					target_p->name,
					target_p->hopcount + 1,
					(unsigned long) target_p->tsinfo,
					ubuf,
					target_p->username, target_p->host,
					target_p->user->server, target_p->info);

		if(ConfigFileEntry.burst_away && !EmptyString(target_p->user->away))
			sendto_one(client_p, ":%s AWAY :%s",
				   use_id(target_p),
				   target_p->user->away);
	}

	DLINK_FOREACH(ptr, global_channel_list.head)
	{
		chptr = ptr->data;

		s_assert(dlink_list_length(&chptr->members) > 0);
		if(dlink_list_length(&chptr->members) <= 0)
			continue;

		if(*chptr->chname != '#')
			return;

		hinfo.chptr = chptr;
		hinfo.client = client_p;
		hook_call_event(h_burst_channel_id, &hinfo);

		*modebuf = *parabuf = '\0';
		channel_modes(chptr, client_p, modebuf, parabuf);

		cur_len = mlen = ircsprintf(buf, ":%s SJOIN %lu %s %s %s:", me.name,
				(unsigned long) chptr->channelts,
				chptr->chname, modebuf, parabuf);

		t = buf + mlen;

		DLINK_FOREACH(uptr, chptr->members.head)
		{
			msptr = uptr->data;

			tlen = strlen(use_id(msptr->client_p)) + 1;
			if(is_chanop(msptr))
				tlen++;
			if(is_voiced(msptr))
				tlen++;

			if(cur_len + tlen >= BUFSIZE - 3)
			{
				*(t-1) = '\0';
				sendto_one(client_p, "%s", buf);
				cur_len = mlen;
				t = buf + mlen;
			}

			ircsprintf(t, "%s%s ", find_channel_status(msptr, 1), 
				   use_id(msptr->client_p));

			cur_len += tlen;
			t += tlen;
		}

		/* remove trailing space */
		*(t-1) = '\0';
		sendto_one(client_p, "%s", buf);

		if(dlink_list_length(&chptr->banlist) > 0)
			burst_modes_TS6(client_p, chptr, &chptr->banlist, 'b');

		if(IsCapable(client_p, CAP_EX) &&
		   dlink_list_length(&chptr->exceptlist) > 0)
			burst_modes_TS6(client_p, chptr, &chptr->exceptlist, 'e');

		if(IsCapable(client_p, CAP_IE) &&
		   dlink_list_length(&chptr->invexlist) > 0)
			burst_modes_TS6(client_p, chptr, &chptr->invexlist, 'I');
	}
}



/*
 * show_capabilities - show current server capabilities
 *
 * inputs       - pointer to an struct Client
 * output       - pointer to static string
 * side effects - build up string representing capabilities of server listed
 */
const char *
show_capabilities(struct Client *target_p)
{
	static char msgbuf[BUFSIZE];
	struct Capability *cap;
	char *t;
	int tl;

	t = msgbuf;
	tl = ircsprintf(msgbuf, "TS ");
	t += tl;

	if(!target_p->localClient->caps)	/* short circuit if no caps */
	{
		msgbuf[2] = '\0';
		return msgbuf;
	}

	for (cap = captab; cap->cap; ++cap)
	{
		if(cap->cap & target_p->localClient->caps)
		{
			tl = ircsprintf(t, "%s ", cap->name);
			t += tl;
		}
	}

#ifdef HAVE_LIBCRYPTO
	if(IsCapable(target_p, CAP_ENC) &&
	   target_p->localClient->in_cipher && target_p->localClient->out_cipher)
	{
		tl = ircsprintf(t, "ENC:%s ", target_p->localClient->in_cipher->name);
		t += tl;
	}
#endif

	t--;
	*t = '\0';

	return msgbuf;
}

/*
 * server_estab
 *
 * inputs       - pointer to a struct Client
 * output       -
 * side effects -
 */

int
server_estab(struct Client *client_p)
{
	struct Client *target_p;
	struct ConfItem *aconf;
	const char *inpath;
	static char inpath_ip[HOSTLEN * 2 + USERLEN + 5];
	char *host;
	dlink_node *ptr;

	s_assert(NULL != client_p);
	if(client_p == NULL)
		return -1;
	ClearAccess(client_p);

	strcpy(inpath_ip, get_client_name(client_p, SHOW_IP));
	inpath = get_client_name(client_p, MASK_IP);	/* "refresh" inpath with host */
	host = client_p->name;

	aconf = client_p->localClient->att_conf;

	if((aconf == NULL) || ((aconf->status & CONF_SERVER) == 0) ||
	   irccmp(aconf->name, client_p->name) || !match(aconf->name, client_p->name))
	{
		/* This shouldn't happen, better tell the ops... -A1kmm */
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Warning: Lost connect{} block for server %s!", host);
		return exit_client(client_p, client_p, client_p, "Lost connect{} block!");
	}

	/* We shouldn't have to check this, it should already done before
	 * server_estab is called. -A1kmm
	 */
	memset((void *) client_p->localClient->passwd, 0, sizeof(client_p->localClient->passwd));

	/* Its got identd , since its a server */
	SetGotId(client_p);

	/* If there is something in the serv_list, it might be this
	 * connecting server..
	 */
	if(!ServerInfo.hub && serv_list.head)
	{
		if(client_p != serv_list.head->data || serv_list.head->next)
		{
			ServerStats->is_ref++;
			sendto_one(client_p, "ERROR :I'm a leaf not a hub");
			return exit_client(client_p, client_p, client_p, "I'm a leaf");
		}
	}

	if(IsUnknown(client_p) && !IsConfCryptLink(aconf))
	{
		int opt = 1;
		/*
		 * jdc -- 1.  Use EmptyString(), not [0] index reference.
		 *        2.  Check aconf->spasswd, not aconf->passwd.
		 */
		if(!EmptyString(aconf->spasswd))
		{
			sendto_one(client_p, "PASS %s TS :%d", 
				   aconf->spasswd, TS_CURRENT);
		}

		/*
		 * Pass my info to the new server
		 *
		 */

		send_capabilities(client_p, aconf, default_server_capabs
				  | ((aconf->flags & CONF_FLAGS_COMPRESSED) ?
				     CAP_ZIP_SUPPORTED : 0), 0);

		/* Turn on Nagle so we shove this packet out immediately otherwise we'll confuse the 
		 * remote with compressed and uncompressed data arriving all at once
		 */
		setsockopt(client_p->localClient->fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
		
		sendto_one(client_p, "SERVER %s 1 :%s%s",
			   my_name_for_link(me.name, aconf),
			   ConfigServerHide.hidden ? "(H) " : "",
			   (me.info[0]) ? (me.info) : "IRCers United");
		opt = 0;
		setsockopt(client_p->localClient->fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
	}

	/*
	 * XXX - this should be in s_bsd
	 */
	if(!set_sock_buffers(client_p->localClient->fd, READBUF_SIZE))
		report_error(L_ALL, SETBUF_ERROR_MSG, get_client_name(client_p, SHOW_IP), errno);

	/* Hand the server off to servlink now */

#ifndef __VMS
	if(IsCapable(client_p, CAP_ENC) || IsCapable(client_p, CAP_ZIP))
	{
		if(fork_server(client_p) < 0)
		{
			sendto_realops_flags(UMODE_ALL, L_ADMIN,
					     "Warning: fork failed for server %s -- check servlink_path (%s)",
					     get_client_name(client_p,
							     HIDE_IP),
					     ConfigFileEntry.servlink_path);
			sendto_realops_flags(UMODE_ALL, L_OPER,
					     "Warning: fork failed for server "
					     "%s -- check servlink_path (%s)",
					     get_client_name(client_p,
							     MASK_IP),
					     ConfigFileEntry.servlink_path);
			return exit_client(client_p, client_p, client_p, "Fork failed");
		}
		start_io(client_p);
		SetServlink(client_p);
	}
#endif

	sendto_one(client_p, "SVINFO %d %d %s :%lu",
		   TS_CURRENT, TS_MIN, me.id, (unsigned long) CurrentTime);

	client_p->servptr = &me;

	if(IsDeadorAborted(client_p))
		return CLIENT_EXITED;

	SetServer(client_p);

	/* Update the capability combination usage counts */
	set_chcap_usage_counts(client_p);

	dlinkAdd(client_p, &client_p->lnode, &me.serv->servers);
	dlinkMoveNode(&client_p->localClient->tnode, &unknown_list, &serv_list);
	dlinkAddAlloc(client_p, &global_serv_list);

	add_to_client_hash(client_p->name, client_p);
	/* doesnt duplicate client_p->serv if allocated this struct already */
	make_server(client_p);
	client_p->serv->up = me.name;
	/* add it to scache */
	find_or_add(client_p->name);
	client_p->firsttime = CurrentTime;
	/* fixing eob timings.. -gnp */

	/* Show the real host/IP to admins */
	sendto_realops_flags(UMODE_ALL, L_ADMIN,
			     "Link with %s established: (%s) link",
			     inpath_ip, show_capabilities(client_p));

	/* Now show the masked hostname/IP to opers */
	sendto_realops_flags(UMODE_ALL, L_OPER,
			     "Link with %s established: (%s) link",
			     inpath, show_capabilities(client_p));

	ilog(L_NOTICE, "Link with %s established: (%s) link",
	     log_client_name(client_p, SHOW_IP), show_capabilities(client_p));

	client_p->serv->sconf = aconf;

	if(HasServlink(client_p))
	{
		/* we won't overflow FD_DESC_SZ here, as it can hold
		 * client_p->name + 64
		 */
#ifdef HAVE_SOCKETPAIR
		fd_note(client_p->localClient->fd, "slink data: %s", client_p->name);
		fd_note(client_p->localClient->ctrlfd, "slink ctrl: %s", client_p->name);
#else
		fd_note(client_p->localClient->fd, "slink data (out): %s", client_p->name);
		fd_note(client_p->localClient->ctrlfd, "slink ctrl (out): %s", client_p->name);
		fd_note(client_p->localClient->fd_r, "slink data  (in): %s", client_p->name);
		fd_note(client_p->localClient->ctrlfd_r, "slink ctrl  (in): %s", client_p->name);
#endif
	}
	else
		fd_note(client_p->localClient->fd, "Server: %s", client_p->name);

	/*
	 ** Old sendto_serv_but_one() call removed because we now
	 ** need to send different names to different servers
	 ** (domain name matching) Send new server to other servers.
	 */
	DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = ptr->data;

		if(target_p == client_p)
			continue;

		if((aconf = target_p->serv->sconf) &&
		   match(my_name_for_link(me.name, aconf), client_p->name))
			continue;
		sendto_one(target_p, ":%s SERVER %s 2 :%s%s",
			   me.name, client_p->name,
			   IsHidden(client_p) ? "(H) " : "", client_p->info);
	}

	/*
	 ** Pass on my client information to the new server
	 **
	 ** First, pass only servers (idea is that if the link gets
	 ** cancelled beacause the server was already there,
	 ** there are no NICK's to be cancelled...). Of course,
	 ** if cancellation occurs, all this info is sent anyway,
	 ** and I guess the link dies when a read is attempted...? --msa
	 ** 
	 ** Note: Link cancellation to occur at this point means
	 ** that at least two servers from my fragment are building
	 ** up connection this other fragment at the same time, it's
	 ** a race condition, not the normal way of operation...
	 **
	 ** ALSO NOTE: using the get_client_name for server names--
	 **    see previous *WARNING*!!! (Also, original inpath
	 **    is destroyed...)
	 */

	aconf = client_p->serv->sconf;
	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = ptr->data;
		/* target_p->from == target_p for target_p == client_p */
		if(target_p->from == client_p)
			continue;
		if(IsServer(target_p))
		{
			if(match(my_name_for_link(me.name, aconf), target_p->name))
				continue;
			sendto_one(client_p, ":%s SERVER %s %d :%s%s",
				   target_p->serv->up,
				   target_p->name, target_p->hopcount + 1,
				   IsHidden(target_p) ? "(H) " : "", target_p->info);
		}
	}

	if(DoesTS6(client_p))
		burst_TS6(client_p);
	else
		burst_TS5(client_p);
		
	if(IsCapable(client_p, CAP_EOB))
		sendto_one(client_p, ":%s EOB", me.name);

	/* Always send a PING after connect burst is done */
	sendto_one(client_p, "PING :%s", me.name);

	return 0;
}

static void
start_io(struct Client *server)
{
	unsigned char *iobuf;
	int c = 0;
	int linecount = 0;
	int linelen;

	iobuf = MyMalloc(256);

	if(IsCapable(server, CAP_ZIP))
	{
		/* ziplink */
		iobuf[c++] = SLINKCMD_SET_ZIP_OUT_LEVEL;
		iobuf[c++] = 0;	/* |          */
		iobuf[c++] = 1;	/* \ len is 1 */
		iobuf[c++] = ConfigFileEntry.compression_level;
		iobuf[c++] = SLINKCMD_START_ZIP_IN;
		iobuf[c++] = SLINKCMD_START_ZIP_OUT;
	}
#ifdef HAVE_LIBCRYPTO
	if(IsCapable(server, CAP_ENC))
	{
		/* Decryption settings */
		iobuf[c++] = SLINKCMD_SET_CRYPT_IN_CIPHER;
		iobuf[c++] = 0;	/* /                     (upper 8-bits of len) */
		iobuf[c++] = 1;	/* \ cipher id is 1 byte (lower 8-bits of len) */
		iobuf[c++] = server->localClient->in_cipher->cipherid;
		iobuf[c++] = SLINKCMD_SET_CRYPT_IN_KEY;
		iobuf[c++] = 0;	/* keylen < 256 */
		iobuf[c++] = server->localClient->in_cipher->keylen;
		memcpy((iobuf + c), server->localClient->in_key,
		       server->localClient->in_cipher->keylen);
		c += server->localClient->in_cipher->keylen;
		/* Encryption settings */
		iobuf[c++] = SLINKCMD_SET_CRYPT_OUT_CIPHER;
		iobuf[c++] = 0;	/* /                     (upper 8-bits of len) */
		iobuf[c++] = 1;	/* \ cipher id is 1 byte (lower 8-bits of len) */
		iobuf[c++] = server->localClient->out_cipher->cipherid;
		iobuf[c++] = SLINKCMD_SET_CRYPT_OUT_KEY;
		iobuf[c++] = 0;	/* keylen < 256 */
		iobuf[c++] = server->localClient->out_cipher->keylen;
		memcpy((iobuf + c), server->localClient->out_key,
		       server->localClient->out_cipher->keylen);
		c += server->localClient->out_cipher->keylen;
		iobuf[c++] = SLINKCMD_START_CRYPT_IN;
		iobuf[c++] = SLINKCMD_START_CRYPT_OUT;
	}
#endif

	while (MyConnect(server))
	{
		linecount++;

		iobuf = MyRealloc(buf, (c + READBUF_SIZE + 64));

		/* store data in c+3 to allow for SLINKCMD_INJECT_RECVQ and len u16 */
		linelen = linebuf_get(&server->localClient->buf_recvq, (char *) (iobuf + c + 3), READBUF_SIZE, LINEBUF_PARTIAL, LINEBUF_RAW);	/* include partial lines & don't
																		   parse data */

		if(linelen)
		{
			iobuf[c++] = SLINKCMD_INJECT_RECVQ;
			iobuf[c++] = (linelen >> 8);
			iobuf[c++] = (linelen & 0xff);
			c += linelen;
		}
		else
			break;
	}

	while (MyConnect(server))
	{
		linecount++;

		iobuf = MyRealloc(buf, (c + BUF_DATA_SIZE + 64));

		/* store data in c+3 to allow for SLINKCMD_INJECT_RECVQ and len u16 */
		linelen = linebuf_get(&server->localClient->buf_sendq, 
				      (char *) (iobuf + c + 3), READBUF_SIZE, 
				      LINEBUF_PARTIAL, LINEBUF_PARSED);	/* include partial lines */

		if(linelen)
		{
			iobuf[c++] = SLINKCMD_INJECT_SENDQ;
			iobuf[c++] = (linelen >> 8);
			iobuf[c++] = (linelen & 0xff);
			c += linelen;
		}
		else
			break;
	}

	/* start io */
	iobuf[c++] = SLINKCMD_INIT;

	server->localClient->slinkq = iobuf;
	server->localClient->slinkq_ofs = 0;
	server->localClient->slinkq_len = c;

	/* schedule a write */
	send_queued_slink_write(server->localClient->ctrlfd, server);
}

#ifndef __VMS
/*
 * fork_server
 *
 * inputs       - struct Client *server
 * output       - success: 0 / failure: -1
 * side effect  - fork, and exec SERVLINK to handle this connection
 */
static int
fork_server(struct Client *server)
{
	int ret;
	int i;
	int fd_temp[2];
	int slink_fds[2][2][2] = { {{0, 0}, {0, 0}},
	{{0, 0}, {0, 0}}
	};
	/* [0][y][z] - ctrl  | [1][y][z] - data  
	 * [x][0][z] - child | [x][1][z] - parent
	 * [x][y][0] - read  | [x][y][1] - write
	 */
	char fd_str[5][6];	/* store 5x '6' '5' '5' '3' '5' '\0' */
	char *kid_argv[7];
	char slink[] = "-slink";
#ifdef HAVE_SOCKETPAIR
	/* ctrl */
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd_temp) < 0)
		goto fork_error;

	slink_fds[0][0][0] = fd_temp[0];
	slink_fds[0][1][1] = fd_temp[1];
	slink_fds[0][0][1] = fd_temp[0];
	slink_fds[0][1][0] = fd_temp[1];

	/* data */
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd_temp) < 0)
		goto fork_error;

	slink_fds[1][0][0] = fd_temp[0];
	slink_fds[1][1][1] = fd_temp[1];
	slink_fds[1][0][1] = fd_temp[0];
	slink_fds[1][1][0] = fd_temp[1];
#else
	/* ctrl parent -> child */
	if(pipe(fd_temp) < 0)
		goto fork_error;
	slink_fds[0][0][0] = fd_temp[0];
	slink_fds[0][1][1] = fd_temp[1];

	/* ctrl child -> parent */
	if(pipe(fd_temp) < 0)
		goto fork_error;
	slink_fds[0][1][0] = fd_temp[0];
	slink_fds[0][0][1] = fd_temp[1];

	/* data parent -> child */
	if(pipe(fd_temp) < 0)
		goto fork_error;
	slink_fds[1][0][0] = fd_temp[0];
	slink_fds[1][1][1] = fd_temp[1];

	/* data child -> parent */
	if(pipe(fd_temp) < 0)
		goto fork_error;
	slink_fds[1][1][0] = fd_temp[0];
	slink_fds[1][0][1] = fd_temp[1];
#endif
#ifdef __CYGWIN__
	if((ret = vfork()) < 0)
#else
	if((ret = fork()) < 0)
#endif
		goto fork_error;
	else if(ret == 0)
	{
		/* set our fds as non blocking and close everything else */
		for (i = 0; i < HARD_FDLIMIT; i++)
		{

			if((i == slink_fds[0][0][0])
			   || (i == slink_fds[0][0][1])
			   || (i == slink_fds[0][0][0])
			   || (i == slink_fds[1][0][1]) || (i == server->localClient->fd))
			{
				set_non_blocking(i);
#ifdef USE_SIGIO		/* the servlink process doesn't need O_ASYNC */
				{
					int flags;
					flags = fcntl(i, F_GETFL, 0);
					flags |= ~O_ASYNC;
					fcntl(i, F_SETFL, flags);
				}
#endif
			}
			else
			{
#if defined(__VMS) || defined(__CYGWIN__)
				if(i > 2)	/* don't close std* */
#endif
					close(i);
			}
		}

		sprintf(fd_str[0], "%d", slink_fds[0][0][0]);	/* ctrl read */
		sprintf(fd_str[1], "%d", slink_fds[0][0][1]);	/* ctrl write */
		sprintf(fd_str[2], "%d", slink_fds[1][0][0]);	/* data read */
		sprintf(fd_str[3], "%d", slink_fds[1][0][1]);	/* data write */
		sprintf(fd_str[4], "%d", server->localClient->fd);	/* network read/write */

		kid_argv[0] = slink;
		kid_argv[1] = fd_str[0];
		kid_argv[2] = fd_str[1];
		kid_argv[3] = fd_str[2];
		kid_argv[4] = fd_str[3];
		kid_argv[5] = fd_str[4];
		kid_argv[6] = NULL;

		/* exec servlink program */
		execv(ConfigFileEntry.servlink_path, kid_argv);

		/* We're still here, abort. */
		_exit(1);
	}
	else
	{
		fd_close(server->localClient->fd);

		/* close the childs end of the pipes */
		close(slink_fds[0][0][0]);
		close(slink_fds[0][0][1]);
		close(slink_fds[1][0][0]);
		close(slink_fds[1][0][1]);

		s_assert(server->localClient);
		server->localClient->ctrlfd = slink_fds[0][1][1];
		server->localClient->fd = slink_fds[1][1][1];

#ifndef HAVE_SOCKETPAIR
		server->localClient->ctrlfd_r = slink_fds[0][1][0];
		server->localClient->fd_r = slink_fds[1][1][0];
#endif

		if(!set_non_blocking(server->localClient->fd))
		{
			report_error(L_ADMIN, NONB_ERROR_MSG,
#ifdef HIDE_SERVERS_IPS
				     get_client_name(server, MASK_IP),
#else
				     get_client_name(server, SHOW_IP),
#endif
				     errno);
			report_error(L_OPER, NONB_ERROR_MSG,
				     get_client_name(server, MASK_IP), errno);
		}

		if(!set_non_blocking(server->localClient->ctrlfd))
		{
			report_error(L_ADMIN, NONB_ERROR_MSG,
#ifdef HIDE_SERVERS_IPS
				     get_client_name(server, MASK_IP),
#else
				     get_client_name(server, SHOW_IP),
#endif
				     errno);
			report_error(L_OPER, NONB_ERROR_MSG,
				     get_client_name(server, MASK_IP), errno);
		}

#ifndef HAVE_SOCKETPAIR
		if(!set_non_blocking(server->localClient->fd_r))
		{
			report_error(L_ADMIN, NONB_ERROR_MSG,
#ifdef HIDE_SERVERS_IPS
				     get_client_name(server, MASK_IP),
#else
				     get_client_name(server, SHOW_IP),
#endif
				     errno);
			report_error(L_OPER, NONB_ERROR_MSG,
				     get_client_name(server, MASK_IP), errno);
		}
		if(!set_non_blocking(server->localClient->ctrlfd_r))
		{
			report_error(L_ADMIN, NONB_ERROR_MSG,
#ifdef HIDE_SERVERS_IPS
				     get_client_name(server, MASK_IP),
#else
				     get_client_name(server, SHOW_IP),
#endif
				     errno);
			report_error(L_OPER, NONB_ERROR_MSG,
				     get_client_name(server, MASK_IP), errno);
		}
#endif

#ifdef HAVE_SOCKETPAIR
		fd_open(server->localClient->ctrlfd, FD_SOCKET, NULL);
		fd_open(server->localClient->fd, FD_SOCKET, NULL);
#else
		fd_open(server->localClient->ctrlfd, FD_PIPE, NULL);
		fd_open(server->localClient->fd, FD_PIPE, NULL);
		fd_open(server->localClient->ctrlfd_r, FD_PIPE, NULL);
		fd_open(server->localClient->fd_r, FD_PIPE, NULL);
#endif

		read_ctrl_packet(slink_fds[0][1][0], server);
		read_packet(slink_fds[1][1][0], server);
	}

	return 0;

      fork_error:
	/* this is ugly, but nicer than repeating
	 * about 50 close() statements everywhre... */
	close(slink_fds[0][0][0]);
	close(slink_fds[0][0][1]);
	close(slink_fds[0][1][0]);
	close(slink_fds[0][1][1]);
	close(slink_fds[1][0][0]);
	close(slink_fds[1][0][1]);
	close(slink_fds[1][1][0]);
	close(slink_fds[1][1][1]);
	return -1;
}
#endif

/*
 * set_autoconn - set autoconnect mode
 *
 * inputs       - struct Client pointer to oper requesting change
 *              -
 * output       - none
 * side effects -
 */
void
set_autoconn(struct Client *source_p, char *parv0, char *name, int newval)
{
	struct ConfItem *aconf;

	if(name && (aconf = find_conf_by_name(name, CONF_SERVER)))
	{
		if(newval)
			aconf->flags |= CONF_FLAGS_ALLOW_AUTO_CONN;
		else
			aconf->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN;

		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s has changed AUTOCONN for %s to %i", parv0, name, newval);
		sendto_one(source_p,
			   ":%s NOTICE %s :AUTOCONN for %s is now set to %i",
			   me.name, parv0, name, newval);
	}
	else if(name)
	{
		sendto_one(source_p, ":%s NOTICE %s :Can't find %s", me.name, parv0, name);
	}
	else
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :Please specify a server name!", me.name, parv0);
	}
}


void
initServerMask(void)
{
	freeMask = 0xFFFFFFFFUL;
}

/*
 * nextFreeMask
 *
 * inputs	- NONE
 * output	- unsigned long next unused mask for use in LL
 * side effects	-
 */
unsigned long
nextFreeMask()
{
	int i;
	unsigned long mask;

	mask = 1;

	for (i = 0; i < 32; i++)
	{
		if(mask & freeMask)
		{
			freeMask &= ~mask;
			return (mask);
		}
		mask <<= 1;
	}
	return 0L;		/* watch this special case ... */
}

/*
 * New server connection code
 * Based upon the stuff floating about in s_bsd.c
 *   -- adrian
 */

/*
 * serv_connect() - initiate a server connection
 *
 * inputs	- pointer to conf 
 *		- pointer to client doing the connet
 * output	-
 * side effects	-
 *
 * This code initiates a connection to a server. It first checks to make
 * sure the given server exists. If this is the case, it creates a socket,
 * creates a client, saves the socket information in the client, and
 * initiates a connection to the server through comm_connect_tcp(). The
 * completion of this goes through serv_completed_connection().
 *
 * We return 1 if the connection is attempted, since we don't know whether
 * it suceeded or not, and 0 if it fails in here somewhere.
 */
int
serv_connect(struct ConfItem *aconf, struct Client *by)
{
	struct Client *client_p;
	struct sockaddr_storage myipnum; 
	int fd;
	/* Make sure aconf is useful */
	s_assert(aconf != NULL);
	if(aconf == NULL)
		return 0;

	/* log */
	inetntop_sock(&aconf->ipnum, buf, sizeof(buf));
	ilog(L_NOTICE, "Connect to %s[%s] @%s", aconf->user, aconf->host, buf);

	/*
	 * Make sure this server isn't already connected
	 * Note: aconf should ALWAYS be a valid C: line
	 */
	if((client_p = find_server(aconf->name)))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Server %s already present from %s",
				     aconf->name, get_client_name(client_p, SHOW_IP));
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Server %s already present from %s",
				     aconf->name, get_client_name(client_p, MASK_IP));
		if(by && IsPerson(by) && !MyClient(by))
			sendto_one(by,
				   ":%s NOTICE %s :Server %s already present from %s",
				   me.name, by->name, aconf->name,
				   get_client_name(client_p, MASK_IP));
		return 0;
	}

	/* create a socket for the server connection */
	if((fd = comm_open(aconf->ipnum.ss_family, SOCK_STREAM, 0, NULL)) < 0)
	{
		/* Eek, failure to create the socket */
		report_error(L_ALL, "opening stream socket to %s: %s", aconf->name, errno);
		return 0;
	}

	/* servernames are always guaranteed under HOSTLEN chars */
	fd_note(fd, "Server: %s", aconf->name);

	/* Create a local client */
	client_p = make_client(NULL);

	/* Copy in the server, hostname, fd */
	strlcpy(client_p->name, aconf->name, sizeof(client_p->name));
	strlcpy(client_p->host, aconf->host, sizeof(client_p->host));
	strlcpy(client_p->sockhost, buf, sizeof(client_p->sockhost));
	client_p->localClient->fd = fd;

	/*
	 * Set up the initial server evilness, ripped straight from
	 * connect_server(), so don't blame me for it being evil.
	 *   -- adrian
	 */

	if(!set_non_blocking(client_p->localClient->fd))
	{
		report_error(L_ADMIN, NONB_ERROR_MSG,
#ifdef HIDE_SERVERS_IPS
			     get_client_name(client_p, MASK_IP),
#else
			     get_client_name(client_p, SHOW_IP),
#endif
			     errno);
		report_error(L_OPER, NONB_ERROR_MSG, get_client_name(client_p, MASK_IP), errno);
	}

	if(!set_sock_buffers(client_p->localClient->fd, READBUF_SIZE))
	{
		report_error(L_ADMIN, SETBUF_ERROR_MSG,
#ifdef HIDE_SERVERS_IPS
			     get_client_name(client_p, MASK_IP),
#else
			     get_client_name(client_p, SHOW_IP),
#endif
			     errno);
		report_error(L_OPER, SETBUF_ERROR_MSG, get_client_name(client_p, MASK_IP), errno);
	}

	/*
	 * Attach config entries to client here rather than in
	 * serv_connect_callback(). This to avoid null pointer references.
	 */
	if(!attach_connect_block(client_p, aconf->name, aconf->host))
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Host %s is not enabled for connecting:no C/N-line",
				     aconf->name);
		if(by && IsPerson(by) && !MyClient(by))
			sendto_one(by,
				   ":%s NOTICE %s :Connect to host %s failed.",
				   me.name, by->name, client_p->name);
		detach_conf(client_p);
		free_client(client_p);
		return 0;
	}
	/*
	 * at this point we have a connection in progress and C/N lines
	 * attached to the client, the socket info should be saved in the
	 * client and it should either be resolved or have a valid address.
	 *
	 * The socket has been connected or connect is in progress.
	 */
	make_server(client_p);
	if(by && IsPerson(by))
	{
		strcpy(client_p->serv->by, by->name);
		if(client_p->serv->user)
			free_user(client_p->serv->user, NULL);
		client_p->serv->user = by->user;
		by->user->refcnt++;
	}
	else
	{
		strcpy(client_p->serv->by, "AutoConn.");
		if(client_p->serv->user)
			free_user(client_p->serv->user, NULL);
		client_p->serv->user = NULL;
	}
	client_p->serv->up = me.name;
	SetConnecting(client_p);
	dlinkAddTail(client_p, &client_p->node, &global_client_list);


	if(IsConfVhosted(aconf))
	{
		memcpy(&myipnum, &aconf->my_ipnum, sizeof(myipnum));
		((struct sockaddr_in *)&myipnum)->sin_port = 0;
		myipnum.ss_family = aconf->my_ipnum.ss_family;
				
	}
	else if(aconf->aftype == AF_INET && ServerInfo.specific_ipv4_vhost)
	{
		memcpy(&myipnum, &ServerInfo.ip, sizeof(myipnum));
		((struct sockaddr_in *)&myipnum)->sin_port = 0;
		myipnum.ss_family = aconf->my_ipnum.ss_family;
	}
	
#ifdef IPV6
	else if((aconf->aftype == AF_INET6) && ServerInfo.specific_ipv6_vhost)
	{
		memcpy(&myipnum, &ServerInfo.ip6, sizeof(myipnum));
		((struct sockaddr_in6 *)&myipnum)->sin6_port = 0;
		myipnum.ss_family = AF_INET6;
	}
#endif
	else
	{
		comm_connect_tcp(client_p->localClient->fd, aconf->host,
				 aconf->port, NULL, 0, serv_connect_callback, 
				 client_p, aconf->aftype, 
				 ConfigFileEntry.connect_timeout);
		 return 1;
	}

	comm_connect_tcp(client_p->localClient->fd, aconf->host,
			 aconf->port, (struct sockaddr *) &myipnum,
			 GET_SS_LEN(myipnum), serv_connect_callback, client_p,
			 myipnum.ss_family, ConfigFileEntry.connect_timeout);

	return 1;
}

/*
 * serv_connect_callback() - complete a server connection.
 * 
 * This routine is called after the server connection attempt has
 * completed. If unsucessful, an error is sent to ops and the client
 * is closed. If sucessful, it goes through the initialisation/check
 * procedures, the capabilities are sent, and the socket is then
 * marked for reading.
 */
static void
serv_connect_callback(int fd, int status, void *data)
{
	struct Client *client_p = data;
	struct ConfItem *aconf;

	/* First, make sure its a real client! */
	s_assert(client_p != NULL);
	s_assert(client_p->localClient->fd == fd);

	if(client_p == NULL)
		return;

	/* Next, for backward purposes, record the ip of the server */
#ifdef IPV6
	if(fd_table[fd].connect.hostaddr.ss_family == AF_INET6)
	{
		struct sockaddr_in6 *lip = (struct sockaddr_in6 *)&client_p->localClient->ip;
		struct sockaddr_in6 *hip = (struct sockaddr_in6 *)&fd_table[fd].connect.hostaddr;	
		memcpy(&lip->sin6_addr, &hip->sin6_addr, sizeof(struct in6_addr));
	} else
#else
	{
		struct sockaddr_in *lip = (struct sockaddr_in *)&client_p->localClient->ip;
		struct sockaddr_in *hip = (struct sockaddr_in *)&fd_table[fd].connect.hostaddr;	
		lip->sin_addr.s_addr = hip->sin_addr.s_addr;
	}	
#endif	
	
	/* Check the status */
	if(status != COMM_OK)
	{
		/* We have an error, so report it and quit */
		/* Admins get to see any IP, mere opers don't *sigh*
		 */
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "Error connecting to %s[%s]: %s (%s)",
				     client_p->name, client_p->host,
				     comm_errstr(status), strerror(errno));
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Error connecting to %s: %s (%s)",
				     client_p->name, comm_errstr(status), strerror(errno));
		exit_client(client_p, client_p, &me, comm_errstr(status));
		return;
	}

	/* COMM_OK, so continue the connection procedure */
	/* Get the C/N lines */
	aconf = client_p->localClient->att_conf;

	if((aconf == NULL) || ((aconf->status & CONF_SERVER) == 0) ||
	   irccmp(aconf->name, client_p->name) || !match(aconf->name, client_p->name))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN, "Lost connect{} block for %s",
#ifdef HIDE_SERVERS_IPS
				     get_client_name(client_p, MASK_IP));
#else
				     get_client_name(client_p, HIDE_IP));
#endif
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "Lost connect{} block for %s",
				     get_client_name(client_p, MASK_IP));
		exit_client(client_p, client_p, &me, "Lost connect{} block");
		return;
	}

	/* Next, send the initial handshake */
	SetHandshake(client_p);

#ifdef HAVE_LIBCRYPTO
	/* Handle all CRYPTLINK links in cryptlink_init */
	if(IsConfCryptLink(aconf))
	{
		cryptlink_init(client_p, aconf, fd);
		return;
	}
#endif

	/*
	 * jdc -- Check and send spasswd, not passwd.
	 */
	if(!EmptyString(aconf->spasswd))
	{
		sendto_one(client_p, "PASS %s :TS", aconf->spasswd);
	}

	/*
	 * Pass my info to the new server
	 *
	 */

	send_capabilities(client_p, aconf, default_server_capabs
			  | ((aconf->flags & CONF_FLAGS_COMPRESSED) ? CAP_ZIP_SUPPORTED : 0), 0);

	sendto_one(client_p, "SERVER %s 1 :%s%s",
		   my_name_for_link(me.name, aconf),
		   ConfigServerHide.hidden ? "(H) " : "", me.info);

	/* 
	 * If we've been marked dead because a send failed, just exit
	 * here now and save everyone the trouble of us ever existing.
	 */
	if(IsDeadorAborted(client_p))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "%s[%s] went dead during handshake",
				     client_p->name, client_p->host);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "%s went dead during handshake", client_p->name);
		exit_client(client_p, client_p, &me, "Went dead during handshake");
		return;
	}

	/* don't move to serv_list yet -- we haven't sent a burst! */

	/* If we get here, we're ok, so lets start reading some data */
	comm_setselect(fd, FDLIST_SERVER, COMM_SELECT_READ, read_packet, client_p, 0);
}

#ifdef HAVE_LIBCRYPTO
/*
 * sends a CRYPTLINK SERV command.
 */
void
cryptlink_init(struct Client *client_p, struct ConfItem *aconf, int fd)
{
	char *encrypted;
	char *key_to_send;
	char randkey[CIPHERKEYLEN];
	int enc_len;

	/* get key */
	if((!ServerInfo.rsa_private_key) || (!RSA_check_key(ServerInfo.rsa_private_key)))
	{
		cryptlink_error(client_p, "SERV", "Invalid RSA private key",
				"Invalid RSA private key");
		return;
	}

	if(!aconf->rsa_public_key)
	{
		cryptlink_error(client_p, "SERV", "Invalid RSA public key",
				"Invalid RSA public key");
		return;
	}

	if(get_randomness((unsigned char *) randkey, CIPHERKEYLEN) != 1)
	{
		cryptlink_error(client_p, "SERV",
				"Couldn't generate keyphrase", "Couldn't generate keyphrase");
		return;
	}

	encrypted = MyMalloc(RSA_size(ServerInfo.rsa_private_key));

	enc_len = RSA_public_encrypt(CIPHERKEYLEN,
				     (unsigned char *) randkey,
				     (unsigned char *) encrypted,
				     aconf->rsa_public_key, RSA_PKCS1_PADDING);

	memcpy(client_p->localClient->in_key, randkey, CIPHERKEYLEN);

	if(enc_len <= 0)
	{
		report_crypto_errors();
		MyFree(encrypted);
		cryptlink_error(client_p, "SERV", "Couldn't encrypt data", "Couldn't encrypt data");
		return;
	}

	if(!(base64_block(&key_to_send, encrypted, enc_len)))
	{
		MyFree(encrypted);
		cryptlink_error(client_p, "SERV",
				"Couldn't base64 encode key", "Couldn't base64 encode key");
		return;
	}


	send_capabilities(client_p, aconf, default_server_capabs
			  | ((aconf->flags & CONF_FLAGS_COMPRESSED) ?
			     CAP_ZIP_SUPPORTED : 0), CAP_ENC_MASK);

	sendto_one(client_p, "CRYPTLINK SERV %s %s :%s%s",
		   my_name_for_link(me.name, aconf), key_to_send,
		   ConfigServerHide.hidden ? "(H) " : "", me.info);

	SetHandshake(client_p);
	SetWaitAuth(client_p);

	MyFree(encrypted);
	MyFree(key_to_send);

	/*
	 * If we've been marked dead because a send failed, just exit
	 * here now and save everyone the trouble of us ever existing.
	 */
	if(IsDeadorAborted(client_p))
	{
		cryptlink_error(client_p, "SERV",
				"Went dead during handshake", "Went dead during handshake");
		return;
	}

	/* If we get here, we're ok, so lets start reading some data */
	if(fd > -1)
	{
		comm_setselect(fd, FDLIST_SERVER, COMM_SELECT_READ, read_packet, client_p, 0);
	}
}

void
cryptlink_error(struct Client *client_p, const char *type,
		const char *reason, const char *client_reason)
{
	sendto_realops_flags(UMODE_ALL, L_ADMIN, "%s: CRYPTLINK %s error - %s",
#ifdef HIDE_SERVERS_IPS
			     get_client_name(client_p, MASK_IP),
#else
			     get_client_name(client_p, SHOW_IP),
#endif
			     type, reason);
	sendto_realops_flags(UMODE_ALL, L_OPER,
			     "%s: CRYPTLINK %s error - %s",
			     get_client_name(client_p, MASK_IP), type, reason);
	ilog(L_ERROR, "%s: CRYPTLINK %s error - %s",
	     log_client_name(client_p, SHOW_IP), type, reason);
	/*
	 * If client_reason isn't NULL, then exit the client with the message
	 * defined in the call.
	 */
	if(client_reason != NULL)
	{
		exit_client(client_p, client_p, &me, client_reason);
	}
	return;
}

#endif /* HAVE_LIBCRYPTO */
