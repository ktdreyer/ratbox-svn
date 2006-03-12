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
#include "rsdb.h"
#include "rserv.h"
#include "io.h"
#include "service.h"
#include "client.h"
#include "channel.h"
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
static int o_banserv_sync(struct client *, struct lconn *, const char **, int);
static int o_banserv_findkline(struct client *, struct lconn *, const char **, int);
static int o_banserv_findxline(struct client *, struct lconn *, const char **, int);
static int o_banserv_findresv(struct client *, struct lconn *, const char **, int);

static struct service_command banserv_command[] =
{
	{ "KLINE",	&o_banserv_kline,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_KLINE, 0 },
	{ "XLINE",	&o_banserv_xline,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_XLINE, 0 },
	{ "RESV",	&o_banserv_resv,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_RESV, 0 },
	{ "UNKLINE",	&o_banserv_unkline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_KLINE, 0 },
	{ "UNXLINE",	&o_banserv_unxline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_XLINE, 0 },
	{ "UNRESV",	&o_banserv_unresv,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_RESV, 0 },
	{ "SYNC",	&o_banserv_sync,	1, NULL, 1, 0L, 0, 0, 0, 0 },
	{ "FINDKLINE",	&o_banserv_findkline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_KLINE, 0 },
	{ "FINDXLINE",	&o_banserv_findxline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_XLINE, 0 },
	{ "FINDRESV",	&o_banserv_findresv,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_RESV, 0 }
};

static struct ucommand_handler banserv_ucommand[] =
{
	{ "kline",	o_banserv_kline,	0, CONF_OPER_BAN_KLINE, 2, 1, NULL },
	{ "xline",	o_banserv_xline,	0, CONF_OPER_BAN_XLINE, 2, 1, NULL },
	{ "resv",	o_banserv_resv,		0, CONF_OPER_BAN_RESV, 2, 1, NULL },
	{ "unkline",	o_banserv_unkline,	0, CONF_OPER_BAN_KLINE, 1, 1, NULL },
	{ "unxline",	o_banserv_unxline,	0, CONF_OPER_BAN_XLINE, 1, 1, NULL },
	{ "unresv",	o_banserv_unresv,	0, CONF_OPER_BAN_RESV, 1, 1, NULL },
	{ "findkline",	o_banserv_findkline,	0, CONF_OPER_BAN_KLINE, 1, 1, NULL },
	{ "findxline",	o_banserv_findxline,	0, CONF_OPER_BAN_XLINE, 1, 1, NULL },
	{ "findresv",	o_banserv_findresv,	0, CONF_OPER_BAN_RESV, 1, 1, NULL },
	{ "sync",	o_banserv_sync,		0, 0, 1, 1, NULL },
	{ "\0", NULL, 0, 0, 0, 0, NULL }
};

static struct service_handler banserv_service = {
	"BANSERV", "BANSERV", "banserv", "services.int",
	"Global Ban Service", 60, 80, 
	banserv_command, sizeof(banserv_command), banserv_ucommand, NULL, NULL
};

static void e_banserv_expire(void *unused);
static void e_banserv_autosync(void *unused);

static void push_unban(const char *target, char type, const char *mask);
static void sync_bans(const char *target, char banletter);

void
preinit_s_banserv(void)
{
	banserv_p = add_service(&banserv_service);

	eventAdd("banserv_expire", e_banserv_expire, NULL, 902);
	eventAdd("banserv_autosync", e_banserv_autosync, NULL,
			DEFAULT_AUTOSYNC_FREQUENCY);
}

static void
e_banserv_autosync(void *unused)
{
	sync_bans("*", 0);
}

