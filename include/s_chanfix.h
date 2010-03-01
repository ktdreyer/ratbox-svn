/* $Id: s_chanfix.h 23704 2007-03-16 21:46:42Z leeh $ */
#ifndef INCLUDED_s_chanfix_h
#define INCLUDED_s_chanfix_h

struct channel;

#define DAYS_SINCE_EPOCH	CURRENT_TIME / 86400

#define CF_STATUS_CLEAREDMODES		0x0000001
#define CF_STATUS_CLEAREDBANS		0x0000002
#define CF_STATUS_MANUALFIX		0x0000004
#define CF_STATUS_AUTOFIX		0x0000008

/* Structures used to store the current status of channels currently being
 * fixed.
 * A chanfix channel has 2 stages:
 * Stage 1 - join with TS-1 and get rid of all the modes;
 * Stage 2 - give ops to the right people.
 */
struct chanfix_channel
{
	struct channel *chptr;
	time_t time_fix_started;
	time_t time_prev_attempt;
	int stage;
	int highest_score;	/* highest chanop score in channel */
	int flags;

	dlink_node node;
};

/* Structures for storing one or more chanfix_score_items containing
 * chanop info such as: msptr, userhost_id and score.
 */
struct chanfix_score
{
	struct chanfix_score_item *score_items;
	int length;
};

struct chanfix_score_item
{
	struct chmember *msptr;
	unsigned long userhost_id;
	int score;
};

/* The maximum number of users chanfix will op per fix cycle. */
#define CF_MAX_USERS_TO_OP	8

/* The minimum number of ops required for a successful chanfix. */
#define CF_MIN_FIX_OPS	5

/* The number of daysamples to keep scores for in the database. */
#define CF_DAYSAMPLES	14

/* The maximum possible score achievable by a user (288 * CF_DAYSAMPLES). */
#define CF_MAX_CHANFIX_SCORE	4032


#endif
