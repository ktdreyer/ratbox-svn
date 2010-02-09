/* src/s_chanfix.c
 *   Contains the code for the chanfix service.
 *
 * Copyright (C) 2004-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2004-2007 ircd-ratbox development team
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
 * $Id: s_chanfix.c 26718 2010-01-03 18:43:02Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_CHANFIX
#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "io.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "hook.h"
#include "ucommand.h"
#include "newconf.h"
#include "watch.h"

/* Please note that this CHANFIX module is very much a work in progress,
 * and that it is likely to change frequently & dramatically whilst being
 * developed. Thanks.
 */

static void init_s_chanfix(void);

static struct client *chanfix_p;

static int o_chanfix_cfjoin(struct client *, struct lconn *, const char **, int);
static int o_chanfix_cfpart(struct client *, struct lconn *, const char **, int);
static int o_chanfix_chanfix(struct client *, struct lconn *, const char **, int);
static int o_chanfix_check(struct client *, struct lconn *, const char **, int);
static int o_chanfix_set(struct client *, struct lconn *, const char **, int);
static int o_chanfix_status(struct client *, struct lconn *, const char **, int);


static struct service_command chanfix_command[] =
{
	{ "CFJOIN",	&o_chanfix_cfjoin,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "CFPART",	&o_chanfix_cfpart,	1, NULL, 1, 0L, 0, 1, 0 },
	/*{ "SCORE",&o_chanfix_cfpart,	1, NULL, 1, 0L, 0, 1, 0 },*/
	{ "CHANFIX",&o_chanfix_chanfix,	1, NULL, 1, 0L, 0, 1, 0 },
	/*{ "HISTORY",&o_chanfix_history,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "INFO",	&o_chanfix_info,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "OPLIST",	&o_chanfix_oplist,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "ADDNOTE",&o_chanfix_addnote,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "DELNOTE",&o_chanfix_delnote,	1, NULL, 1, 0L, 0, 1, 0 },*/
	{ "SET",	&o_chanfix_set,	1, NULL, 1, 0L, 0, 1, 0 },
	/*{ "BLOCK",	&o_chanfix_block,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "UNBLOCK",&o_chanfix_unblock,	1, NULL, 1, 0L, 0, 1, 0 },*/
	{ "CHECK",	&o_chanfix_check,	1, NULL, 1, 0L, 0, 1, 0 },
	/*{ "OPNICKS",&o_chanfix_opnicks,	1, NULL, 1, 0L, 0, 1, 0 },*/
	{ "STATUS",&o_chanfix_status,	0, NULL, 1, 0L, 0, 1, 0 }
};

static struct ucommand_handler chanfix_ucommand[] =
{
	/*{ "cfjoin",	o_chanfix_cfjoin,	0, CONF_OPER_OB_CHANNEL, 1, NULL },
	{ "cfpart",	o_chanfix_cfpart,	0, CONF_OPER_OB_CHANNEL, 1, NULL },*/
	{ "\0",		NULL,			0, 0, 0, NULL }
};

static struct service_handler chanfix_service = {
	"CHANFIX", "chanfix", "chanfix", "services.int",
	"Channel fixing service", 0, 0, 
	chanfix_command, sizeof(chanfix_command), chanfix_ucommand, init_s_chanfix, NULL
};

/*static int operbot_db_callback(int, const char **);*/

void
preinit_s_chanfix(void)
{
	chanfix_p = add_service(&chanfix_service);
}

static void
init_s_chanfix(void)
{
	/*rsdb_exec(operbot_db_callback,
			"SELECT chname, tsinfo FROM operbot");*/

	/*eventAdd("chanfix_join", e_join_channels, NULL, 180);
	eventAdd("chanfix_part", e_part_channels, NULL, 300);*/
}


/*static int
chanfix_db_callback(int argc, const char **argv)
{
	join_service(chanfix_p, argv[0], atol(argv[1]), NULL, 0);
	return 0;
}*/



