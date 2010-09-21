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

/* netsplit warning timestamp. Used to let chanfix know when a server has
 * squit, so it can temporarily ignore opless channels.
 */
static time_t netsplit_warn_ts = 0;

static int o_chanfix_score(struct client *, struct lconn *, const char **, int);
static int o_chanfix_uscore(struct client *, struct lconn *, const char **, int);
static int o_chanfix_userlist(struct client *, struct lconn *, const char **, int);
static int o_chanfix_userlist2(struct client *, struct lconn *, const char **, int);
static int o_chanfix_chanfix(struct client *, struct lconn *, const char **, int);
static int o_chanfix_revert(struct client *, struct lconn *, const char **, int);
static int o_chanfix_check(struct client *, struct lconn *, const char **, int);
static int o_chanfix_block(struct client *, struct lconn *, const char **, int);
static int o_chanfix_unblock(struct client *, struct lconn *, const char **, int);
static int o_chanfix_alert(struct client *, struct lconn *, const char **, int);
static int o_chanfix_unalert(struct client *, struct lconn *, const char **, int);
static int o_chanfix_set(struct client *, struct lconn *, const char **, int);
static int o_chanfix_status(struct client *, struct lconn *, const char **, int);
static int o_chanfix_info(struct client *, struct lconn *, const char **, int);
static int o_chanfix_addnote(struct client *, struct lconn *, const char **, int);
static int o_chanfix_delnote(struct client *, struct lconn *, const char **, int);

