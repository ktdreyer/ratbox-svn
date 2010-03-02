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
#include "event.h"
#ifdef ENABLE_CHANSERV
#include "s_chanserv.h"
#endif

/* Please note that this CHANFIX module is very much a work in progress,
 * and that it is likely to change frequently & dramatically whilst being
 * developed. Thanks.
 */

static void init_s_chanfix(void);

static struct client *chanfix_p;

static dlink_list chanfix_list;

static int o_chanfix_cfjoin(struct client *, struct lconn *, const char **, int);
static int o_chanfix_cfpart(struct client *, struct lconn *, const char **, int);
static int o_chanfix_score(struct client *, struct lconn *, const char **, int);
static int o_chanfix_userlist(struct client *, struct lconn *, const char **, int);
static int o_chanfix_chanfix(struct client *, struct lconn *, const char **, int);
static int o_chanfix_revert(struct client *, struct lconn *, const char **, int);
static int o_chanfix_check(struct client *, struct lconn *, const char **, int);
static int o_chanfix_set(struct client *, struct lconn *, const char **, int);
static int o_chanfix_status(struct client *, struct lconn *, const char **, int);

static struct service_command chanfix_command[] =
{
	{ "CFJOIN",	&o_chanfix_cfjoin,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "CFPART",	&o_chanfix_cfpart,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "SCORE",	&o_chanfix_score,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "CHANFIX",	&o_chanfix_chanfix,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "REVERT",	&o_chanfix_revert,	1, NULL, 1, 0L, 0, 1, 0 },
	/*{ "HISTORY",	&o_chanfix_history,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "INFO",	&o_chanfix_info,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "ADDNOTE",	&o_chanfix_addnote,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "DELNOTE",	&o_chanfix_delnote,	1, NULL, 1, 0L, 0, 1, 0 },*/
	{ "SET",	&o_chanfix_set,	1, NULL, 1, 0L, 0, 1, 0 },
	/*{ "BLOCK",	&o_chanfix_block,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "UNBLOCK",	&o_chanfix_unblock,	1, NULL, 1, 0L, 0, 1, 0 },*/
	{ "CHECK",	&o_chanfix_check,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "USERLIST",	&o_chanfix_userlist,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "STATUS",	&o_chanfix_status,	0, NULL, 1, 0L, 0, 1, 0 }
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
static int h_chanfix_channel_oppless(void *chptr_v, void *unused);

static void e_chanfix_score_channels(void *unused);
static void e_chanfix_collate_history(void *unused);
static void e_chanfix_autofix_channels(void *unused);

/* Internal chanfix functions */
static void chan_takeover(struct channel *);
static unsigned long get_userhost_id(const char *);
static unsigned long get_channel_id(const char *);
static time_t seconds_to_midnight(void);
static struct chanfix_score * get_all_cf_scores(struct channel *, int, int);
static struct chanfix_score * get_chmember_cf_scores(struct channel *, short, int);
static void process_chanfix_list(int);

static int add_chanfix(struct channel *chptr, short man_fix);
static void del_chanfix(struct channel *chptr);

void
preinit_s_chanfix(void)
{
	chanfix_p = add_service(&chanfix_service);
}

static void
init_s_chanfix(void)
{
	hook_add(h_chanfix_channel_destroy, HOOK_CHANNEL_DESTROY);
	/*hook_add(h_chanfix_channel_oppless, HOOK_CHANNEL_OPPLESS);*/

	eventAdd("e_chanfix_score_channels", e_chanfix_score_channels, NULL, 300);
	/*eventAdd("e_chanfix_autofix_channels", e_chanfix_autofix_channels, NULL, 300);*/
	eventAddOnce("e_chanfix_collate_history", e_chanfix_collate_history, NULL,
				seconds_to_midnight()+5);
}

/* preconditions: TS >= 2 and there is at least one user in the channel */
static void
chan_takeover(struct channel *chptr)
{
	dlink_node *ptr;

	part_service(chanfix_p, chptr->name);

	remove_our_simple_modes(chptr, NULL, 0);
	remove_our_ov_modes(chptr);

	remove_our_bans(chptr, NULL, 1, 1, 1);

	chptr->mode.mode = MODE_TOPIC|MODE_NOEXTERNAL;

	chptr->tsinfo--;

	join_service(chanfix_p, chptr->name, chptr->tsinfo, NULL, 0);

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

/* Fetch the userhost_id of a given userhost from the DB. */
static unsigned long
get_userhost_id(const char *userhost)
{
	unsigned long userhost_id;
	struct rsdb_table data;

	rsdb_exec_fetch(&data, "SELECT id FROM cf_userhost WHERE userhost=LOWER('%Q')",
						userhost);

	if((data.row_count > 0) && (data.row[0][0] != NULL))
	{
		userhost_id = atoi(data.row[0][0]);
	}
	else
	{
		userhost_id = 0;
	}

	rsdb_exec_fetch_end(&data);

	return userhost_id;
}

/* Fetch the channel_id of a given channel name from the DB. */
static unsigned long
get_channel_id(const char *channel)
{
	unsigned long channel_id;
	struct rsdb_table data;

	rsdb_exec_fetch(&data, "SELECT id FROM cf_channel WHERE chname=LOWER('%Q')",
						channel);

	if((data.row_count > 0) && (data.row[0][0] != NULL))
	{
		channel_id = atoi(data.row[0][0]);
	}
	else
	{
		channel_id = 0;
	}

	rsdb_exec_fetch_end(&data);

	return channel_id;
}


/* Return an array of chanfix_score_item structures for each chanop stored in
 * the DB for the specified channel.
 */
static struct chanfix_score *
get_all_cf_scores(struct channel *chptr, int max_num, int min_score)
{
	struct chanfix_score *scores;
	unsigned long channel_id;
	struct rsdb_table data;
	int i;

	if((channel_id = get_channel_id(chptr->name)) == 0)
	{
		/* Channel has no ID in the DB and therefore can't have any scores. */
		return NULL;
	}

	if(min_score > 0)
	{
		rsdb_exec_fetch(&data, "SELECT userhost_id, SUM(total) "
			"FROM "
			"  (SELECT cf_score.userhost_id, count(*) AS total "
			"  FROM cf_score WHERE channel_id = '%lu' "
			"  GROUP BY cf_score.userhost_id "
			"  UNION ALL "
			"  SELECT cf_score_history.userhost_id, SUM(score) AS total "
			"  FROM cf_score_history WHERE channel_id = '%lu' "
			"  GROUP BY cf_score_history.userhost_id) AS total_table "
			"WHERE total > %d "
			"GROUP BY total_table.userhost_id " 
			"ORDER BY total DESC", 
			channel_id, channel_id, min_score);
	}
	else
	{
		rsdb_exec_fetch(&data, "SELECT userhost_id, SUM(total) "
			"FROM "
			"  (SELECT cf_score.userhost_id, count(*) AS total "
			"  FROM cf_score WHERE channel_id = '%lu' "
			"  GROUP BY cf_score.userhost_id "
			"  UNION ALL "
			"  SELECT cf_score_history.userhost_id, SUM(score) AS total "
			"  FROM cf_score_history WHERE channel_id = '%lu' "
			"  GROUP BY cf_score_history.userhost_id) AS total_table "
			"GROUP BY total_table.userhost_id " 
			"ORDER BY total DESC", 
			channel_id, channel_id);
	}

	if(data.row_count == 0 || data.row[0][0] == NULL)
	{
		rsdb_exec_fetch_end(&data);
		return NULL;
	}

	scores = my_malloc(sizeof(struct chanfix_score));

	if(max_num < 1)
		max_num = data.row_count;

	if(max_num > data.row_count)
		max_num = data.row_count;

	scores->length = max_num;
	scores->score_items = my_malloc(sizeof(struct chanfix_score_item) * max_num);

	for(i = 0; i < max_num; i++)
	{
		scores->score_items[i].userhost_id = atoi(data.row[i][0]);
		scores->score_items[i].score = atoi(data.row[i][1]);
	}

	rsdb_exec_fetch_end(&data);

	return scores;
}

/* Return an array of chanfix_score_item structures for each chanop stored in
 * the DB for the specified channel.
 */
static struct chanfix_score *
get_chmember_cf_scores(struct channel *chptr, short status, int max_num)
{
	struct chanfix_score *scores;
	unsigned long userhost_id;
	struct chmember *msptr;
	dlink_node *ptr;
	dlink_list *listptr;
	char userhost[USERLEN+HOSTLEN+4+1];
	unsigned int count, i, user_count;

	if(status == 2)
	{
		count = dlink_list_length(&chptr->users);
		listptr = &chptr->users;
	}
	else if(status == 1)
	{
		count = dlink_list_length(&chptr->users_opped);
		listptr = &chptr->users_opped;
	}
	else
	{
		count = dlink_list_length(&chptr->users_unopped);
		listptr = &chptr->users_unopped;
	}

	if(count < 1)
		return NULL;

	if((scores = get_all_cf_scores(chptr, max_num, 0)) == NULL)
		return NULL;

	user_count = 0;
	DLINK_FOREACH(ptr, listptr->head)
	{
		msptr = ptr->data;

		snprintf(userhost, sizeof(userhost), "%s@%s",
				msptr->client_p->user->username,
				msptr->client_p->user->host);

		userhost_id = get_userhost_id(userhost);

		for(i = 0; (i < scores->length) && (user_count < max_num); i++)
		{
			if(scores->score_items[i].userhost_id == userhost_id)
			{
				/* Check to see if msptr is NULL so this function call doesn't
				 * count duplicate user@hosts.
				 */
				if(scores->score_items[i].msptr == NULL)
				{
					scores->score_items[i].msptr = msptr;
					user_count++;
					break;
				}
				else
					break;
			}
		}

		if((user_count >= max_num) || (user_count >= scores->length))
			break;
	}

	return scores;
}

/* Function for collecting chanop scores for a given channel.
 */
static void
collect_channel_scores(struct channel *chptr, time_t timestamp, unsigned int dayts)
{
	struct chmember *msptr;
	dlink_node *ptr;
	char userhost[USERLEN+HOSTLEN+2];

	DLINK_FOREACH(ptr, chptr->users_opped.head)
	{
		msptr = ptr->data;

		snprintf(userhost, sizeof(userhost), "%s@%s",
				msptr->client_p->user->username,
				msptr->client_p->user->host);

		rsdb_exec(NULL, "INSERT INTO cf_temp_score (chname, userhost, timestamp, dayts) "
						"VALUES(LOWER('%Q'), LOWER('%Q'), '%lu', '%lu')",
						chptr->name, userhost, timestamp, dayts);
	}
}

/* Iterate over the chanfix_list and perform chanfixing for the specified
 * fix_type. This will be either CF_STATUS_AUTOFIX or CF_STATUS_MANUALFIX.
 */
static void 
process_chanfix_list(int fix_type)
{
	struct chanfix_channel *cf_ch;
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, chanfix_list.head)
	{
		cf_ch = ptr->data;

		if(cf_ch->flags & fix_type)
		{
			if(dlink_list_length(&cf_ch->chptr->users) < config_file.cf_min_clients)
			{
				del_chanfix(cf_ch->chptr);
				mlog("debug: Channel '%s' has insufficient users for a fix",
					cf_ch->chptr->name);
			}

			if(dlink_list_length(&cf_ch->chptr->users_opped) >= CF_MIN_FIX_OPS)
			{
				del_chanfix(cf_ch->chptr);
				mlog("debug: Channel '%s' has enough ops (fix complete).",
					cf_ch->chptr->name);
			}

			if(cf_ch->highest_score <=
					(CF_MIN_ABS_CHAN_SCORE_END * CF_MAX_CHANFIX_SCORE))
			{
				del_chanfix(cf_ch->chptr);
				mlog("debug: Cannot fix channel '%s' (highest user score is too low).");
			}

		}
	}


	/*op_chmember(msptr);
	modebuild_add(DIR_ADD, "o", msptr->client_p->name);

	cf_chan = my_calloc(1, sizeof(struct chanfix_channel));
	cf_chan->chptr = chptr;
	cf_chan->time_fix_started = CURRENT_TIME;
	cf_chan->time_prev_attempt = 0;
	*/


	/* Also record an entry in the fixhistory table to say we're autofixing
	 * this channel.
	 */
	/*dlink_add(chptr, &cf_chan->node, &chanfix_list);
	chptr->cfptr = cf_chan;
	*/
}

/* General function to manage how we iterate over all the channels
 * gathering score data.
 */
static void 
e_chanfix_score_channels(void *unused)
{
	struct channel *chptr;
	dlink_node *ptr;
	time_t min_ts, max_ts, timestamp = CURRENT_TIME;
	unsigned int dayts = DAYS_SINCE_EPOCH;
	short i = 0;
	struct rsdb_table ts_data;

	DLINK_FOREACH(ptr, channel_list.head)
	{
		chptr = ptr->data;

#ifdef ENABLE_CHANSERV
		/* Need to skip the channel if it's registered with ChanServ.
		 * If scoring channels isn't very resource intensive, it might be worth
		 * scoring ChanServ channels anyway (or making it a config file option)
		 * to avoid the string comparisons.
		 */
		if(find_channel_reg(NULL, chptr->name))
			continue;
#endif

		if((dlink_list_length(&chptr->users) >= config_file.cf_min_clients) &&
				(&chptr->users_opped.head != NULL))
		{
			mlog("debug: Scoring opped clients in: %s", chptr->name);
			collect_channel_scores(chptr, timestamp, dayts);
		}
	}

	/* Done collecting chanops, execute the collation routine.
	 * As a basic sanity check, don't do more than 5 of these at a time.
	 */
	while (i < 5)
	{
		rsdb_exec_fetch(&ts_data, "SELECT MIN(timestamp), MAX(timestamp) FROM cf_temp_score");

		if(ts_data.row_count == 0)
		{
			mlog("warning: Unable to retrieve min timestamp for ChanFix collation.");
			rsdb_exec_fetch_end(&ts_data);
			return;
		}

		min_ts = atoi(ts_data.row[0][0]);
		max_ts = atoi(ts_data.row[0][1]);
		rsdb_exec_fetch_end(&ts_data);

		if(min_ts == max_ts)
			break;

		/* Using the min_ts timestamp, select the distinct channels and
		 * userhosts from the temporary table.
		 */

		rsdb_exec(NULL, "INSERT INTO cf_channel (chname) "
					"SELECT DISTINCT cf_temp_score.chname FROM cf_temp_score "
					"LEFT JOIN cf_channel ON cf_temp_score.chname=cf_channel.chname "
					"WHERE cf_channel.id IS NULL");

		rsdb_exec(NULL, "INSERT INTO cf_userhost (userhost) "
					"SELECT DISTINCT cf_temp_score.userhost FROM cf_temp_score "
					"LEFT JOIN cf_userhost ON cf_temp_score.userhost=cf_userhost.userhost "
					"WHERE cf_userhost.id IS NULL");

		rsdb_exec(NULL, "INSERT INTO cf_score (channel_id, userhost_id, timestamp, dayts) "
				"SELECT DISTINCT cf_channel.id, cf_userhost.id, timestamp, dayts "
				"FROM cf_temp_score LEFT JOIN cf_channel ON cf_temp_score.chname=cf_channel.chname "
				"LEFT JOIN cf_userhost ON cf_temp_score.userhost=cf_userhost.userhost "
				"WHERE timestamp='%lu'", min_ts);

		/* Delete these timestamp entries when we're done. */
		rsdb_exec(NULL, "DELETE FROM cf_temp_score WHERE timestamp='%lu'", min_ts);

		i++;
	}
}

/* Collate the cf_score data into cf_score_history. */
static void 
e_chanfix_collate_history(void *unused)
{
	unsigned int min_dayts;
	struct rsdb_table ts_data;
	short i = 0;

	/* As a basic sanity check, don't do more than 10 of these at a time. */
	while (i < 10)
	{
		rsdb_exec_fetch(&ts_data, "SELECT MIN(dayts) FROM cf_score");

		if(ts_data.row_count == 0)
		{
			mlog("warning: Unable to retrieve min timestamp for ChanFix collation.");
			rsdb_exec_fetch_end(&ts_data);
			return;
		}

		if(ts_data.row[0][0] != NULL)
			min_dayts = atoi(ts_data.row[0][0]);
		else
			min_dayts = DAYS_SINCE_EPOCH;

		rsdb_exec_fetch_end(&ts_data);

		if(min_dayts == DAYS_SINCE_EPOCH)
		{
			mlog("info: History successfully collated.");
			break;
		}

		mlog("info: Collating score history for dayts: %d", min_dayts);

		rsdb_exec(NULL, "INSERT INTO cf_score_history (channel_id, userhost_id, dayts, score) "
				"SELECT channel_id, userhost_id, dayts, count(*) "
				"FROM cf_score where dayts = '%u' "
				"GROUP BY channel_id, userhost_id",
				min_dayts);

		/* Delete these day's entries when we're done. */
		rsdb_exec(NULL, "DELETE FROM cf_score WHERE dayts='%lu'", min_dayts);

		i++;
	}

	eventAddOnce("e_chanfix_collate_history", e_chanfix_collate_history, NULL,
				seconds_to_midnight()+5);

	/* Drop old history data from the cf_score_history table. */
	mlog("info: Dropping old daysample history.");

	rsdb_exec(NULL, "DELETE FROM cf_score_history WHERE dayts < '%lu'",
				(DAYS_SINCE_EPOCH - CF_DAYSAMPLES));
}

static void
e_chanfix_autofix_channels(void *unused)
{
	if(!is_network_split())
		process_chanfix_list(CF_STATUS_AUTOFIX);
}


static int
h_chanfix_channel_destroy(void *chptr_v, void *unused)
{
	struct channel *chptr = (struct channel *) chptr_v;

	if(chptr->cfptr)
		del_chanfix(chptr);

	return 0;
}

static int
h_chanfix_channel_oppless(void *chptr_v, void *unused)
{
	struct channel *chptr = (struct channel *) chptr_v;

	if(!chptr->cfptr)
		add_chanfix(chptr, 0);

	return 0;
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
o_chanfix_score(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	char buf[BUFSIZE], t_buf[8];
	int i;
	struct chanfix_score *scores;

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

#ifdef ENABLE_CHANSERV
	if(find_channel_reg(NULL, chptr->name))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHANSERVCHANNEL, parv[0]);
		return 0;
	}
#endif