static void
e_banserv_expire(void *unused)
{
	/* these bans are temp, so they will expire automatically on 
	 * servers
	 */
	rsdb_exec(NULL, "DELETE FROM operbans WHERE hold != 0 AND hold <= %lu",
			CURRENT_TIME);
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
split_ban(const char *mask, char **user, char **host)
{
	static char buf[BUFSIZE];
	char *p;

	strlcpy(buf, mask, sizeof(buf));

	if((p = strchr(buf, '@')) == NULL)
		return 0;

	*p++ = '\0';

	if(EmptyString(buf) || EmptyString(p))
		return 0;

	if(strlen(buf) > USERLEN || strlen(p) > HOSTLEN)
		return 0;

	if(user)
		*user = buf;

	if(host)
		*host = p;

	return 1;
}

static int
find_ban(const char *mask, char type)
{
	struct rsdb_table data;
	int retval;

	rsdb_exec_fetch(&data, "SELECT remove FROM operbans WHERE type='%c' AND mask='%Q' LIMIT 1",
			type, mask);

	if(data.row_count)
	{
		if(atoi(data.row[0][0]) == 1)
			retval = -1;
		else
			retval = 1;
	}
	else
		retval = 0;

	rsdb_exec_fetch_end(&data);

	return retval;
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
	struct rsdb_table data;
	const char *retval;

	rsdb_exec_fetch(&data, "SELECT remove, oper FROM operbans WHERE type='%c' AND mask='%Q' LIMIT 1",
			type, mask);

	if(data.row_count && (atoi(data.row[0][0]) == 0))
	{
		strlcpy(buf, data.row[0][1], sizeof(buf));
		retval = buf;
	}
	else
		retval = NULL;

	rsdb_exec_fetch_end(&data);

	return retval;
}

static void
push_ban(const char *target, char type, const char *mask, 
		const char *reason, time_t hold)
{
	/* when ircd receives a temp ban it already has banned, it just
	 * ignores the request and doesnt update the expiry.  This can cause
	 * problems with the old max temp time of 4 weeks.  This work
	 * around issues an unban first, allowing the new ban to be set,
	 * which just delays the expiry somewhat.
	 *
	 * We only do this for temporary bans. --anfl
	 */
	if(config_file.bs_temp_workaround && hold)
		push_unban(target, type, mask);

	if(type == 'K')
	{
		char *user, *host;

		if(!split_ban(mask, &user, &host))
			return;

		sendto_server(":%s ENCAP %s KLINE %lu %s %s :%s",
				banserv_p->name, target,
				(unsigned long) hold,
				user, host, reason);
	}
	else if(type == 'X')
		sendto_server(":%s ENCAP %s XLINE %lu %s 2 :%s",
				banserv_p->name, target,
				(unsigned long) hold, mask, reason);
	else if(type == 'R')
		sendto_server(":%s ENCAP %s RESV %lu %s 0 :%s",
				banserv_p->name, target,
				(unsigned long) hold, mask, reason);
}

static void
push_unban(const char *target, char type, const char *mask)
{
	if(type == 'K')
	{
		char *user, *host;

		if(!split_ban(mask, &user, &host))
			return;

		sendto_server(":%s ENCAP %s UNKLINE %s %s",
				banserv_p->name, target, user, host);
	}
	else if(type == 'X')
		sendto_server(":%s ENCAP %s UNXLINE %s",
				banserv_p->name, target, mask);
	else if(type == 'R')
		sendto_server(":%s ENCAP %s UNRESV %s",
				banserv_p->name, target, mask);
}

static void
sync_bans(const char *target, char banletter)
{
	struct rsdb_table data;
	int i;

	/* first is temporary bans */
	if(banletter)
		rsdb_exec_fetch(&data, "SELECT type, mask, reason, hold FROM operbans "
					"WHERE hold > %lu AND remove=0 AND type='%c'",
				CURRENT_TIME, banletter);
	else
		rsdb_exec_fetch(&data, "SELECT type, mask, reason, hold FROM operbans "
					"WHERE hold > %lu AND remove=0",
				CURRENT_TIME);

	for(i = 0; i < data.row_count; i++)
	{
		push_ban(target, data.row[i][0][0], data.row[i][1], data.row[i][2],
			(unsigned long) (atol(data.row[i][3]) - CURRENT_TIME));
	}

	rsdb_exec_fetch_end(&data);

	/* permanent bans */
	if(banletter)
		rsdb_exec_fetch(&data, "SELECT type, mask, reason, hold FROM operbans "
					"WHERE hold=0 AND remove=0 AND type='%c'",
				CURRENT_TIME, banletter);
	else
		rsdb_exec_fetch(&data, "SELECT type, mask, reason, hold FROM operbans "
					"WHERE hold=0 AND remove=0",
				CURRENT_TIME);

	for(i = 0; i < data.row_count; i++)
	{
		push_ban(target, data.row[i][0][0], data.row[i][1], data.row[i][2], 0);
	}

	rsdb_exec_fetch_end(&data);

	/* bans to remove */
	if(banletter)
		rsdb_exec_fetch(&data, "SELECT type, mask FROM operbans "
					"WHERE hold > %lu AND remove=1 AND type='%c'",
				CURRENT_TIME, banletter);
	else
		rsdb_exec_fetch(&data, "SELECT type, mask FROM operbans "
					"WHERE hold > %lu AND remove=1",
				CURRENT_TIME);

	for(i = 0; i < data.row_count; i++)
	{
		push_unban(target, data.row[i][0][0], data.row[i][1]);
	}

	rsdb_exec_fetch_end(&data);
}

static int
o_banserv_kline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *mask;
	char *reason;
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
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_PERM))
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

	mask = parv[para++];

	if(!split_ban(mask, NULL, NULL))
	{
		service_send(banserv_p, client_p, conn_p,
				"Invalid kline %s", mask);
		return 0;
	}

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_send(banserv_p, client_p, conn_p,
				"Insufficient parameters to %s::KLINE",
				banserv_p->name);
		return 0;
	}

	if(strlen(reason) > REASONLEN)
		reason[REASONLEN] = '\0';

	if(res)
		rsdb_exec(NULL, "UPDATE operbans SET reason='%Q', "
				"hold=%ld, oper='%Q', remove=0 WHERE "
				"type='K' AND mask='%Q'",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p), mask);
	else
		rsdb_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('K', '%Q', '%Q', %lu, %lu, '%Q', 0, 0)",
				mask, reason,
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));
			
	service_send(banserv_p, client_p, conn_p,
			"Issued kline for %s", mask);

	push_ban("*", 'K', mask, reason, temptime);

	slog(banserv_p, 1, "%s - KLINE %s %s",
		OPER_NAME(client_p, conn_p), mask, reason);

	return 0;
}