static struct service_command chanfix_command[] =
{
	{ "SCORE",	&o_chanfix_score,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "USCORE",	&o_chanfix_uscore,	2, NULL, 1, 0L, 0, 0, CONF_OPER_CF_CHANFIX },
	{ "CHANFIX",	&o_chanfix_chanfix,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_CHANFIX },
	{ "REVERT",	&o_chanfix_revert,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_CHANFIX },
	{ "INFO",	&o_chanfix_info,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "ADDNOTE",	&o_chanfix_addnote,	2, NULL, 1, 0L, 0, 0, CONF_OPER_CF_NOTES },
	{ "DELNOTE",	&o_chanfix_delnote,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_NOTES },
	{ "SET",	&o_chanfix_set,		1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_ADMIN },
	{ "BLOCK",	&o_chanfix_block,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_BLOCK },
	{ "UNBLOCK",	&o_chanfix_unblock,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_BLOCK },
	{ "ALERT",	&o_chanfix_alert,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_NOTES },
	{ "UNALERT",	&o_chanfix_unalert,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_NOTES },
	{ "CHECK",	&o_chanfix_check,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_INFO },
	{ "USERLIST",	&o_chanfix_userlist,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_CHANFIX },
	{ "USERLIST2",	&o_chanfix_userlist2,	1, NULL, 1, 0L, 0, 0, CONF_OPER_CF_CHANFIX },
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
static int h_chanfix_channel_opless(void *chptr_v, void *unused);
static int h_chanfix_server_squit_warn(void *target_p, void *unused);

static void e_chanfix_score_channels(void *unused);
static void e_chanfix_collate_history(void *unused);
static void e_chanfix_autofix_channels(void *unused);
static void e_chanfix_manualfix_channels(void *unused);

/* Internal chanfix functions */
static void chan_takeover(struct channel *);
static unsigned long get_userhost_id(const char *);
static unsigned long get_channel_id(const char *);
static int get_cf_chan_flags(const char *, uint32_t *);
static void add_system_note(const char *, unsigned long, const char *, ...);
static time_t seconds_to_midnight(void);
static struct chanfix_score * get_all_cf_scores(struct channel *, int, int);
static struct chanfix_score * get_chmember_scores(struct chanfix_score *,
			struct channel *, dlink_list *, short);
static void process_chanfix_list(int);

static int add_chanfix(struct channel *, short, struct client *);
static void del_chanfix(struct channel *);

void
preinit_s_chanfix(void)
{
	chanfix_p = add_service(&chanfix_service);
}

static void
init_s_chanfix(void)
{
	hook_add(h_chanfix_channel_destroy, HOOK_CHANNEL_DESTROY);
	hook_add(h_chanfix_channel_opless, HOOK_CHANNEL_OPLESS);
	hook_add(h_chanfix_server_squit_warn, HOOK_SERVER_EXIT_WARNING);

	eventAdd("e_chanfix_score_channels", e_chanfix_score_channels, NULL, 300);
	eventAdd("e_chanfix_autofix_channels", e_chanfix_autofix_channels, NULL, 300);
	eventAdd("e_chanfix_manualfix_channels", e_chanfix_manualfix_channels, NULL, 300);
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

	remove_our_bans(chptr, NULL, 1, 1, 0);

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

/* Fetch a channel's chanfix flags from the DB (if present). */
static int
get_cf_chan_flags(const char *chan, uint32_t *flags)
{
	struct rsdb_table data;

	rsdb_exec_fetch(&data,
		"SELECT flags FROM cf_channel WHERE chname=LOWER('%Q')", chan);

	if(data.row_count > 0)
	{
		*flags = atoi(data.row[0][0]);
	}
	else
	{
		rsdb_exec_fetch_end(&data);
		return 0;
	}

	rsdb_exec_fetch_end(&data);
	return 1;
}

static void
add_system_note(const char *chan, unsigned long chan_id, const char *format, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(chan_id == 0)
		chan_id = get_channel_id(chan);

	if(chan_id > 0)
	{

		rsdb_exec(NULL,
			"INSERT INTO cf_note (channel_id, timestamp, author, text) "
			"VALUES('%lu', '%lu', '%Q', '%Q')",
			chan_id, CURRENT_TIME, chanfix_p->name, buf);
	}
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
		rsdb_exec_fetch(&data, "SELECT userhost_id, SUM(t) AS total "
			"FROM "
			"  (SELECT cf_score.userhost_id, count(*) AS t "
			"  FROM cf_score WHERE channel_id = %lu "
			"  GROUP BY cf_score.userhost_id "
			"  UNION ALL "
			"  SELECT cf_score_history.userhost_id, SUM(score) AS t "
			"  FROM cf_score_history WHERE channel_id = %lu "
			"  GROUP BY cf_score_history.userhost_id) AS total_table "
			"GROUP BY total_table.userhost_id "
			"HAVING total > %d "
			"ORDER BY total DESC",
			channel_id, channel_id, min_score);
	}
	else
	{
		rsdb_exec_fetch(&data, "SELECT userhost_id, SUM(t) AS total "
			"FROM "
			"  (SELECT cf_score.userhost_id, count(*) AS t "
			"  FROM cf_score WHERE channel_id = %lu "
			"  GROUP BY cf_score.userhost_id "
			"  UNION ALL "
			"  SELECT cf_score_history.userhost_id, SUM(score) AS t "
			"  FROM cf_score_history WHERE channel_id = %lu "
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
	scores->s_items = my_malloc(sizeof(struct chanfix_score_item) * max_num);

	for(i = 0; i < max_num; i++)
	{
		scores->s_items[i].userhost_id = atoi(data.row[i][0]);
		scores->s_items[i].score = atoi(data.row[i][1]);
	}

	rsdb_exec_fetch_end(&data);

	return scores;
}

/* Return an array of chanfix_score_item structures for each chanop stored in
 * the DB for the specified channel.
 */
static struct chanfix_score *
get_chmember_scores(struct chanfix_score *scores, struct channel *chptr,
		dlink_list *listptr, short ignore_clones)
{
	unsigned long userhost_id;
	struct chmember *msptr;
	struct chanfix_score_item *clone;
	dlink_node *ptr, *next_ptr, *ptr2;
	char userhost[USERLEN+HOSTLEN+4+1];
	unsigned int i, u_count, c_count;


	if(dlink_list_length(listptr) < 1)
		return NULL;

	if(scores == NULL)
	{
		if((scores = get_all_cf_scores(chptr, 0, 0)) == NULL)
			return NULL;
	}
	else
	{
		for(i = 0; i < scores->length; i++)
			scores->s_items[i].msptr = NULL;

		if(dlink_list_length(&scores->clones) > 0)
		{
			DLINK_FOREACH(ptr, scores->clones.head)
			{
				clone = ptr->data;
				mlog("debug: re-setting clone with userhost_id: %d",
						clone->userhost_id);
				clone->msptr = NULL;
			}
		}
	}


	u_count = 0;	/* counter for users */
	c_count = 0;	/* counter for clones */

	DLINK_FOREACH_SAFE(ptr, next_ptr, listptr->head)
	{
		msptr = ptr->data;

		snprintf(userhost, sizeof(userhost), "%s@%s",
				msptr->client_p->user->username,
				msptr->client_p->user->host);

		userhost_id = get_userhost_id(userhost);

		for(i = 0; i < scores->length; i++)
		{
			if(scores->s_items[i].userhost_id == userhost_id)
			{
				/* Check to see if msptr is NULL so this function call doesn't
				 * count duplicate user@hosts. This means if a channel contains
				 * multiple users with the same user@host, only the first one
				 * will be assigned a score.
				 */
				if(scores->s_items[i].msptr == NULL)
				{
					scores->s_items[i].msptr = msptr;
					u_count++;
					break;
				}
				else if(ignore_clones == 0)
				{
					/* Found matching user@host but msptr != NULL, meaning
					 * this must be a duplicate.
					 */
					mlog("debug: found clone as '%s'.", userhost);
					c_count++;
					if(c_count > dlink_list_length(&scores->clones))
					{
						mlog("debug: creating new clone struct.");
						clone = my_malloc(sizeof(struct chanfix_score_item));
						clone->msptr = msptr;
						clone->score = scores->s_items[i].score;
						clone->userhost_id = userhost_id;
						dlink_add_alloc(clone, &scores->clones);
					}
					else
					{
						DLINK_FOREACH(ptr2, scores->clones.head)
						{
							clone = ptr2->data;
							if(clone->msptr == NULL)
							{
								mlog("debug: re-using clone struct.");
								clone->msptr = msptr;
								clone->score = scores->s_items[i].score;
								clone->userhost_id = userhost_id;
								break;
							}
						}
					}
					break;
				}
			}
		}

		if(u_count >= scores->length) 
			break;
	}

	scores->matched = u_count;

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

		if(config_file.cf_client_needs_ident &&
				msptr->client_p->user->username[0] == '~')
			continue;

		snprintf(userhost, sizeof(userhost), "%s@%s",
				msptr->client_p->user->username,
				msptr->client_p->user->host);

		rsdb_exec(NULL, "INSERT INTO cf_temp_score (chname, userhost, timestamp, dayts) "
						"VALUES(LOWER('%Q'), LOWER('%Q'), '%lu', '%lu')",
						chptr->name, userhost, timestamp, dayts);
	}
}

static int
chanfix_opless_channel(struct chanfix_channel *cf_ch)
{
	struct chanfix_score *scores;
	struct chanfix_score_item *clone;
	unsigned int time_since_start, i, count;
	int min_score, min_chan_s, min_user_s;
	short all_opped = 1;
	dlink_node *ptr;

	time_since_start = CURRENT_TIME - (cf_ch->time_fix_started);

	/* Fetch all the scores from the DB that aren't below the absolute
	 * useful minimum.
	 */
	/*all_scores = get_all_cf_scores(cf_ch->chptr, 0,
				(CF_MIN_ABS_CHAN_SCORE_END * CF_MAX_CHANFIX_SCORE));*/
	/* We already checked this when adding the chanfix channel, so this
	 * shouldn't happen, but just to be safe.
	 */
	if(cf_ch->scores == NULL)
		return -1;

	/* If we're unable to fix the channel after the designated times, remove
	 * any possible bans or modes that might be preventing people from joining.
	 */
	if(cf_ch->flags & CF_STATUS_AUTOFIX)
	{
		if(!(cf_ch->flags & CF_STATUS_CLEAREDMODES) &&
				time_since_start > (CF_MAX_FIX_TIME * CF_REMOVE_MODES_TIME))
		{
			remove_our_simple_modes(cf_ch->chptr, chanfix_p, 1);
			cf_ch->flags |= CF_STATUS_CLEAREDMODES;
		}

		if(!(cf_ch->flags & CF_STATUS_CLEAREDBANS) &&
				time_since_start > (CF_MAX_FIX_TIME * CF_REMOVE_BANS_TIME))
		{
			remove_our_bans(cf_ch->chptr, chanfix_p, 1, 0, 0);
			cf_ch->flags |= CF_STATUS_CLEAREDBANS;
		}
	}

	/* Give get_chmember_scores() the all_scores scores so it can match them up
	 * against users in the channel for us.
	 */
	scores = get_chmember_scores(cf_ch->scores, cf_ch->chptr,
				&cf_ch->chptr->users_unopped, 0);

	/* Check there are scores and some users have been matched. */
	if(scores == NULL || scores->matched == 0)
		return 0;


	min_chan_s = (CF_MAX_CHANFIX_SCORE * CF_MIN_ABS_CHAN_SCORE_BEGIN) -
		(float)time_since_start / CF_MAX_FIX_TIME * 
			(CF_MAX_CHANFIX_SCORE * CF_MIN_ABS_CHAN_SCORE_BEGIN -
			CF_MIN_ABS_CHAN_SCORE_END * CF_MAX_CHANFIX_SCORE);

	min_user_s = (cf_ch->highest_score * CF_MIN_USER_SCORE_BEGIN) -
		(float)time_since_start / CF_MAX_FIX_TIME * 
			(cf_ch->highest_score * CF_MIN_USER_SCORE_BEGIN -
			CF_MIN_USER_SCORE_END * cf_ch->highest_score);
	
	/* The min score needed for ops is the HIGHER of min_chan_score
	 * or min_user_score.
	 */
	min_score = (min_chan_s > min_user_s)
				? min_chan_s
				: min_user_s;

	mlog("debug: Calculated min score for '%s' is %d.",
			cf_ch->chptr->name, min_score);

	/* Knowing the min score we need for ops, see how many users in the channel
	 * have a high enough score.
	 */
	modebuild_start(chanfix_p, cf_ch->chptr);
	count = 0;

	for(i = 0; i < scores->length; i++)
	{
		if(scores->s_items[i].msptr &&
			(scores->s_items[i].score >= min_score))
		{
			modebuild_add(DIR_ADD, "o",
					scores->s_items[i].msptr->client_p->name);
			op_chmember(scores->s_items[i].msptr);
			scores->s_items[i].opped = 1;
			count++;
		}
	}

	if(dlink_list_length(&scores->clones) > 0)
	{
		DLINK_FOREACH(ptr, scores->clones.head)
		{
			clone = ptr->data;
			if(clone->msptr && clone->score >= min_score)
			{
				modebuild_add(DIR_ADD, "o", clone->msptr->client_p->name);
				op_chmember(clone->msptr);
			}
		}
	}

	modebuild_finish();

	if(count < 1)
	{
		/* Unfortunately no one has a high enough score right now. */
		mlog("debug: '%s' currently doesn't have users with high enough scores.",
				cf_ch->chptr->name);
		/*my_free(all_scores->s_items);
		my_free(all_scores);*/
		return 0;
	}

	if(count == 1)
		service_err_chanmsg(chanfix_p, cf_ch->chptr, SVC_CF_1BEENOPPED);
	else
		service_err_chanmsg(chanfix_p, cf_ch->chptr, SVC_CF_HAVEBEENOPPED, count);

	mlog("debug: Gave some ops out in '%s'.", cf_ch->chptr->name);

	/* Crude check to guess whether we've opped everyone we have scores for. */
	for(i = 0; i < scores->length; i++)
	{
		if(scores->s_items[i].opped == 0)
		{
			all_opped = 0;
			break;
		}
		/*my_free(all_scores->s_items);
		my_free(all_scores);*/
	}

	if(all_opped &&
		(dlink_list_length(&cf_ch->chptr->users_opped) >= scores->length))
	{
		return 1;
	}
	/*my_free(all_scores->s_items);
	my_free(all_scores);*/

	return 0;
}


/* Iterate over the chanfix_list and perform chanfixing for the specified
 * fix_type. This will be either CF_STATUS_AUTOFIX or CF_STATUS_MANUALFIX.
 */
static void 
process_chanfix_list(int fix_type)
{
	struct chanfix_channel *cf_ch;
	dlink_node *ptr, *next_ptr;
	int fix_status;

	DLINK_FOREACH_SAFE(ptr, next_ptr, chanfix_list.head)
	{
		cf_ch = ptr->data;

		if(cf_ch->flags & fix_type)
		{
			if((fix_type == CF_STATUS_AUTOFIX) &&
				((CURRENT_TIME - cf_ch->time_prev_attempt) < CF_AUTOFIX_INTERVAL))
			{
				continue;
			}
			else if((fix_type == CF_STATUS_MANUALFIX) &&
				((CURRENT_TIME - cf_ch->time_prev_attempt) < CF_MANUALFIX_INTERVAL))
			{
				continue;
			}
			else
			{
				cf_ch->time_prev_attempt = CURRENT_TIME;
			}

			if(dlink_list_length(&cf_ch->chptr->users) < config_file.cf_min_clients)
			{
				del_chanfix(cf_ch->chptr);
				mlog("debug: Channel '%s' has insufficient users for a fix.",
						cf_ch->chptr->name);
				continue;
			}

			if(dlink_list_length(&cf_ch->chptr->users_opped) >= CF_MIN_FIX_OPS)
			{
				del_chanfix(cf_ch->chptr);
				mlog("debug: Channel '%s' has enough ops (fix complete).",
						cf_ch->chptr->name);
				continue;
			}

			if(cf_ch->highest_score <=
					(CF_MIN_ABS_CHAN_SCORE_END * CF_MAX_CHANFIX_SCORE))
			{
				del_chanfix(cf_ch->chptr);
				mlog("debug: Cannot fix channel '%s' (highest user score is too low).",
						cf_ch->chptr->name);
				continue;
			}

			if(CURRENT_TIME - cf_ch->time_fix_started > CF_MAX_FIX_TIME)
			{
				del_chanfix(cf_ch->chptr);
				mlog("debug: Cannot fix channel '%s' (fix time expired).",
						cf_ch->chptr->name);
				add_system_note(chptr->name, 0,
						"AUTOFIX ended (fix time expired)");
				continue;
			}
				
			join_service(chanfix_p, cf_ch->chptr->name,
					cf_ch->chptr->tsinfo, NULL, 0);

			fix_status = chanfix_opless_channel(cf_ch);

			if(fix_status != 0)
			{
				if(fix_status)
					mlog("debug: Opping logic thinks '%s' is fixed.",
							cf_ch->chptr->name);
				else
					mlog("debug: Fatal error occured trying to fix '%s'.",
							cf_ch->chptr->name);

				del_chanfix(cf_ch->chptr);
			}
		}
	}

}


typedef int (*scorecmp)(const void *, const void *);
static int
score_cmp(struct chanfix_score_item *one, struct chanfix_score_item *two)
{
	return (two->score - one->score);
}


static struct chanfix_score *
build_channel_scores(struct channel *chptr, int max_num)
{
	struct chanfix_score *scores;
	unsigned long channel_id, userhost_id;
	struct chmember *msptr;
	dlink_node *ptr;
	struct rsdb_table data;
	char userhost[USERLEN+HOSTLEN+4+1];
	int day_score, hist_score;
	unsigned int user_count;

	/* Channel has no ID in the DB and therefore can't have any scores. */
	if((channel_id = get_channel_id(chptr->name)) == 0)
		return NULL;

	if(dlink_list_length(&chptr->users) < 1)
		return NULL;

	scores = my_malloc(sizeof(struct chanfix_score));
	scores->s_items = my_malloc(sizeof(struct chanfix_score_item) *
					dlink_list_length(&chptr->users));

	user_count = 0;

	DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;

		snprintf(userhost, sizeof(userhost), "%s@%s",
				msptr->client_p->user->username,
				msptr->client_p->user->host);

		userhost_id = get_userhost_id(userhost);

		mlog("debug: user id %d matches with userhost '%s'.", userhost_id, userhost);

		if(!userhost_id)
			continue;

		rsdb_exec_fetch(&data, "SELECT userhost_id, COUNT(*) AS t "
			"FROM cf_score "
			"WHERE channel_id = %lu AND userhost_id= %lu ",
			channel_id, userhost_id);
		if(data.row_count == 0 || data.row[0][0] == NULL)
			day_score = 0;
		else
			day_score = atoi(data.row[0][1]);
		rsdb_exec_fetch_end(&data);


		rsdb_exec_fetch(&data, "SELECT userhost_id, SUM(score) AS t "
			"  FROM cf_score_history "
			"  WHERE channel_id = %lu AND userhost_id= %lu",
			channel_id, userhost_id);
		if(data.row_count == 0 || data.row[0][0] == NULL)
			hist_score = 0;
		else
			hist_score = atoi(data.row[0][1]);
		rsdb_exec_fetch_end(&data);

		if(day_score == 0 && hist_score == 0)
		{
			continue;
		}

		scores->s_items[user_count].userhost_id = userhost_id;
		scores->s_items[user_count].score = day_score + hist_score;
		scores->s_items[user_count].msptr = msptr;
		user_count++;
	}

	scores->s_items = realloc(scores->s_items,
			sizeof(struct chanfix_score_item) * user_count);

	scores->length = user_count;
	scores->matched = user_count;

	qsort(scores->s_items, scores->length,
			sizeof(struct chanfix_score_item), (scorecmp) score_cmp);

	if(max_num > 0)
	{
		scores->s_items = realloc(scores->s_items,
				sizeof(struct chanfix_score_item) * max_num);
		if(user_count > max_num)
		{
			scores->length = max_num;
			scores->matched = max_num;
		}
	}

	return scores;
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

	if(is_network_split())
	{
		mlog("debug: Channel scoring skipped (network split).");
		return;
	}

	mlog("debug: Examining channels for opped users.");

	DLINK_FOREACH(ptr, channel_list.head)
	{
		chptr = ptr->data;

		if(dlink_list_length(&chptr->users) >= config_file.cf_min_clients)
		{
			if(dlink_list_length(&chptr->users_opped) != 0)
			{
				collect_channel_scores(chptr, timestamp, dayts);
			}
			else if((!chptr->cfptr) && (add_chanfix(chptr, 0, NULL)))
			{
				mlog("debug: Added opless channel '%s' for autofixing.",
						chptr->name);
			}
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
				"WHERE timestamp=%lu", min_ts);

		/* Delete these timestamp entries when we're done. */
		rsdb_exec(NULL, "DELETE FROM cf_temp_score WHERE timestamp=%lu", min_ts);

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
				"FROM cf_score where dayts = %u "
				"GROUP BY channel_id, userhost_id",
				min_dayts);

		/* Delete these day's entries when we're done. */
		rsdb_exec(NULL, "DELETE FROM cf_score WHERE dayts = %lu", min_dayts);

		i++;
	}