/* cf_count_opped_users()
 *   Count the number of opped users in the channel. Does not include
 *   any services that may be in the channel.
 * inputs	- channel ptr
 * outputs	- number of opped users
 */
static int
cf_count_opped_users(struct channel *chptr) {
	int opcount = 0;
	struct chmember *msptr;
	dlink_node *ptr;
	DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;
		if(is_opped(msptr))
			opcount++;
	}
	return opcount;
}


/* cf_num_of_linked_servers()
 *   Returns the number of linked servers on the network.*/
static int
cf_num_of_linked_servers(void)
{
	return dlink_list_length(&server_list);
}

/* cf_check_min_servers_linked()
 *   Checks to see if the min number of servers are linked to know
 *   whether we're split or not.
 * inputs	-
 * outputs	- 1 if enough servers are linked, 0 is not
 */
static int
cf_check_min_servers_linked(void)
{
	int num_linked = cf_num_of_linked_servers();
	
	if(num_linked * 100 >= config_file.cf_minimum_servers * config_file.cf_network_servers)
	{
		return 1;
	}

	return 0;
}


/* cf_send_chan_privmsg()
 *   Sends the given message and args to the channel.
 *
 * inputs	- channel ptr, formatted string, args
 * outputs	-
 */
static void
cf_send_chan_privmsg(struct channel *chptr, const char *format, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	/* Make sure we're in the channel before we try to send */
	if (dlink_find(chanfix_p, &chptr->services)) {
		sendto_server(":%s PRIVMSG %s :%s", chanfix_p->name, chptr->name, buf);
	}
}



/* preconditions: TS >= 2 and there is at least one user in the channel */
static void
cf_takeover(struct channel *chptr, int invite)
{
	dlink_node *ptr;

	part_service(chanfix_p, chptr->name);

	remove_our_modes(chptr);

	if (EmptyString(server_p->sid))
	{
		modebuild_start(chanfix_p, chptr);

		DLINK_FOREACH(ptr, chptr->bans.head)
		{
			modebuild_add(DIR_DEL, "b", ptr->data);
		}
		DLINK_FOREACH(ptr, chptr->excepts.head)
		{
			modebuild_add(DIR_DEL, "e", ptr->data);
		}
		DLINK_FOREACH(ptr, chptr->invites.head)
		{
			modebuild_add(DIR_DEL, "I", ptr->data);
		}
	}

	remove_bans(chptr);

	if(invite)
		chptr->mode.mode = MODE_TOPIC|MODE_NOEXTERNAL|MODE_INVITEONLY;
	else
		chptr->mode.mode = MODE_TOPIC|MODE_NOEXTERNAL;

	chptr->tsinfo--;

	join_service(chanfix_p, chptr->name, chptr->tsinfo, NULL, 0);

	/* apply the -beI if needed, after the join */
	if(EmptyString(server_p->sid))
		modebuild_finish();

	/* need to reop some services */
	if(dlink_list_length(&chptr->services) > 1)
	{
		struct client *target_p;
		modebuild_start(chanfix_p, chptr);
		DLINK_FOREACH(ptr, chptr->services.head)
		{
			target_p = ptr->data;

			if(target_p != chanfix_p)
				modebuild_add(DIR_ADD, "o", target_p->name);
		}
		modebuild_finish();
	}
}



static int
o_chanfix_cfjoin(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	time_t tsinfo;

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	if((chptr = find_channel(parv[0])) && 
	   dlink_find(chanfix_p, &chptr->services))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_ALREADYONCHANNEL,
				chanfix_p->name, chptr->name);
		return 0;
	}

	zlog(chanfix_p, 1, WATCH_OPERBOT, 1, client_p, conn_p,
		"CFJOIN %s", parv[0]);

	tsinfo = chptr != NULL ? chptr->tsinfo : CURRENT_TIME;

	/*rsdb_exec(NULL, "INSERT INTO operbot (chname, tsinfo, oper) VALUES(LOWER('%Q'), '%lu', '%Q')",
			parv[0], tsinfo, OPER_NAME(client_p, conn_p));*/

	join_service(chanfix_p, parv[0], tsinfo, NULL, 0);

	service_snd(chanfix_p, client_p, conn_p, SVC_SUCCESSFULON,
			chanfix_p->name, "CFJOIN", parv[0]);
	return 0;
}