	/* get all scores. */
	if(scores = get_all_cf_scores(chptr, config_file.cf_num_top_scores, 0))
	{
		mlog("debug: generate string buf for all users.");
		buf[0] = '\0';
		for(i = 0; i < scores->length; i++)
		{
			snprintf(t_buf, sizeof(t_buf), "%d ", scores->score_items[i].score);
			strcat(buf, t_buf);
		}
		service_send(chanfix_p, client_p, conn_p,
			"Top %d scores for channel '%s':", config_file.cf_num_top_scores, parv[0]);
		service_send(chanfix_p, client_p, conn_p, "%s", buf);

		my_free(scores->score_items);
		my_free(scores);
		scores = NULL;
	}
	else
	{
		service_send(chanfix_p, client_p, conn_p,
					"Channel '%s' has no score data.", parv[0]);
		return 0;
	}

	/* show the opped userhost / score data. */
	service_send(chanfix_p, client_p, conn_p,
		"Top %d scores for ops in channel '%s':", config_file.cf_num_top_scores, parv[0]);
	if(scores = get_chmember_cf_scores(chptr, 1, config_file.cf_num_top_scores))
	{
		mlog("debug: generate string buf for opped users.");
		buf[0] = '\0';
		for(i = 0; i < scores->length; i++)
		{
			if(scores->score_items[i].msptr)
			{
				snprintf(t_buf, sizeof(t_buf), "%d ", scores->score_items[i].score);
				strcat(buf, t_buf);
			}
		}
		service_send(chanfix_p, client_p, conn_p, "%s", buf);

		my_free(scores->score_items);
		my_free(scores);
		scores = NULL;
	}
	else
	{
		service_send(chanfix_p, client_p, conn_p, "No opped scores.");
	}