	eventAddOnce("e_chanfix_collate_history", e_chanfix_collate_history, NULL,
				seconds_to_midnight()+5);

	/* Drop old history data from the cf_score_history table. */
	mlog("info: Dropping old daysample history.");

	rsdb_exec(NULL, "DELETE FROM cf_score_history WHERE dayts < %lu",
				(DAYS_SINCE_EPOCH - CF_DAYSAMPLES + 1));
}

static void
e_chanfix_autofix_channels(void *unused)
{
	if(!is_network_split() && config_file.cf_enable_autofix)
	{
		mlog("debug: Processing autofix channels.");
		process_chanfix_list(CF_STATUS_AUTOFIX);
	}
}

static void
e_chanfix_manualfix_channels(void *unused)
{
	if(!is_network_split() && config_file.cf_enable_chanfix)
	{
		mlog("debug: Processing chanfix channels.");
		process_chanfix_list(CF_STATUS_MANUALFIX);
	}
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
h_chanfix_channel_opless(void *chptr_v, void *unused)
{
	struct channel *chptr = (struct channel *) chptr_v;

	if(!chptr->cfptr)
	{
		if(netsplit_warn_ts > CURRENT_TIME)
		{
			mlog("debug: Temporarily ignoring opless channel %s "
					"(recent squit detected).", chptr->name);
		}
		else if(add_chanfix(chptr, 0, NULL))
		{
			mlog("debug: Added opless channel %s for autofixing.",
					chptr->name);
		}
	}

	return 0;
}

static int
h_chanfix_server_squit_warn(void *target_p, void *unused)
{
	/* Temporarily disable opless channel detection because it looks
	 * like a netsplit is in progress.
	 */
	netsplit_warn_ts = CF_TEMP_OPLESS_IGNORE_TIME;

	return 0;
}

static int
o_chanfix_score(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	char buf[BUFSIZE], t_buf[8];
	int i, count;
	struct chanfix_score *scores, *all_scores;

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
	}
#endif

	/* Show top scores for the channel. */
	if(all_scores = get_all_cf_scores(chptr, config_file.cf_num_top_scores, 0))
	{
		buf[0] = '\0';
		for(i = 0; i < all_scores->length; i++)
		{
			snprintf(t_buf, sizeof(t_buf), "%d ", all_scores->s_items[i].score);
			strcat(buf, t_buf);
		}
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_TOPSCORESFOR,
				config_file.cf_num_top_scores, parv[0]);
		service_send(chanfix_p, client_p, conn_p, "%s", buf);
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
		return 0;
	}

	my_free(all_scores->s_items);
	my_free(all_scores);
	all_scores = NULL;

	/* Get new list containing all DB scores */
	if((all_scores = get_all_cf_scores(chptr, 0, 0)) == NULL)
		return 0;

	/* Show the top opped scores for the channel. */
	service_snd(chanfix_p, client_p, conn_p, SVC_CF_TOPOPSCORES,
			config_file.cf_num_top_scores, parv[0]);

	scores = get_chmember_scores(all_scores, chptr, &chptr->users_opped, 1);
	if(scores && scores->matched > 0)
	{
		buf[0] = '\0';
		count = 0;
		for(i = 0; i < scores->length && count < config_file.cf_num_top_scores; i++)
		{
			if(scores->s_items[i].msptr)
			{
				snprintf(t_buf, sizeof(t_buf), "%d ", scores->s_items[i].score);
				strcat(buf, t_buf);
				count++;
			}
		}
		service_send(chanfix_p, client_p, conn_p, "%s", buf);
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOMATCHES);
	}

	/* Show the top unopped scores for the channel. */
	service_snd(chanfix_p, client_p, conn_p, SVC_CF_TOPUNOPSCORES,
			config_file.cf_num_top_scores, parv[0]);

	scores = get_chmember_scores(all_scores, chptr, &chptr->users_unopped, 1);
	if(scores && scores->matched > 0)
	{
		buf[0] = '\0';
		count = 0;
		for(i = 0; i < scores->length && count < config_file.cf_num_top_scores; i++)
		{
			if(scores->s_items[i].msptr)
			{
				snprintf(t_buf, sizeof(t_buf), "%d ", scores->s_items[i].score);
				strcat(buf, t_buf);
				count++;
			}
		}
		service_send(chanfix_p, client_p, conn_p, "%s", buf);
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOMATCHES);
	}

	my_free(all_scores->s_items);
	my_free(all_scores);

	return 0;
}


