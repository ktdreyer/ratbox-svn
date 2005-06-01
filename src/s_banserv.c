/* src/s_banserv.c
 *   Contains the code for the ban (kline etc) service
 *
 * Copyright (C) 2005 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2005 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $Id$
 */
#include "stdinc.h"

#ifdef ENABLE_BANSERV
#include "service.h"
#include "client.h"
#include "io.h"
#include "rserv.h"
#include "c_init.h"
#include "conf.h"
#include "ucommand.h"
#include "log.h"

static struct client *banserv_p;

static int o_banserv_kline(struct client *, struct lconn *, const char **, int);
static int o_banserv_xline(struct client *, struct lconn *, const char **, int);
static int o_banserv_resv(struct client *, struct lconn *, const char **, int);

static struct service_command banserv_command[] =
{
	{ "KLINE", &o_banserv_kline, 2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_TEMP, 0 },
	{ "XLINE", &o_banserv_xline, 2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_TEMP, 0 },
	{ "RESV",  &o_banserv_resv,  2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_TEMP, 0 }
};

static struct ucommand_handler banserv_ucommand[] =
{
	{ "kline",	o_banserv_kline,	0, CONF_OPER_BAN_TEMP, 2, 1, NULL },
	{ "xline",	o_banserv_xline,	0, CONF_OPER_BAN_TEMP, 2, 1, NULL },
	{ "resv",	o_banserv_resv,		0, CONF_OPER_BAN_TEMP, 2, 1, NULL },
	{ "\0", NULL, 0, 0, 0, 0, NULL }
};

static struct service_handler banserv_service = {
	"BANSERV", "BANSERV", "banserv", "services.int",
	"Global Ban Service", 60, 80, 
	banserv_command, sizeof(banserv_command), banserv_ucommand, NULL
};

void
init_s_banserv(void)
{
	banserv_p = add_service(&banserv_service);

	/* banserv has to be opered otherwise it
	 * wont work. --anfl
	 */
	banserv_p->service->flags |= SERVICE_OPERED;
}

static time_t
get_temp_time(const char *duration)
{
	time_t result = 0;

	for(; *duration; duration++)
	{
		if(IsDigit(*duration))
		{
			result *= 10;
			result += ((*duration) & 0xF);
		}
		else
			return 0;
	}

	/* max at 1 year */
	if(result > (60*24*7*52))
		result = (60*24*7*52);

	return(result*60);
}
			
static int
o_banserv_kline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static const char wild[] = "*";
	const char *user, *host, *reason;
	char *mask, *p;
	time_t temptime = 0;
	int para = 0;

	if((temptime = get_temp_time(parv[para])))
		para++;

	if(!temptime)
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->flags & CONF_OPER_BAN_PERM))
				hit++;
		}
		else if(!conn_p->sprivs & CONF_OPER_BAN_PERM)
			hit++;

		if(hit)
		{
			service_send(banserv_p, client_p, conn_p,
					"No access to set permanent klines");
			return 0;
		}
	}

	mask = LOCAL_COPY(parv[para]);
	para++;

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_send(banserv_p, client_p, conn_p,
				"Insufficient parameters to %s::KLINE",
				banserv_p->name);
		return 0;
	}

	if((p = strchr(mask, '@')))
	{
		*p++ = '\0';
		user = mask;
		host = p;
	}
	else
	{
		user = wild;
		host = mask;
	}

	service_send(banserv_p, client_p, conn_p,
			"Issued kline for %s@%s", user, host);

	sendto_server(":%s ENCAP * KLINE %lu %s %s :%s",
			banserv_p->name, temptime, user, host, reason);

	slog(banserv_p, 1, "%s - KLINE %s %s %s",
		OPER_NAME(client_p, conn_p), user, host, reason);

	return 0;
}

static int
o_banserv_xline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *gecos, *reason;
	time_t temptime = 0;
	int para = 0;

	if((temptime = get_temp_time(parv[para])))
		para++;

	if(!temptime)
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->flags & CONF_OPER_BAN_PERM))
				hit++;
		}
		else if(!conn_p->sprivs & CONF_OPER_BAN_PERM)
			hit++;

		if(hit)
		{
			service_send(banserv_p, client_p, conn_p,
					"No access to set permanent xlines");
			return 0;
		}
	}

	gecos = parv[para++];
	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_send(banserv_p, client_p, conn_p,
				"Insufficient parameters to %s::XLINE",
				banserv_p->name);
		return 0;
	}

	service_send(banserv_p, client_p, conn_p,
			"Issued xline for %s", gecos);

	sendto_server(":%s ENCAP * XLINE %lu %s 2 :%s",
			banserv_p->name, temptime, gecos, reason);

	slog(banserv_p, 1, "%s - XLINE %s %s",
		OPER_NAME(client_p, conn_p), gecos, reason);

	return 0;
}

static int
o_banserv_resv(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *mask, *reason;
	time_t temptime = 0;
	int para = 0;

	if((temptime = get_temp_time(parv[para])))
		para++;

	if(!temptime)
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->flags & CONF_OPER_BAN_PERM))
				hit++;
		}
		else if(!conn_p->sprivs & CONF_OPER_BAN_PERM)
			hit++;

		if(hit)
		{
			service_send(banserv_p, client_p, conn_p,
					"No access to set permanent resvs");
			return 0;
		}
	}

	mask = parv[para++];
	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_send(banserv_p, client_p, conn_p,
				"Insufficient parameters to %s::RESV",
				banserv_p->name);
		return 0;
	}

	service_send(banserv_p, client_p, conn_p,
			"Issued resv for %s", mask);

	sendto_server(":%s ENCAP * RESV %lu %s 0 :%s",
			banserv_p->name, temptime, mask, reason);

	slog(banserv_p, 1, "%s - RESV %s %s",
		OPER_NAME(client_p, conn_p), mask, reason);

	return 0;
}

#endif