static int
o_chanfix_cfpart(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	if(part_service(chanfix_p, parv[0]))
	{
		zlog(chanfix_p, 1, WATCH_OPERBOT, 1, client_p, conn_p,
			"CFPART %s", parv[0]);

		/*rsdb_exec(NULL, "DELETE FROM operbot WHERE chname = LOWER('%Q')",
				parv[0]);*/
		service_snd(chanfix_p, client_p, conn_p, SVC_SUCCESSFULON,
				chanfix_p->name, "CFPART", parv[0]);
	}
	else
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_NOTINCHANNEL,
				chanfix_p->name, parv[0]);

	return 0;
}

static int
o_chanfix_chanfix(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	int override = 0;

	if(!valid_chname(parv[0])) {
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}
	if((chptr = find_channel(parv[0])) == NULL) {
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_NOSUCHCHANNEL, parv[0]);
		return 0;
	}
	if(dlink_list_length(&chptr->users) < config_file.cf_min_clients) {
		/* Not enough users in this channel */
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOTENOUGHUSERS, parv[0]);
		return 0;
	}

	/* Check if anyone is opped, if they are we don't do anything unless someone forced us. */
	if(cf_count_opped_users(chptr) > 0)
	{
		/* Check whether the OVERRIDE flag has been given */
		if (parc > 1) {
			if (!irccmp(parv[1], "override") || parv[1][0] == '!') {
				if(chptr->tsinfo < 2) {
					service_send(chanfix_p, client_p, conn_p,
									"Channel '%s' TS too low for takeover", parv[0]);
					return 0;
				}
				/* override command has been given, so we should take-over the channel */
				cf_takeover(chptr, 0);
				cf_send_chan_privmsg(chptr, "Channel fix in progress, please stand by.");
				override = 1;
			}
		}
		else
		{
			service_snd(chanfix_p, client_p, conn_p, SVC_CF_HASOPPEDUSERS, parv[0]);
			return 0;
		}
	}


	/*if(!dlink_find(chanfix_p, &chptr->services))*/

	/* override command will have already joined chanfix for us */
	if(!override)
		join_service(chanfix_p, parv[0], chptr->tsinfo, NULL, 0);
 
	service_snd(chanfix_p, client_p, conn_p, SVC_ISSUEDFORBY,
				chanfix_p->name, "CHANFIX", parv[0], client_p->name);

	/* for now we'll just op everyone */
	dlink_node *ptr;
	struct chmember *msptr;
	modebuild_start(chanfix_p, chptr);
	/* Do I need to use DLINK_FOREACH_SAFE here? */
	DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;
		msptr->flags &= ~MODE_DEOPPED;
		msptr->flags |= MODE_OPPED;
		modebuild_add(DIR_ADD, "o", msptr->client_p->name);
	}
	modebuild_finish();

	/* Once the channel has been taken over, we should add it to the list of channels that need
	to be fixed based on their scores. */

	part_service(chanfix_p, parv[0]);

	return 0;
}



static int
o_chanfix_check(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	int users, ops;

	if(!valid_chname(parv[0])) {
		service_err(chanfix_p, client_p, SVC_IRC_CHANNELINVALID, chanfix_p->name, parv[0]);
		return 0;
	}
	if((chptr = find_channel(parv[0])) == NULL) {
		service_err(chanfix_p, client_p, SVC_IRC_NOSUCHCHANNEL, chanfix_p->name, parv[0]);
		return 0;
	}

	service_snd(chanfix_p, client_p, conn_p, SVC_ISSUEDFORBY,
				chanfix_p->name, "CHECK", parv[0], client_p->name);

	ops = cf_count_opped_users(chptr);
	users = dlink_list_length(&chptr->users);
	service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHECK, parv[0], ops, users);
	return 0;
}