static int
o_chanfix_uscore(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	unsigned long channel_id, userhost_id;
	struct rsdb_table data;
	int day_score, hist_score;

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	if((channel_id = get_channel_id(parv[0])) == 0)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
		return 0;
	}

#ifdef ENABLE_CHANSERV
	if(find_channel_reg(NULL, parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHANSERVCHANNEL, parv[0]);
	}
#endif

	userhost_id = get_userhost_id(parv[1]);

	if(!userhost_id)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOUSERMATCH);
		return 0;
	}

	rsdb_exec_fetch(&data, "SELECT userhost_id, COUNT(*) AS t "
		"FROM cf_score "
		"WHERE channel_id = %lu AND userhost_id= %lu ",
		channel_id, userhost_id);
	if(data.row_count == 0 || data.row[0][0] == NULL)
		day_score = 0;
	else
		day_score = atoi(data.row[0][1]);
	rsdb_exec_fetch_end(&data);


	rsdb_exec_fetch(&data, "SELECT userhost_id, SUM(score) AS t "
		"  FROM cf_score_history "
		"  WHERE channel_id = %lu AND userhost_id= %lu",
		channel_id, userhost_id);
	if(data.row_count == 0 || data.row[0][0] == NULL)
		hist_score = 0;
	else
		hist_score = atoi(data.row[0][1]);
	rsdb_exec_fetch_end(&data);


	if(day_score == 0 && hist_score == 0)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOUSERMATCH);
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_UHOSTSCORE,
				day_score + hist_score, parv[1]);
	}

	return 0;
}

