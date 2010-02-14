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
 * $Id$
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
#include "tools.h"
#include "ucommand.h"
#include "newconf.h"
#include "watch.h"
#include "modebuild.h"
#include "s_chanfix.h"

/* Please note that this CHANFIX module is very much a work in progress,
 * and that it is likely to change frequently & dramatically whilst being
 * developed. Thanks.
 */

static void init_s_chanfix(void);

static struct client *chanfix_p;

static dlink_list chanfix_list;

/* Services operator functions */
static int o_chanfix_cfjoin(struct client *, struct lconn *, const char **, int);
static int o_chanfix_cfpart(struct client *, struct lconn *, const char **, int);
static int o_chanfix_chanfix(struct client *, struct lconn *, const char **, int);
static int o_chanfix_check(struct client *, struct lconn *, const char **, int);
static int o_chanfix_set(struct client *, struct lconn *, const char **, int);
static int o_chanfix_status(struct client *, struct lconn *, const char **, int);

/* Event triggered functions */
static void e_rotate_chanfix_db(void);
static void e_fix_chanfix_channels(void);
static void e_fix_autofix_channels(void);
static void e_gather_channels(void);

/* Internal chanfix functions */
static void send_chan_privmsg(struct channel *, const char *, ...);
static void gather_channel_bucket(void);
static void chan_takeover(struct channel *, int);
static int chan_remove_modes(struct channel *, char);
static int chan_remove_bans(struct channel *, char);
static int is_chan_being_fixed(const char *);
static int is_being_chanfixed(const char *);
static int is_being_autofixed(const char *);
static time_t seconds_to_midnight(void);
static void e_find_oppless_channels(void);

static int add_chanfix(struct channel *chptr);
static void del_chanfix(struct channel *chptr);

static struct service_command chanfix_command[] =
{
	{ "CFJOIN",	&o_chanfix_cfjoin,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "CFPART",	&o_chanfix_cfpart,	1, NULL, 1, 0L, 0, 1, 0 },
	/*{ "SCORE",&o_chanfix_score,	1, NULL, 1, 0L, 0, 1, 0 },*/
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

static int h_chanfix_channel_destroy(void *chptr_v, void *unused);
static int h_find_oppless_after_burst(void *unused, void *unused);

void
preinit_s_chanfix(void)
{
	chanfix_p = add_service(&chanfix_service);
}

static void
init_s_chanfix(void)
{
	hook_add(h_chanfix_channel_destroy, HOOK_CHANNEL_DESTROY);
	/*hook_add(h_find_oppless_after_burst, HOOK_FINISHED_BURSTING);*/

	/*eventAdd("cf_fix_chanfix_channels", e_fix_chanfix_channels, NULL, 300);
	eventAdd("cf_fix_autofix_channels", e_fix_autofix_channels, NULL, 300);
	eventAddOnce("cf_rotate_cf_db", e_rotate_chanfix_db, NULL, seconds_to_midnight());
	*/
}

/* preconditions: TS >= 2 and there is at least one user in the channel */
static void
chan_takeover(struct channel *chptr, int invite)
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
h_chanfix_channel_destroy(void *chptr_v, void *unused)
{
	struct channel *chptr = (struct channel *) chptr_v;

	if(chptr->cfptr)
		del_chanfix(chptr);
}

static int
h_find_oppless_after_burst(void *unused, void *unused)
{
	struct channel *chptr = (struct channel *) chptr_v;

	eventAddOnce("cf_find_oppless_chans", e_find_oppless_channels, NULL, 300);
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

	if((chptr = find_channel(parv[0])) && dlink_find(chanfix_p, &chptr->services))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_ALREADYONCHANNEL,
				chanfix_p->name, chptr->name);
		return 0;
	}

	zlog(chanfix_p, 1, WATCH_OPERBOT, 1, client_p, conn_p,
		"CFJOIN %s", parv[0]);

	tsinfo = chptr != NULL ? chptr->tsinfo : CURRENT_TIME;

	/*rsdb_exec(NULL, "INSERT INTO operbot (chname, tsinfo, oper) VALUES(LOWER('%Q'), '%lu', '%Q')",
			parv[0], tsinfo, OPER_NAME(client_p, conn_p));
	*/

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
				parv[0]);
		*/
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
	dlink_node *ptr;
	struct chmember *msptr;

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_NOSUCHCHANNEL, parv[0]);
		return 0;
	}

	if(dlink_list_length(&chptr->users) < config_file.cf_min_clients)
	{
		/* Not enough users in this channel */
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOTENOUGHUSERS, parv[0]);
		return 0;
	}

	/* Check if anyone is opped, if they are we don't do anything unless someone forced us. */
	if(dlink_list_length(&chptr->users_opped) > 0)
	{
		/* Check whether the OVERRIDE flag has been given */
		if (parc > 1)
		{
			if (!irccmp(parv[1], "override") || parv[1][0] == '!')
			{
				if(chptr->tsinfo < 2)
				{
					service_send(chanfix_p, client_p, conn_p,
									"Channel '%s' TS too low for takeover", parv[0]);
					return 0;
				}
				/* override command has been given, so we should take-over the channel */
				chan_takeover(chptr, 0);
				override = 1;
				service_err_chan(chanfix_p, chptr, SVC_CF_CHANFIXINPROG);
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

	chan_remove_modes(chptr, '0');
	chan_remove_bans(chptr, '0');
 
	service_snd(chanfix_p, client_p, conn_p, SVC_ISSUEDFORBY,
				chanfix_p->name, "CHANFIX", parv[0], client_p->name);

	/* for now we'll just op everyone */
	modebuild_start(chanfix_p, chptr);
	/* Do I need to use DLINK_FOREACH_SAFE here? */
	DLINK_FOREACH(ptr, chptr->users_unopped.head)
	{
		msptr = ptr->data;
		op_chmember(msptr);
		modebuild_add(DIR_ADD, "o", msptr->client_p->name);
	}
	modebuild_finish();

	/* Once the channel has been taken over, we should add it to the list of channels that need
	 * to be fixed based on their scores.
	 */

	part_service(chanfix_p, parv[0]);

	return 0;
}



static int
o_chanfix_check(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	int users, ops;

	if(!valid_chname(parv[0]))
	{
		service_err(chanfix_p, client_p, SVC_IRC_CHANNELINVALID, chanfix_p->name, parv[0]);
		return 0;
	}
	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_err(chanfix_p, client_p, SVC_IRC_NOSUCHCHANNEL, chanfix_p->name, parv[0]);
		return 0;
	}

	service_snd(chanfix_p, client_p, conn_p, SVC_ISSUEDFORBY,
				chanfix_p->name, "CHECK", parv[0], client_p->name);

	ops = dlink_list_length(&chptr->users_opped);
	users = dlink_list_length(&chptr->users);
	service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHECK, parv[0], ops, users);

	return 0;
}


