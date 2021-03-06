/* $Id: s_chanfix.h 23704 2007-03-16 21:46:42Z leeh $ */
#ifndef INCLUDED_s_chanfix_h
#define INCLUDED_s_chanfix_h

struct channel;

#define DAYS_SINCE_EPOCH	(rb_time() / 86400)

#define CF_STATUS_CLEAREDMODES		0x0000001
#define CF_STATUS_CLEAREDBANS		0x0000002
#define CF_STATUS_MANFIX		0x0000004
#define CF_STATUS_AUTOFIX		0x0000008

/* Channel flags used by the DB in cf_channel table */
#define CF_CHAN_BLOCK	0x0000001
#define CF_CHAN_ALERT	0x0000002
#define CF_CHAN_SUSPEND	0x0000004

/* Structures used to store the current status of channels currently
 * being fixed.
 */
struct chanfix_channel
{
	struct channel *chptr;
	time_t fix_started;
	time_t prev_attempt;
	unsigned int cycle;	/* number of fix attempt cycles */
	struct chanfix_score *scores;
	int highest_score;	/* highest chanop score in channel */
	int endfix_uscore;	/* min possible user score at end of a chanfix */
	uint32_t flags;

	rb_dlink_node node;	/* ptr to this node in chanfix_list */
};

/* Structures for storing one or more chanfix_score_items containing
 * chanop info such as: msptr, userhost_id and score.
 */
struct chanfix_score
{
	struct chanfix_score_item *s_items;
	unsigned int length;
	unsigned int matched;

	rb_dlink_list clones;	/* List of duplicate user@hosts found. */
};

struct chanfix_score_item
{
	struct chmember *msptr;
	unsigned long userhost_id;
	unsigned short score;
	int8_t opped;
};

/* The minimum number of ops required for a successful chanfix. */
#define CF_MIN_FIX_OPS	5

/* The number of daysamples to keep scores for in the database. */
#define CF_DAYSAMPLES	14

/* The maximum possible score achievable by a user (288 * CF_DAYSAMPLES). */
#define CF_MAX_CHANFIX_SCORE	4032

/* Minimum absolute score required for chanfix to op a user, based on the
 * maximum possible chanfix score achievable (default: 0.20 * 4032). 
 * Make sure this value is between 0 and 1. */
#define CF_MIN_ABS_CHAN_SCORE_BEGIN	0.20f

/* Minimum absolute score required for chanfix to op a user towards the end
 * of a fix, based on the maximum possible chanfix score achievable. 
 * Make sure this value is between 0 and CF_MIN_ABS_CHAN_SCORE_BEGIN. */
#define CF_MIN_ABS_CHAN_SCORE_END	0.04f

/* Minimum score required for chanfix to op, relative to the highest
 * user score found for this channel in the DB (at the beginning of the fix).
 * Make sure this value is between 0 and 1. */
#define CF_MIN_USER_SCORE_BEGIN	0.90f

/* Minimum score required for chanfix to op, relative to the highest
 * user score found for this channel in the DB. Thus if you have less than
 * 30% of the highest score, chanfix will never op you.
 * Make sure this value is between 0 and CF_MIN_USER_SCORE_BEGIN. */
#define CF_MIN_USER_SCORE_END	0.30f

/* The time spent trying to fix a channel during each fix cycle.
 * Used by the op logic and should not be changed (seconds). */
#define CF_FIX_TIME	3600

/* While counting the number of fix attempt cycles for an opless
 * channel; stop inhabiting the channel after every multiple of
 * CF_FIX_CYCLES and refresh the DB scores. */
#define CF_FIX_CYCLES	6

/* The time to wait between consecutive autofix attempts (seconds). */
#define CF_AUTOFIX_FREQ	600

/* The time to wait between consecutive chanfix attempts (seconds). */
#define CF_CHANFIX_FREQ	300

/* Time to wait before removing channel modes during an autofix.
 * Expressed in seconds. */
#define CF_REMOVE_MODES_TIME	(20 * 60)

/* Time to wait before removing channel bans during an autofix.
 * Expressed in seconds. */
#define CF_REMOVE_BANS_TIME	(40 * 60)

/* Time to temporarily ignore opless channels for after a server
 * squits, meaning a netsplit might be in progress (seconds). */
#define CF_OPLESS_IGNORE_TIME  180

#endif
