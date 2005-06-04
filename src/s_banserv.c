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
#include "event.h"

static struct client *banserv_p;

static int o_banserv_kline(struct client *, struct lconn *, const char **, int);
static int o_banserv_xline(struct client *, struct lconn *, const char **, int);
static int o_banserv_resv(struct client *, struct lconn *, const char **, int);
static int o_banserv_unkline(struct client *, struct lconn *, const char **, int);
static int o_banserv_unxline(struct client *, struct lconn *, const char **, int);
static int o_banserv_unresv(struct client *, struct lconn *, const char **, int);

static struct service_command banserv_command[] =
{
	{ "KLINE",	&o_banserv_kline,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_KLINE, 0 },
	{ "XLINE",	&o_banserv_xline,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_XLINE, 0 },
	{ "RESV",	&o_banserv_resv,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_RESV, 0 },
	{ "UNKLINE",	&o_banserv_unkline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_KLINE, 0 },
	{ "UNXLINE",	&o_banserv_unxline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_XLINE, 0 },
	{ "UNRESV",	&o_banserv_unresv,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_RESV, 0 }
};

static struct ucommand_handler banserv_ucommand[] =
{
	{ "kline",	o_banserv_kline,	0, CONF_OPER_BAN_KLINE, 2, 1, NULL },
	{ "xline",	o_banserv_xline,	0, CONF_OPER_BAN_XLINE, 2, 1, NULL },
	{ "resv",	o_banserv_resv,		0, CONF_OPER_BAN_RESV, 2, 1, NULL },
	{ "unkline",	o_banserv_unkline,	0, CONF_OPER_BAN_KLINE, 1, 1, NULL },
	{ "unxline",	o_banserv_unxline,	0, CONF_OPER_BAN_XLINE, 1, 1, NULL },
	{ "unresv",	o_banserv_unresv,	0, CONF_OPER_BAN_RESV, 1, 1, NULL },
	{ "\0", NULL, 0, 0, 0, 0, NULL }
};

static struct service_handler banserv_service = {
	"BANSERV", "BANSERV", "banserv", "services.int",
	"Global Ban Service", 60, 80, 
	banserv_command, sizeof(banserv_command), banserv_ucommand, NULL
};

static void e_banserv_expire(void *unused);

void
init_s_banserv(void)
{
	banserv_p = add_service(&banserv_service);

	eventAdd("e_banserv_expire", e_banserv_expire, NULL, 902);
}