static int
o_chanfix_set(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	/* TODO: Make these messages into translation strings when we're sure about what
	 * they're going to be. If rserv has a general /SET command like IRCD does, we
	 * should move the min_servers and min_users into that because these may become
	 * generic options not necessarily specific to chanfix alone.
	 */
	int num;

	if(!irccmp(parv[0], "min_servers"))
	{
		if(parc > 1)
		{
			num = atoi(parv[1]);
			if(num > 0)
			{
				config_file.min_servers = num;
				service_err(chanfix_p, client_p, SVC_OPTIONSETTO, chanfix_p->name,
						"SET", "min_servers", parv[1]);
			}
			else
			{
				service_err(chanfix_p, client_p, SVC_PARAMINVALID,
						chanfix_p->name, "SET::min_servers");
			}
		}
		else
		{
			/* Instead of returning an error here, we could show the current value */
			service_err(chanfix_p, client_p, SVC_NEEDMOREPARAMS, chanfix_p->name, "SET::min_servers");
		}
	}
	else if(!irccmp(parv[0], "min_users"))
	{
		if(parc > 1)
		{
			num = atoi(parv[1]);
			if(num > 0)
			{
				config_file.min_users = num;
				service_err(chanfix_p, client_p, SVC_OPTIONSETTO, chanfix_p->name,
						"SET", "min_users", parv[1]);
			}
			else
			{
				service_err(chanfix_p, client_p, SVC_PARAMINVALID,
						chanfix_p->name, "SET::min_users");
			}
		}
		else
		{
			/* Instead of returning an error here, we could show the current value */
			service_err(chanfix_p, client_p, SVC_NEEDMOREPARAMS, chanfix_p->name, "SET::min_users");
		}
	}
	else if(!irccmp(parv[0], "autofix"))
	{
		if(parc > 1)
		{
			if(!irccmp(parv[1], "on") || !irccmp(parv[0], "yes"))
			{
				config_file.cf_enable_autofix = 1;
				service_err(chanfix_p, client_p, SVC_OPTIONSETTO, chanfix_p->name,
						"SET", "autofix", "enabled");
			}
			else if(!irccmp(parv[1], "off") || !irccmp(parv[0], "no"))
			{
				service_err(chanfix_p, client_p, SVC_OPTIONSETTO, chanfix_p->name,
						"SET", "autofix", "disabled");
				config_file.cf_enable_autofix = 0;
			}
			else
			{
				service_err(chanfix_p, client_p, SVC_PARAMINVALID,
						chanfix_p->name, "SET::autofix");
			}
		}
		else
		{
			/* Instead of returning an error here, we should show the current value */
			service_err(chanfix_p, client_p, SVC_NEEDMOREPARAMS, chanfix_p->name, "SET::autofix");
		}
	}
	else if(!irccmp(parv[0], "chanfix"))
	{
		if(parc > 1)
		{
			if(!irccmp(parv[1], "on") || !irccmp(parv[0], "yes"))
			{
				config_file.cf_enable_chanfix = 1;
				service_err(chanfix_p, client_p, SVC_OPTIONSETTO, chanfix_p->name,
						"SET", "chanfix", "enabled");
			}
			else if(!irccmp(parv[1], "off") || !irccmp(parv[0], "no"))
			{
				service_err(chanfix_p, client_p, SVC_OPTIONSETTO, chanfix_p->name,
						"SET", "chanfix", "disabled");
				config_file.cf_enable_chanfix = 0;
			}
			else
			{
				service_err(chanfix_p, client_p, SVC_PARAMINVALID,
						chanfix_p->name, "SET::chanfix");
			}
		}
		else
		{
			service_err(chanfix_p, client_p, SVC_NEEDMOREPARAMS, chanfix_p->name, "SET::chanfix");
		}
	}
	else
	{
		service_err(chanfix_p, client_p, SVC_OPTIONINVALID, chanfix_p->name, "SET");
	}
		
	return 0;
}