static int
o_banserv_xline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *gecos;
	char *reason;
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
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_PERM))
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

	if(strlen(gecos) > NICKUSERHOSTLEN)
	{
		service_send(banserv_p, client_p, conn_p,
				"Invalid xline %s", gecos);
		return 0;
	}

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_send(banserv_p, client_p, conn_p,
				"Insufficient parameters to %s::XLINE",
				banserv_p->name);
		return 0;
	}

	if(strlen(reason) > REASONLEN)
		reason[REASONLEN] = '\0';

	if(res)
		rsdb_exec(NULL, "UPDATE operbans SET reason='%Q', "
				"hold=%ld, oper='%Q', remove=0 WHERE "
				"type='X' AND mask='%Q'",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p), gecos);
	else
		rsdb_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('X', '%Q', '%Q', %lu, %lu, '%Q', 0, 0)",
				gecos, reason, 
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));

	service_send(banserv_p, client_p, conn_p,
			"Issued xline for %s", gecos);

	push_ban("*", 'X', gecos, reason, temptime);

	slog(banserv_p, 1, "%s - XLINE %s %s",
		OPER_NAME(client_p, conn_p), gecos, reason);

	return 0;
}

static int
o_banserv_resv(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *mask;
	char *reason;
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
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_PERM))
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

	if(strlen(mask) > CHANNELLEN)
	{
		service_send(banserv_p, client_p, conn_p,
				"Invalid resv %s", mask);
		return 0;
	}

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_send(banserv_p, client_p, conn_p,
				"Insufficient parameters to %s::RESV",
				banserv_p->name);
		return 0;
	}

	if(strlen(reason) > REASONLEN)
		reason[REASONLEN] = '\0';

	if(res)
		rsdb_exec(NULL, "UPDATE operbans SET reason='%Q', "
				"hold=%ld, oper='%Q', remove=0 WHERE "
				"type='R' AND mask='%Q'",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p), mask);
	else
		rsdb_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('R', '%Q', '%Q', %lu, %lu, '%Q', 0, 0)",
				mask, reason, 
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));

	service_send(banserv_p, client_p, conn_p,
			"Issued resv for %s", mask);

	push_ban("*", 'R', mask, reason, temptime);

	slog(banserv_p, 1, "%s - RESV %s %s",
		OPER_NAME(client_p, conn_p), mask, reason);

	return 0;
}

static int
o_banserv_unkline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
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
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_REMOVE))
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

	if(!split_ban(parv[0], NULL, NULL))
	{
		service_send(banserv_p, client_p, conn_p,
				"Invalid kline %s", parv[0]);
		return 0;
	}

	rsdb_exec(NULL, "UPDATE operbans SET remove=1, hold=%lu "
			"WHERE mask='%Q' AND type='K'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_send(banserv_p, client_p, conn_p,
			"Issued unkline for %s", parv[0]);

	push_unban("*", 'K', parv[0]);

	slog(banserv_p, 1, "%s - UNKLINE %s",
		OPER_NAME(client_p, conn_p), parv[0]);

	return 0;
}