static void
e_banserv_expire(void *unused)
{
	void *sql_vm;
	const char **coldata;
	const char **colnames;
	int ncol;

	sql_vm = loc_sqlite_compile("SELECT * FROM operbans WHERE "
				"hold <= %lu AND remove=0",
				CURRENT_TIME);

	while(loc_sqlite_step(sql_vm, &ncol, &coldata, &colnames))
	{
		if(coldata[0][0] == 'K')
		{
			static const char def_wild[] = "*";
			char buf[BUFSIZE];
			const char *user;
			char *host;

			strlcpy(buf, coldata[1], sizeof(buf));

			if((host = strchr(buf, '@')))
			{
				user = buf;
				*host++ = '\0';
			}
			else
			{
				user = def_wild;
				host = buf;
			}

			sendto_server(":%s ENCAP * UNKLINE %s %s",
					banserv_p, user, host);
		}
		else if(coldata[0][0] == 'X')
			sendto_server(":%s ENCAP * UNXLINE %s",
					banserv_p, coldata[1]);
		else if(coldata[0][0] == 'R')
			sendto_server(":%s ENCAP * UNRESV %s",
					banserv_p->name, coldata[1]);
	}

	loc_sqlite_exec(NULL, "DELETE FROM operbans WHERE hold <= %lu AND remove=1",
			CURRENT_TIME);
	loc_sqlite_exec(NULL, "UPDATE operbans SET remove=1, hold=%lu "
			"WHERE hold <= %lu",
			CURRENT_TIME + config_file.bs_unban_time, CURRENT_TIME);
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
find_ban(const char *mask, char type)
{
	void *sql_vm;
	const char **coldata;
	const char **colnames;
	int ncol;
	
	sql_vm = loc_sqlite_compile("SELECT * FROM operbans WHERE "
					"type='%c' AND mask=%Q",
					type, mask);

	if(loc_sqlite_step(sql_vm, &ncol, &coldata, &colnames))
	{
		if(atoi(coldata[7]) == 1)
		{
			loc_sqlite_finalize(sql_vm);
			return -1;
		}

		loc_sqlite_finalize(sql_vm);
		return 1;
	}

	return 0;
}

/* find_ban_remove()
 * Finds bans in the database suitable for removing.
 * 
 * inputs	- mask, type (K/X/R)
 * outputs	- oper who set the ban, NULL if none found
 * side effects	-
 */
static const char *
find_ban_remove(const char *mask, char type)
{
	static char buf[BUFSIZE];
	void *sql_vm;
	const char **coldata;
	const char **colnames;
	int ncol;
	
	sql_vm = loc_sqlite_compile("SELECT * FROM operbans WHERE "
					"type='%c' AND mask=%Q",
					type, mask);

	if(loc_sqlite_step(sql_vm, &ncol, &coldata, &colnames))
	{
		/* check this ban isnt already marked as removed */
		if(atoi(coldata[7]) == 1)
		{
			loc_sqlite_finalize(sql_vm);
			return NULL;
		}

		strlcpy(buf, coldata[6], sizeof(buf));
		loc_sqlite_finalize(sql_vm);
		return buf;
	}

	return NULL;
}

static int
o_banserv_kline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static const char wild[] = "*";
	const char *user, *host, *reason;
	char *mask, *p;
	time_t temptime = 0;
	int para = 0;
	int res;

	if((temptime = get_temp_time(parv[para])))
		para++;

	res = find_ban(parv[para], 'K');

	if(res == 1)
	{
		service_send(banserv_p, client_p, conn_p,
				"Kline already placed on %s",
				parv[para]);
		return 0;
	}

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

	if(res)
		loc_sqlite_exec(NULL, "UPDATE operbans SET reason=%Q, "
				"hold=%ld, oper=%Q, remove=0 WHERE "
				"type='K' AND mask='%q@%q'",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p),
				user, host);
	else
		loc_sqlite_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('K', '%q@%q', %Q, %lu, %lu, %Q, 0, 0)",
				user, host, reason,
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));
			
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
	int res;

	if((temptime = get_temp_time(parv[para])))
		para++;

	res = find_ban(parv[para], 'X');

	if(res == 1)
	{
		service_send(banserv_p, client_p, conn_p,
				"Xline already placed on %s",
				parv[para]);
		return 0;
	}

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

	if(res)
		loc_sqlite_exec(NULL, "UPDATE operbans SET reason=%Q, "
				"hold=%ld, oper=%Q, remove=0 WHERE "
				"type='X' AND mask=%Q",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p), gecos);
	else
		loc_sqlite_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('X', %Q, %Q, %lu, %lu, %Q, 0, 0)",
				gecos, reason, 
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));

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
	int res;

	if((temptime = get_temp_time(parv[para])))
		para++;

	res = find_ban(parv[para], 'R');

	if(res == 1)
	{
		service_send(banserv_p, client_p, conn_p,
				"Resv already placed on %s",
				parv[para]);
		return 0;
	}

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

	if(res)
		loc_sqlite_exec(NULL, "UPDATE operbans SET reason=%Q, "
				"hold=%ld, oper=%Q, remove=0 WHERE "
				"type='R' AND mask=%Q",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p), mask);
	else
		loc_sqlite_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('R', %Q, %Q, %lu, %lu, %Q, 0, 0)",
				mask, reason, 
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));

	service_send(banserv_p, client_p, conn_p,
			"Issued resv for %s", mask);

	sendto_server(":%s ENCAP * RESV %lu %s 0 :%s",
			banserv_p->name, temptime, mask, reason);

	slog(banserv_p, 1, "%s - RESV %s %s",
		OPER_NAME(client_p, conn_p), mask, reason);

	return 0;
}