static int
o_chanfix_status(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	service_send(chanfix_p, client_p, conn_p, "Chanfix module status:");

	if(config_file.cf_enable_autofix)
		service_send(chanfix_p, client_p, conn_p, "Automatic channel fixing enabled.");
	else
		service_send(chanfix_p, client_p, conn_p, "Automatic channel fixing disabled.");

	if(config_file.cf_enable_chanfix)
		service_send(chanfix_p, client_p, conn_p, "Manual channel fixing enabled.");
	else
		service_send(chanfix_p, client_p, conn_p, "Manual channel fixing disabled.");

	/*service_send(chanfix_p, client_p, conn_p,
		"For scoring/fixing, chanfix requires that at least %d out of %d network servers be linked (~%d percent).",
			(config_file.cf_min_server_percent * config_file.cf_network_servers) / 100 + 1,
			config_file.cf_network_servers, config_file.cf_min_server_percent);
	*/

	if(is_network_split())
		service_send(chanfix_p, client_p, conn_p, "Splitmode active. Channel scoring/fixing disabled.");
	else
		service_send(chanfix_p, client_p, conn_p, "Splitmode not active.");
		
	return 0;
}

/* General function to manage how we iterate over all the channels
 * gathering score data.
 */
static void
e_gather_channels(void)
{
	/*DAYS_SINCE_EPOCH config_file.cf_min_clients */

	/* Rorate the scores if it's midnight. */




}

/* Process the score data for a bucket of channels. A bucket is a subset
 * of the total network channels.
 */
static void
gather_channel_bucket(void)
{


}

/* Removes modes from a channel that might be preventing regular users
 * from getting in.
 */
static int
chan_remove_modes(struct channel *chptr, char showmsg)
{
	/* TODO: Try making this function more like chanserv::clearmodes */
	char modelist[] = "ilkrS", flag = '0';
	int i;
	int masklist[] = {
		MODE_INVITEONLY,
		MODE_LIMIT,
		MODE_KEY,
		MODE_REGONLY,
		MODE_SSLONLY
	};

	modebuild_start(chanfix_p, chptr);

	for(i = 0; i < sizeof(modelist)-1; i++)
	{
		if(chptr->mode.mode & masklist[i])
		{
			modebuild_add(DIR_DEL, &modelist[i], NULL);
			chptr->mode.mode &= ~masklist[i];
			flag = '1';

			if(masklist[i] == MODE_KEY)
			{
				chptr->mode.key[0] = '\0';
			}
			else if(masklist[i] == MODE_LIMIT)
			{
				chptr->mode.limit = 0;
			}
		}
	}

	if(flag == '1')
	{
		join_service(chanfix_p, chptr->name, chptr->tsinfo, NULL, 0);
		modebuild_finish();
		if(showmsg == '1')
		{
			service_err_chan(chanfix_p, chptr, SVC_CF_MODESREMOVED);
		}
		return 1;
	}

	return 0;
}