static int
o_banserv_unxline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *oper = find_ban_remove(parv[0], 'X');

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
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_REMOVE))
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

	rsdb_exec(NULL, "UPDATE operbans SET remove=1, hold=%lu "
			"WHERE mask='%Q' AND type='X'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_send(banserv_p, client_p, conn_p,
			"Issued unxline for %s", parv[0]);

	push_unban("*", 'X', parv[0]);

	slog(banserv_p, 1, "%s - UNXLINE %s",
		OPER_NAME(client_p, conn_p), parv[0]);

	return 0;
}

static int
o_banserv_unresv(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *oper = find_ban_remove(parv[0], 'R');

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
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_REMOVE))
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

	rsdb_exec(NULL, "UPDATE operbans SET remove=1, hold=%lu "
			"WHERE mask='%Q' AND type='R'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_send(banserv_p, client_p, conn_p,
			"Issued unresv for %s", parv[0]);

	push_unban("*", 'R', parv[0]);

	slog(banserv_p, 1, "%s - UNRESV %s",
		OPER_NAME(client_p, conn_p), parv[0]);

	return 0;
}

static int
o_banserv_sync(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	char banletter = '\0';

	if(conn_p || irccmp(client_p->user->servername, parv[0]))
	{
		struct client *target_p;
		dlink_node *ptr;
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_SYNC))
				hit++;
		}
		else if(!conn_p->sprivs & CONF_OPER_BAN_SYNC)
			hit++;

		if(hit)
		{
			service_send(banserv_p, client_p, conn_p,
					"No access to sync bans");
			return 0;
		}

		/* check their mask matches at least one server */
		DLINK_FOREACH(ptr, server_list.head)
		{
			target_p = ptr->data;

			if(match(parv[0], target_p->name))
				break;
		}

		/* NULL if loop terminated without a break */
		if(ptr == NULL)
		{
			service_send(banserv_p, client_p, conn_p,
				"Server %s does not exist", parv[0]);
			return 0;
		}
	}

	if(!EmptyString(parv[1]))
	{
		if(!irccmp(parv[1], "klines"))
			banletter = 'K';
		else if(!irccmp(parv[1], "xlines"))
			banletter = 'X';
		else if(!irccmp(parv[1], "resvs"))
			banletter = 'R';
		else
		{
			service_send(banserv_p, client_p, conn_p,
					"Invalid ban type");
			return 0;
		}
	}

	sync_bans(parv[0], banletter);

	service_send(banserv_p, client_p, conn_p,
			"Issued sync to %s", parv[0]);

	slog(banserv_p, 1, "%s - SYNC %s %s",
		OPER_NAME(client_p, conn_p), parv[0],
		EmptyString(parv[1]) ? "" : parv[1]);

	return 0;
}

static void
list_bans(struct client *client_p, struct lconn *conn_p, 
		const char *mask, char type)
{
	struct rsdb_table data;
	time_t duration;
	int i;

	rsdb_exec_fetch(&data, "SELECT mask, reason, operreason, hold, oper "
				"FROM operbans WHERE type='%c' AND remove=0 AND hold > %lu",
			type, (unsigned long) CURRENT_TIME);

	service_send(banserv_p, client_p, conn_p,
			"Ban list matching %s", mask);

	for(i = 0; i < data.row_count; i++)
	{
		if(!match(mask, data.row[i][0]))
			continue;

		duration = ((unsigned long) atol(data.row[i][3]) - CURRENT_TIME);

		service_send(banserv_p, client_p, conn_p,
				"  %-30s exp:%s oper:%s [%s%s]",
				data.row[i][0], get_short_duration(duration),
				data.row[i][4], data.row[i][1],
				EmptyString(data.row[i][2]) ? "" : data.row[i][2]);
	}

	rsdb_exec_fetch_end(&data);

	service_send(banserv_p, client_p, conn_p, "End of ban list");
}

static int
o_banserv_findkline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	list_bans(client_p, conn_p, parv[0], 'K');
	return 0;
}

static int
o_banserv_findxline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	list_bans(client_p, conn_p, parv[0], 'X');
	return 0;
}

static int
o_banserv_findresv(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	list_bans(client_p, conn_p, parv[0], 'R');
	return 0;
}

#endif