static int
o_chanfix_userlist(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	char buf[BUFSIZE];
	int i, count;
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
	}
#endif

	if((scores = get_chmember_scores(NULL, chptr, &chptr->users, 0)) == NULL)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
		return 0;
	}

	if(scores->matched > 0)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_TOPUSERSFOR,
			 config_file.cf_num_top_scores, parv[0]);

		count = 0;
		for(i = 0; i < scores->length && count < config_file.cf_num_top_scores; i++)
		{
			if(scores->s_items[i].msptr)
			{
				snprintf(buf, sizeof(buf), "%4d  %s!%s@%s", scores->s_items[i].score,
					scores->s_items[i].msptr->client_p->name,
					scores->s_items[i].msptr->client_p->user->username,
					scores->s_items[i].msptr->client_p->user->host);
				service_send(chanfix_p, client_p, conn_p, "%s", buf);
				count++;
			}
		}
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOMATCHES);
	}

	my_free(scores->s_items);
	my_free(scores);

	return 0;
}

/* Duplicate userlist command using build_channel_scores() */
static int
o_chanfix_userlist2(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	char buf[BUFSIZE];
	int i, count;
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
	}
#endif

	if((scores = build_channel_scores(chptr, 0)) == NULL)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
		return 0;
	}

	if(scores->matched > 0)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_TOPUSERSFOR,
				config_file.cf_num_top_scores, parv[0]);

		count = 0;
		for(i = 0; i < scores->length && count < config_file.cf_num_top_scores; i++)
		{
			if(scores->s_items[i].msptr)
			{
				snprintf(buf, sizeof(buf), "%4d  %s!%s@%s", scores->s_items[i].score,
					scores->s_items[i].msptr->client_p->name,
					scores->s_items[i].msptr->client_p->user->username,
					scores->s_items[i].msptr->client_p->user->host);
				service_send(chanfix_p, client_p, conn_p, "%s", buf);
				count++;
			}
		}
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOMATCHES);
	}

	my_free(scores->s_items);
	my_free(scores);

	return 0;
}