/* Remove only the bans from a channel in case they're preventing people
 * from getting in.
 */
static int
chan_remove_bans(struct channel *chptr, char showmsg)
{
	dlink_node *ptr, *next_ptr;

	if(dlink_list_length(&chptr->bans) < 1)
	{
		return 0;
	}

	join_service(chanfix_p, chptr->name, chptr->tsinfo, NULL, 0);

	modebuild_start(chanfix_p, chptr);

	DLINK_FOREACH(ptr, chptr->bans.head)
	{
		modebuild_add(DIR_DEL, "b", ptr->data);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->bans.head)
	{
		my_free(ptr->data);
		dlink_destroy(ptr, &chptr->bans);
	}

	/* apply the bans removal */
	modebuild_finish();

	if(showmsg == '1')
	{
		service_err_chan(chanfix_p, chptr, SVC_CF_BANSREMOVED, chptr->name);
	}

	return 1;
}

static void
e_find_oppless_channels(void)
{
	struct channel *chptr;
	dlink_node *ptr;
	int min_clients;

	if(is_network_split() || !config_file.cf_enable_autofix)
	{
		return;
	}

	min_clients = config_file.cf_min_clients;

	DLINK_FOREACH(ptr, channel_list.head)
	{
		chptr = ptr->data;

		if((dlink_list_length(&chptr->users) >= min_clients) &&
					(chptr->users_opped.head == NULL))
		{
			/* Add this channel to the channel_autofix_list if:
				- The channel doesn't have a 'suspend' time.
				- The channel is not registered with ChanServ.
				- The channel has scores in the DB.
				- A client is present in the channel that has a score higher than the
				  minimum absolute required for opping.
				- The channel is not set as blocked.
				- The channel is not already being fixed (automatically or manually).

			is_chan_being_fixed()

			*/
		}
	}
}

static void
e_fix_chanfix_channels(void)
{
}

static void
e_fix_autofix_channels(void)
{
}

static void
e_rotate_chanfix_db(void)
{
	/* stuff we need to do at midnight. like clear out the old daysample scores
	 * from the DB, or update some variables etc.
	 */
	/* Re-add this event once we're done
	eventAddOnce("cf_rotate_cf_db", e_rotate_chanfix_db, NULL, seconds_to_midnight());
	*/
}

static time_t
seconds_to_midnight(void)
{
	struct tm *t_info;
	time_t nowtime = CURRENT_TIME;
	t_info = gmtime(&nowtime);
	return 86400 - (t_info->tm_hour * 3600 + t_info->tm_min * 60 + t_info->tm_sec);
}

static int
add_chanfix(struct channel *chptr)
{
	struct chanfix_channel *af_chan;

	if(dlink_find(chptr, &chanfix_list))
		return 0;

	af_chan = my_calloc(1, sizeof(struct chanfix_channel));
	af_chan->chptr = chptr;
	af_chan->flags |= CF_STATUS_AUTOFIX;
	af_chan->time_fix_started = CURRENT_TIME;
	af_chan->time_prev_attempt = 0;

	/* We should either:
	 *	- Get the highest chanop score so we can set highest_score, or
	 *  - Fetch all the chanops from the DB we can consider for opping and store
	 *    them in the autofix_channel struct so we don't have to keep querying
	 *    the DB.
	 */
	/*af_chan->hightest_score = */

	/* Also record an entry in the fixhistory table to say we're autofixing
	 * this channel.
	 */
	dlink_add(chptr, &af_chan->node, &chanfix_list);
	chptr->cfptr = af_chan;

	return 1;
}

static void 
del_chanfix(struct channel *chptr)
{
	struct chanfix_channel *cfptr;

	cfptr = (struct chanfix_channel *) chptr->cfptr;
	dlink_delete(&cfptr->node, &chanfix_list);
	my_free(cfptr);
	chptr->cfptr = NULL;
}


#endif