static int
o_banserv_unkline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static const char wild[] = "*";
	const char *user, *host;
	char *mask, *p;
	const char *oper = find_ban_remove(parv[0], 'K');

	if(oper == NULL)
	{
		service_send(banserv_p, client_p, conn_p,
				"Kline not placed on %s",
				parv[0]);
		return 0;
	}

	if(irccmp(oper, OPER_NAME(client_p, conn_p)))
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->flags & CONF_OPER_BAN_REMOVE))
				hit++;
		}
		else if(!conn_p->sprivs & CONF_OPER_BAN_REMOVE)
			hit++;

		if(hit)
		{
			service_send(banserv_p, client_p, conn_p,
					"No access to remove klines");
			return 0;
		}
	}

	mask = LOCAL_COPY(parv[0]);

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

	loc_sqlite_exec(NULL, "UPDATE operbans SET remove=1, hold=%lu "
			"WHERE mask=%Q AND type='K'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_send(banserv_p, client_p, conn_p,
			"Issued unkline for %s@%s", user, host);

	sendto_server(":%s ENCAP * UNKLINE %s %s",
			banserv_p->name, user, host);

	slog(banserv_p, 1, "%s - UNKLINE %s %s",
		OPER_NAME(client_p, conn_p), user, host);

	return 0;
}

static int
o_banserv_unxline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *oper = find_ban_remove(parv[0], 'K');

	if(oper == NULL)
	{
		service_send(banserv_p, client_p, conn_p,
				"Xline not placed on %s",
				parv[0]);
		return 0;
	}

	if(irccmp(oper, OPER_NAME(client_p, conn_p)))
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->flags & CONF_OPER_BAN_REMOVE))
				hit++;
		}
		else if(!conn_p->sprivs & CONF_OPER_BAN_REMOVE)
			hit++;

		if(hit)
		{
			service_send(banserv_p, client_p, conn_p,
					"No access to remove xlines");
			return 0;
		}
	}

	loc_sqlite_exec(NULL, "UPDATE operbans SET remove=1, hold=%lu "
			"WHERE mask=%Q AND type='X'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_send(banserv_p, client_p, conn_p,
			"Issued unxline for %s", parv[0]);

	sendto_server(":%s ENCAP * UNXLINE %s",
			banserv_p->name, parv[0]);

	slog(banserv_p, 1, "%s - UNXLINE %s",
		OPER_NAME(client_p, conn_p), parv[0]);

	return 0;
}

static int
o_banserv_unresv(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *oper = find_ban_remove(parv[0], 'K');

	if(oper == NULL)
	{
		service_send(banserv_p, client_p, conn_p,
				"Resv not placed on %s",
				parv[0]);
		return 0;
	}

	if(irccmp(oper, OPER_NAME(client_p, conn_p)))
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->flags & CONF_OPER_BAN_REMOVE))
				hit++;
		}
		else if(!conn_p->sprivs & CONF_OPER_BAN_REMOVE)
			hit++;

		if(hit)
		{
			service_send(banserv_p, client_p, conn_p,
					"No access to remove xlines");
			return 0;
		}
	}

	loc_sqlite_exec(NULL, "UPDATE operbans SET remove=1, hold=%lu "
			"WHERE mask=%Q AND type='R'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_send(banserv_p, client_p, conn_p,
			"Issued unresv for %s", parv[0]);

	sendto_server(":%s ENCAP * UNRESV %s",
			banserv_p->name, parv[0]);

	slog(banserv_p, 1, "%s - UNRESV %s",
		OPER_NAME(client_p, conn_p), parv[0]);

	return 0;
}

#endif