	mlog("debug: querying for unopped users.");
	/* get the unopped userhost / score data. */
	service_send(chanfix_p, client_p, conn_p,
		"Top %d scores for non-ops in channel '%s':", config_file.cf_num_top_scores, parv[0]);
	if(scores = get_chmember_cf_scores(chptr, 0, config_file.cf_num_top_scores))
	{
		mlog("debug: generate string buf for unopped users.");
		buf[0] = '\0';
		for(i = 0; i < scores->length; i++)
		{
			if(scores->score_items[i].msptr)
			{
				snprintf(t_buf, sizeof(t_buf), "%d ", scores->score_items[i].score);
				strcat(buf, t_buf);
			}
		}
		service_send(chanfix_p, client_p, conn_p, "%s", buf);

		my_free(scores->score_items);
		my_free(scores);
		scores = NULL;
	}
	else
	{
		service_send(chanfix_p, client_p, conn_p, "No non-op scores.");
	}

	return 0;
}


static int
o_chanfix_userlist(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	char buf[BUFSIZE];
	int i;
	struct chanfix_score *scores;

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

#ifdef ENABLE_CHANSERV
	if(find_channel_reg(NULL, chptr->name))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHANSERVCHANNEL, parv[0]);
		return 0;
	}
#endif


	if(scores = get_chmember_cf_scores(chptr, 2, config_file.cf_num_top_scores))
	{
		service_send(chanfix_p, client_p, conn_p,
			"Top %d users for channel '%s':", config_file.cf_num_top_scores, parv[0]);
		for(i = 0; i < scores->length; i++)
		{
			if(scores->score_items[i].msptr)
			{
				snprintf(buf, sizeof(buf), "%4d  %s!%s@%s", scores->score_items[i].score,
					scores->score_items[i].msptr->client_p->name,
					scores->score_items[i].msptr->client_p->user->username,
					scores->score_items[i].msptr->client_p->user->host);
				service_send(chanfix_p, client_p, conn_p, "%s", buf);
			}
		}

		my_free(scores->score_items);
		my_free(scores);
		scores = NULL;
	}
	else
	{
		service_send(chanfix_p, client_p, conn_p,
				"Channel '%s' has no score data.", parv[0]);
		return 0;
	}

	return 0;
}

