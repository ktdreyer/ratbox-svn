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

#endif