static int
o_chanfix_chanfix(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	dlink_node *ptr, *next_ptr;
	struct chmember *msptr;

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_snd(chanfix_p, client_p, conn_p,
				SVC_IRC_NOSUCHCHANNEL, parv[0]);
		return 0;
	}

	if(!config_file.cf_enable_chanfix)
	{
		service_send(chanfix_p, client_p, conn_p,
				"Manual channel fixing disabled.");
		return 0;
	}

	if(is_network_split())
	{
		service_send(chanfix_p, client_p, conn_p,
				"Unable to comply. Splitmode active.");
		return 0;
	}


	if(dlink_list_length(&chptr->users) < config_file.cf_min_clients)
	{
		/* Not enough users in this channel */
		service_snd(chanfix_p, client_p, conn_p,
				SVC_CF_NOTENOUGHUSERS, parv[0]);
		return 0;
	}

	/* Check if anyone is opped, if they are we don't do anything. */
	if(dlink_list_length(&chptr->users_opped) > 0)
	{
		service_snd(chanfix_p, client_p, conn_p,
				SVC_CF_HASOPPEDUSERS, parv[0]);
		return 0;
	}

	/* Need to check to see if this channel has an ALERT set for it. */

	/* Try to add the manual fix. If an error occurs, add_chanfix() should
	 * inform the client since we've passed a client ptr.
	 */
	if(!add_chanfix(chptr, 1, client_p))
		return 0;

	join_service(chanfix_p, parv[0], chptr->tsinfo, NULL, 0);

	service_err_chanmsg(chanfix_p, chptr, SVC_CF_CHANFIXINPROG);

	remove_our_simple_modes(chptr, chanfix_p, 1);
	remove_our_bans(chptr, chanfix_p, 1, 0, 0);
 
	service_snd(chanfix_p, client_p, conn_p, SVC_ISSUEDFORBY,
			chanfix_p->name, "CHANFIX", parv[0], client_p->name);

	add_system_note(parv[0], 0, "CHANFIX by %s",
			client_p->user->oper->name);

	return 0;
}

static int
o_chanfix_revert(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
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

	if(!add_chanfix(chptr, 1, client_p))
		return 0;

	/* Everything looks okay, execute the takeover. */
	chan_takeover(chptr);
	service_err_chanmsg(chanfix_p, chptr, SVC_CF_CHANFIXINPROG);

	service_snd(chanfix_p, client_p, conn_p, SVC_ISSUEDFORBY,
			chanfix_p->name, "REVERT", parv[0], client_p->name);

	add_system_note(parv[0], 0, "REVERT issued by %s",
			client_p->user->oper->name);

	return 0;
}


static int
o_chanfix_block(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	uint32_t flags = 0;

	/*const char *msg;
	msg = rebuild_params(parv, parc, 1);*/

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	rsdb_exec_fetch(&data,
			"SELECT flags FROM cf_channel WHERE chname=LOWER('%Q')", parv[0]);

	if((data.row_count > 0) && (data.row[0][0] != NULL))
	{
		flags = atoi(data.row[0][0]);

		if(flags & CF_CHAN_BLOCK)
		{
			service_snd(chanfix_p, client_p, conn_p, SVC_CF_ISBLOCKED, parv[0]);
		}
		else
		{
			flags |= CF_CHAN_BLOCK;
			service_snd(chanfix_p, client_p, conn_p, SVC_SUCCESSFULON,
					chanfix_p->name, "BLOCKED", parv[0]);
			rsdb_exec(NULL, "UPDATE cf_channel SET flags = '%d' WHERE chname = '%Q'",
					flags, parv[0]);
		}
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
		flags |= CF_CHAN_BLOCK;
		rsdb_exec(NULL, "INSERT INTO cf_channel (chname, flags) "
				"VALUES (LOWER('%Q'), '%d')", parv[0], flags);
		service_snd(chanfix_p, client_p, conn_p, SVC_SUCCESSFULON,
				chanfix_p->name, "BLOCKED", parv[0]);
	}

	rsdb_exec_fetch_end(&data);

	return 0;
}