static int
o_chanfix_chanfix(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	int override = 0;
	dlink_node *ptr, *next_ptr;
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

#ifdef ENABLE_CHANSERV
	if(find_channel_reg(NULL, chptr->name))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHANSERVCHANNEL, parv[0]);
		return 0;
	}
#endif

	if(dlink_list_length(&chptr->users) < config_file.cf_min_clients)
	{
		/* Not enough users in this channel */
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOTENOUGHUSERS, parv[0]);
		return 0;
	}

	/* Check if anyone is opped, if they are we don't do anything. */
	if(dlink_list_length(&chptr->users_opped) > 0)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_HASOPPEDUSERS, parv[0]);
		return 0;
	}

	/* Need to check to see if this channel has an ALERT set for it. */

	service_err_chan(chanfix_p, chptr, SVC_CF_CHANFIXINPROG);

	/*if(!dlink_find(chanfix_p, &chptr->services))*/

	join_service(chanfix_p, parv[0], chptr->tsinfo, NULL, 0);

	remove_our_simple_modes(chptr, chanfix_p, 1);
	remove_our_bans(chptr, chanfix_p, 1, 0, 0);
 
	service_snd(chanfix_p, client_p, conn_p, SVC_ISSUEDFORBY,
				chanfix_p->name, "CHANFIX", parv[0], client_p->name);

	/* for testing we'll just op everyone */
	modebuild_start(chanfix_p, chptr);
	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->users_unopped.head)
	{
		msptr = ptr->data;
		op_chmember(msptr);
		modebuild_add(DIR_ADD, "o", msptr->client_p->name);
	}
	modebuild_finish();

	part_service(chanfix_p, parv[0]);

	/*add_chanfix(chptr, 1);*/

	return 0;
}