static int
o_chanfix_set(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	if(!irccmp(parv[0], "network_servers")) {
		if(parc > 1) {
			int num;
			num = atoi(parv[1]);
			if(num > 0) {
				config_file.cf_network_servers = num;
				service_send(chanfix_p, client_p, conn_p, "Number of network_servers is now %d.", num);
			} else {
				service_send(chanfix_p, client_p, conn_p, "Invalid parameter. Use SET network_servers <integer>.");
			}
		} else {
			/* Instead of returning an error here, we should show the current value */
			service_err(chanfix_p, client_p, SVC_NEEDMOREPARAMS, chanfix_p->name, "SET::network_servers");
		}
	}
	else if(!irccmp(parv[0], "enable_autofix")) {
		if(parc > 1) {
			if(!irccmp(parv[1], "on") || !irccmp(parv[0], "yes")) {
				service_send(chanfix_p, client_p, conn_p, "Automatic channel fixing enabled.");
				config_file.cf_enable_autofix = 1;
			}
			else if(!irccmp(parv[1], "off") || !irccmp(parv[0], "no")) {
				service_send(chanfix_p, client_p, conn_p, "Automatic channel fixing disabled.");
				config_file.cf_enable_autofix = 0;
			} else {
				service_send(chanfix_p, client_p, conn_p, "Invalid parameter. Use SET enable_autofix <on|off>.");
			}
		} else {
			/* Instead of returning an error here, we should show the current value */
			service_err(chanfix_p, client_p, SVC_NEEDMOREPARAMS, chanfix_p->name, "SET::enable_autofix");
		}
	}
	else if(!irccmp(parv[0], "enable_chanfix")) {
		if(parc > 1) {
			if(!irccmp(parv[1], "on") || !irccmp(parv[0], "yes")) {
				service_send(chanfix_p, client_p, conn_p, "Manual channel fixing enabled.");
				config_file.cf_enable_chanfix = 1;
			}
			else if(!irccmp(parv[1], "off") || !irccmp(parv[0], "no")) {
				service_send(chanfix_p, client_p, conn_p, "Manual channel fixing disabled.");
				config_file.cf_enable_chanfix = 0;
			} else {
				service_send(chanfix_p, client_p, conn_p, "Invalid parameter. Use SET enable_chanfix <on|off>.");
			}
		} else {
			service_err(chanfix_p, client_p, SVC_NEEDMOREPARAMS, chanfix_p->name, "SET::enable_chanfix");
		}
	}
	else {
		service_err(chanfix_p, client_p, SVC_OPTIONINVALID, chanfix_p->name, "SET");
	}
		
	return 0;
}

static int
o_chanfix_status(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	service_send(chanfix_p, client_p, conn_p,
		"This is ratbox-services version %s", MYNAME, RSERV_VERSION);

	if(config_file.cf_enable_autofix)
		service_send(chanfix_p, client_p, conn_p, "Automatic channel fixing enabled.");
	else
		service_send(chanfix_p, client_p, conn_p, "Automatic channel fixing disabled.");

	if(config_file.cf_enable_chanfix)
		service_send(chanfix_p, client_p, conn_p, "Manual channel fixing enabled.");
	else
		service_send(chanfix_p, client_p, conn_p, "Manual channel fixing disabled.");

	service_send(chanfix_p, client_p, conn_p,
			"At least %d percent of all network servers (%d) need to be linked, which is a minimum of %d.",
				config_file.cf_minimum_servers, config_file.cf_network_servers,
				(config_file.cf_minimum_servers * config_file.cf_network_servers) / 100 + 1);

	if(cf_check_min_servers_linked())
		service_send(chanfix_p, client_p, conn_p, "Chanfix splitmode not active.");
	else
		service_send(chanfix_p, client_p, conn_p, "Chanfix splitmode enabled.");
		
	return 0;
}

#endif