static int
o_chanfix_unblock(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	uint32_t flags = 0;

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	rsdb_exec_fetch(&data,
			"SELECT flags FROM cf_channel WHERE chname=LOWER('%Q')", parv[0]);

	if((data.row_count > 0) && (data.row[0][0] != NULL))
	{
		flags = atoi(data.row[0][0]);

		if(!(flags & CF_CHAN_BLOCK))
		{
			service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOTBLOCKED, parv[0]);
		}
		else
		{
			flags &= ~CF_CHAN_BLOCK;
			service_snd(chanfix_p, client_p, conn_p, SVC_SUCCESSFULON,
					chanfix_p->name, "UNBLOCKED", parv[0]);
			rsdb_exec(NULL, "UPDATE cf_channel SET flags = '%d' WHERE chname = '%Q'",
					flags, parv[0]);
		}
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
	}

	rsdb_exec_fetch_end(&data);

	return 0;
}


static int
o_chanfix_alert(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	uint32_t flags = 0;

	/*const char *msg;
	msg = rebuild_params(parv, parc, 1);*/

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	rsdb_exec_fetch(&data,
			"SELECT flags FROM cf_channel WHERE chname=LOWER('%Q')", parv[0]);

	if((data.row_count > 0) && (data.row[0][0] != NULL))
	{
		flags = atoi(data.row[0][0]);

		if(flags & CF_CHAN_ALERT)
		{
			service_snd(chanfix_p, client_p, conn_p, SVC_CF_HASALERT, parv[0]);
		}
		else
		{
			flags |= CF_CHAN_ALERT;
			service_snd(chanfix_p, client_p, conn_p, SVC_SUCCESSFULON,
					chanfix_p->name, "ALERT", parv[0]);
			rsdb_exec(NULL, "UPDATE cf_channel SET flags = '%d' "
					"WHERE chname = '%Q'", flags, parv[0]);
		}
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
	}

	rsdb_exec_fetch_end(&data);

	return 0;
}


static int
o_chanfix_unalert(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	uint32_t flags = 0;

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	rsdb_exec_fetch(&data,
			"SELECT flags FROM cf_channel WHERE chname=LOWER('%Q')", parv[0]);

	if((data.row_count > 0) && (data.row[0][0] != NULL))
	{
		flags = atoi(data.row[0][0]);

		if(!(flags & CF_CHAN_ALERT))
		{
			service_snd(chanfix_p, client_p, conn_p, SVC_CF_NOALERT, parv[0]);
		}
		else
		{
			flags &= ~CF_CHAN_ALERT;
			service_snd(chanfix_p, client_p, conn_p, SVC_SUCCESSFULON,
					chanfix_p->name, "UNALERT", parv[0]);
			rsdb_exec(NULL, "UPDATE cf_channel SET flags = '%d' WHERE chname = '%Q'",
					flags, parv[0]);
		}
	}
	else
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
	}

	rsdb_exec_fetch_end(&data);

	return 0;
}

static int
o_chanfix_info(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	uint32_t flags;
	unsigned long channel_id;
	struct channel *chptr;
	struct chanfix_channel *cfptr;
	struct rsdb_table data;

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

#ifdef ENABLE_CHANSERV
	if(find_channel_reg(NULL, parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHANSERVCHANNEL, parv[0]);
		return 0;
	}
#endif

	service_snd(chanfix_p, client_p, conn_p, SVC_CF_INFOON, parv[0]);

	if(!get_cf_chan_flags(parv[0], &flags))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
		return 0;
	}

	if(flags & CF_CHAN_ALERT)
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_HASALERT, parv[0]);

	if(flags & CF_CHAN_BLOCK)
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_ISBLOCKED, parv[0]);

	if(((chptr = find_channel(parv[0])) != NULL) && (chptr->cfptr))
	{
		mlog("debug: inside fix status check");
		cfptr = (struct chanfix_channel *) chptr->cfptr;
		if(cfptr->flags & CF_STATUS_AUTOFIX)
			service_snd(chanfix_p, client_p,
					conn_p, SVC_CF_AUTOFIXED, parv[0]);
		else
			service_snd(chanfix_p, client_p,
					conn_p, SVC_CF_CHANFIXED, parv[0]);
	}

	if((channel_id = get_channel_id(parv[0])) == 0)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
		return 0;
	}

	rsdb_exec_fetch(&data, "SELECT id, author, timestamp, text FROM cf_note "
			"WHERE channel_id = %lu "
			"ORDER BY timestamp DESC LIMIT %d",
			channel_id, config_file.cf_max_notes);

	if(data.row_count > 0)
	{
		int i;

		for(i = data.row_count-1; i >= 0; i--)
		{
			service_err(chanfix_p, client_p, SVC_CF_SHOWNOTE,
					atoi(data.row[i][0]), data.row[i][1],
					get_time(atoi(data.row[i][2]), 0), data.row[i][3]);
		}
	}
	else
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NONOTES);

	rsdb_exec_fetch_end(&data);

	service_snd(chanfix_p, client_p, conn_p, SVC_CF_ENDOFINFO);

	return 0;
}

static int
o_chanfix_check(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	int users, ops;

	if(!valid_chname(parv[0]))
	{
		service_err(chanfix_p, client_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_err(chanfix_p, client_p, SVC_IRC_NOSUCHCHANNEL, parv[0]);
		return 0;
	}

	ops = dlink_list_length(&chptr->users_opped);
	users = dlink_list_length(&chptr->users);
	service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHECK, parv[0], ops, users);

	return 0;
}