static int
o_chanfix_revert(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	int override = 0;
	dlink_node *ptr, *next_ptr;
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

#ifdef ENABLE_CHANSERV
	if(find_channel_reg(NULL, chptr->name))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHANSERVCHANNEL, parv[0]);
		return 0;
	}
#endif

	if(dlink_list_length(&chptr->users) < config_file.cf_min_clients)
	{
		/* Not enough users in this channel */
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOTENOUGHUSERS, parv[0]);
		return 0;
	}

	if(chptr->tsinfo < 2)
	{
		service_send(chanfix_p, client_p, conn_p,
						"Channel '%s' TS too low for takeover", parv[0]);
		return 0;
	}

	/* Everything looks okay, execute the takeover. */
	chan_takeover(chptr);
	service_err_chan(chanfix_p, chptr, SVC_CF_CHANFIXINPROG);

	service_snd(chanfix_p, client_p, conn_p, SVC_ISSUEDFORBY,
				chanfix_p->name, "REVERT", parv[0], client_p->name);

	/* for testing we'll just op everyone */
	modebuild_start(chanfix_p, chptr);
	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->users_unopped.head)
	{
		msptr = ptr->data;
		op_chmember(msptr);
		modebuild_add(DIR_ADD, "o", msptr->client_p->name);
	}
	modebuild_finish();

	part_service(chanfix_p, parv[0]);

	/*add_chanfix(chptr, 1);*/

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