static int
o_chanfix_addnote(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	unsigned long channel_id, note_id;
	struct rsdb_table data;
	const char *msg;

	if(!valid_chname(parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	if((channel_id = get_channel_id(parv[0])) == 0)
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_NODATAFOR, parv[0]);
		return 0;
	}

#ifdef ENABLE_CHANSERV
	if(find_channel_reg(NULL, parv[0]))
	{
		service_snd(chanfix_p, client_p, conn_p, SVC_CF_CHANSERVCHANNEL, parv[0]);
	}
#endif


	/*rsdb_exec_fetch(&data, "SELECT userhost_id, COUNT(*) AS t "
		"FROM cf_score "
		"WHERE channel_id = %lu AND userhost_id= %lu ",
		channel_id, userhost_id);
	if(data.row_count == 0 || data.row[0][0] == NULL)
		day_score = 0;
	else
		day_score = atoi(data.row[0][1]);
	rsdb_exec_fetch_end(&data);


	rsdb_exec_fetch(&data, "SELECT userhost_id, SUM(score) AS t "
		"  FROM cf_score_history "
		"  WHERE channel_id = %lu AND userhost_id= %lu",
		channel_id, userhost_id);
	if(data.row_count == 0 || data.row[0][0] == NULL)
		hist_score = 0;
	else
		hist_score = atoi(data.row[0][1]);
	rsdb_exec_fetch_end(&data);*/

	msg = rebuild_params(parv, parc, 1);

	rsdb_exec(NULL,
			"INSERT INTO cf_note (channel_id, timestamp, author, text) "
			"VALUES('%lu', '%lu', '%Q', '%Q')",
			channel_id, CURRENT_TIME, client_p->user->oper->name, msg);

	service_err(chanfix_p, client_p, SVC_CF_NOTEADDED, parv[0]);



	return 0;
}

static int
o_chanfix_delnote(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	unsigned long note_id;
	char *endptr;

	note_id = strtol(parv[0], &endptr, 10);

	if(EmptyString(endptr) && note_id > 0)
	{
		rsdb_exec(NULL, "DELETE FROM cf_note WHERE id = %lu", note_id);
		service_err(chanfix_p, client_p, SVC_CF_DELNOTE, note_id);
	}
	else
		service_err(chanfix_p, client_p, SVC_CF_NOTEINVALID, parv[0]);

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

/* Add a channel to be manual or auto fixed.
 * Input: channel pointer, man/auto fix bool, client struct if an error
 *        message needs to be sent to a client.
 */
static int
add_chanfix(struct channel *chptr, short chanfix, struct client *client_p)
{
	struct chanfix_channel *cf_chan;
	uint32_t flags;

#ifdef ENABLE_CHANSERV
	/* Need to skip the channel if it's registered with ChanServ. */
	if(find_channel_reg(NULL, chptr->name))
	{
		if(client_p)
			service_err(chanfix_p, client_p,
					SVC_CF_CHANSERVCHANNEL, chptr->name);
		return 0;
	}
#endif

	/* Must check if the channel has any sores in the DB. If not, print a
	 * messages to logged in chanfix operators saying we know the channel
	 * is oppless but cannot fix it, then return 0.
	 */

	if(get_cf_chan_flags(chptr->name, &flags) > 0)
	{
		if(flags & CF_CHAN_BLOCK)
		{
			if(client_p)
				service_err(chanfix_p, client_p, SVC_CF_ISBLOCKED, chptr->name);
			return 0;
		}

		if(chanfix && (flags & CF_CHAN_ALERT))
		{
			if(client_p)
				service_err(chanfix_p, client_p, SVC_CF_HASALERT, chptr->name);
			return 0;
		}
	}

	/* Make sure this channel isn't already being fixed. */
	if(chptr->cfptr)
	{
		if(client_p)
			service_err(chanfix_p, client_p, SVC_CF_BEINGFIXED, chptr->name);
		return 0;
	}

	cf_chan = my_calloc(1, sizeof(struct chanfix_channel));
	cf_chan->chptr = chptr;

	if(chanfix)
		cf_chan->flags |= CF_STATUS_MANUALFIX;
	else
		cf_chan->flags |= CF_STATUS_AUTOFIX;

	cf_chan->time_fix_started = CURRENT_TIME;
	cf_chan->time_prev_attempt = 0;

	cf_chan->scores = get_all_cf_scores(chptr, 0,
			(CF_MIN_ABS_CHAN_SCORE_END * CF_MAX_CHANFIX_SCORE));

	if(cf_chan->scores == NULL)
	{
		mlog("debug: Insufficient DB scores for '%s', cannot add for chanfixing.",
					chptr->name);
		if(client_p)
			service_err(chanfix_p, client_p, SVC_CF_NODATAFOR, chanfix_p->name);
		my_free(cf_chan);
		cf_chan = NULL;
		return 0;
	}

	cf_chan->highest_score = cf_chan->scores->s_items[0].score;
	cf_chan->endfix_uscore = CF_MIN_USER_SCORE_END * cf_chan->highest_score;

	/*my_free(scores->s_items);
	my_free(scores);
	scores = NULL;*/


	/* We should either:
	 *	- Get the highest chanop score so we can set highest_score, or
	 *  - Fetch all the chanops from the DB we can consider for opping and store
	 *    them in the autofix_channel struct so we don't have to keep querying
	 *    the DB.
	 */

	dlink_add(cf_chan, &cf_chan->node, &chanfix_list);
	chptr->cfptr = cf_chan;

	if(!chanfix)
		add_system_note(chptr->name, 0, "Added for AUTOFIX");

	return 1;
}

static void 
del_chanfix(struct channel *chptr)
{
	struct chanfix_channel *cfptr;
	struct chanfix_score_item *clone;
	dlink_node *ptr, *next_ptr;

	part_service(chanfix_p, chptr->name);

	cfptr = (struct chanfix_channel *) chptr->cfptr;

	dlink_delete(&cfptr->node, &chanfix_list);

	/* If chanfix detected any clones, get rid of those too. */
	if(dlink_list_length(&cfptr->scores->clones) > 0)
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, cfptr->scores->clones.head)
		{
			clone = ptr->data;
			mlog("debug: freeing clone memory with userhost_id: %d",
					clone->userhost_id);
			dlink_destroy(ptr, &cfptr->scores->clones);
			my_free(clone);
		}
	}

	if(cfptr->scores)
	{
		mlog("debug: deleting scores struct for: %s", chptr->name);
		my_free(cfptr->scores->s_items);
		my_free(cfptr->scores);
		cfptr->scores = NULL;
	}

	my_free(cfptr);
	chptr->cfptr = NULL;
}


#endif