static time_t
seconds_to_midnight(void)
{
	struct tm *t_info;
	time_t nowtime = CURRENT_TIME;
	t_info = gmtime(&nowtime);
	return 86400 - (t_info->tm_hour * 3600 + t_info->tm_min * 60 + t_info->tm_sec);
}

static int
add_chanfix(struct channel *chptr, short man_fix)
{
	struct chanfix_channel *cf_chan;

#ifdef ENABLE_CHANSERV
	/* Need to skip the channel if it's registered with ChanServ. */
	if(find_channel_reg(NULL, chptr->name))
		return 0;
#endif

	/* Must check if the channel has any sores in the DB. If not, print a
	 * messages to logged in chanfix operators saying we know the channel
	 * is oppless but cannot fix it, then return 0.
	 */

	/* Need to check to see if the channel is BLOCKed and should be ignored. */

	if(dlink_find(chptr, &chanfix_list))
		return 0;

	cf_chan = my_calloc(1, sizeof(struct chanfix_channel));
	cf_chan->chptr = chptr;
	if(man_fix)
		cf_chan->flags |= CF_STATUS_MANUALFIX;
	else
		cf_chan->flags |= CF_STATUS_AUTOFIX;
	cf_chan->time_fix_started = CURRENT_TIME;
	cf_chan->time_prev_attempt = 0;

	/* We should either:
	 *	- Get the highest chanop score so we can set highest_score, or
	 *  - Fetch all the chanops from the DB we can consider for opping and store
	 *    them in the autofix_channel struct so we don't have to keep querying
	 *    the DB.
	 */
	/*cf_chan->hightest_score = */

	/* Also record an entry in the fixhistory table to say we're autofixing
	 * this channel.
	 */
	dlink_add(chptr, &cf_chan->node, &chanfix_list);
	chptr->cfptr = cf_chan;

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
