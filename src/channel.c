/*
 *  ircd-ratbox: A slightly useful ircd.
 *  channel.c: Controls channels.
 *
 * Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center 
 * Copyright (C) 1996-2002 Hybrid Development Team 
 * Copyright (C) 2002 ircd-ratbox development team 
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "tools.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "hook.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"		/* captab */
#include "s_user.h"
#include "send.h"
#include "whowas.h"
#include "s_conf.h"		/* ConfigFileEntry, ConfigChannel */
#include "event.h"
#include "memory.h"
#include "balloc.h"
#include "s_log.h"
#include "s_newconf.h"

struct config_channel_entry ConfigChannel;
dlink_list global_channel_list;
BlockHeap *channel_heap;
BlockHeap *ban_heap;
BlockHeap *topic_heap;
static BlockHeap *member_heap;

static int channel_capabs[] = { CAP_EX, CAP_IE, CAP_TS6 };
#define NCHCAPS         (sizeof(channel_capabs)/sizeof(int))
#define NCHCAP_COMBOS   (1 << NCHCAPS)

static struct ChCapCombo chcap_combos[NCHCAP_COMBOS];

static void destroy_channel(struct Channel *);
static void free_topic(struct Channel *chptr);

/* init_channels()
 *
 * input	-
 * output	-
 * side effects - initialises the various blockheaps
 */
void
init_channels(void)
{
	channel_heap = BlockHeapCreate(sizeof(struct Channel), CHANNEL_HEAP_SIZE);
	ban_heap = BlockHeapCreate(sizeof(struct Ban), BAN_HEAP_SIZE);
	topic_heap = BlockHeapCreate(TOPICLEN + 1 + USERHOST_REPLYLEN, TOPIC_HEAP_SIZE);
	member_heap = BlockHeapCreate(sizeof(struct membership), MEMBER_HEAP_SIZE);
}

/* find_channel_membership()
 *
 * input	- channel to find them in, client to find
 * output	- membership of client in channel, else NULL
 * side effects	-
 */
struct membership *
find_channel_membership(struct Channel *chptr, struct Client *client_p)
{
	struct membership *msptr;
	dlink_node *ptr;

	if(!IsClient(client_p))
		return NULL;

	DLINK_FOREACH(ptr, client_p->user->channel.head)
	{
		msptr = ptr->data;
		if(msptr->chptr == chptr)
			return msptr;
	}
	
	return NULL;
}

/* find_channel_status()
 *
 * input	- membership to get status for, whether we can combine flags
 * output	- flags of user on channel
 * side effects -
 */
const char *
find_channel_status(struct membership *msptr, int combine)
{
	static char buffer[3];
	char *p;

	p = buffer;

	if(is_chanop(msptr))
	{
		if(!combine)
			return "@";
		*p++ = '@';
	}

	if(is_voiced(msptr))
		*p++ = '+';

	*p = '\0';
	return buffer;
}

/* add_user_to_channel()
 *
 * input	- channel to add client to, client to add, channel flags
 * output	- 
 * side effects - user is added to channel
 */
void
add_user_to_channel(struct Channel *chptr, struct Client *client_p, int flags)
{
	struct membership *msptr;

	s_assert(client_p->user != NULL);
	if(client_p->user == NULL)
		return;

	msptr = BlockHeapAlloc(member_heap);
	memset(msptr, 0, sizeof(struct membership));

	msptr->chptr = chptr;
	msptr->client_p = client_p;
	msptr->flags = flags;

	dlinkAdd(msptr, &msptr->usernode, &client_p->user->channel);
	dlinkAdd(msptr, &msptr->channode, &chptr->members);

	if(MyClient(client_p))
		dlinkAdd(msptr, &msptr->locchannode, &chptr->locmembers);
}

/* remove_user_from_channel()
 *
 * input	- membership pointer to remove from channel
 * output	-
 * side effects - membership (thus user) is removed from channel
 */
void
remove_user_from_channel(struct membership *msptr)
{
	struct Client *client_p;
	struct Channel *chptr;
	s_assert(msptr != NULL);
	if(msptr == NULL)
		return;

	client_p = msptr->client_p;
	chptr = msptr->chptr;

	dlinkDelete(&msptr->usernode, &client_p->user->channel);
	dlinkDelete(&msptr->channode, &chptr->members);

	if(client_p->servptr == &me)
		dlinkDelete(&msptr->locchannode, &chptr->locmembers);

	chptr->users_last = CurrentTime;

	if(dlink_list_length(&chptr->members) <= 0)
	{
		destroy_channel(chptr);
		return;
	}

	BlockHeapFree(member_heap, msptr);

	return;
}

/* remove_user_from_channels()
 *
 * input        - user to remove from all channels
 * output       -
 * side effects - user is removed from all channels
 */
void
remove_user_from_channels(struct Client *client_p)
{
	struct Channel *chptr;
	struct membership *msptr;
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(client_p == NULL)
		return;

	DLINK_FOREACH_SAFE(ptr, next_ptr, client_p->user->channel.head)
	{
		msptr = ptr->data;
		chptr = msptr->chptr;

		dlinkDelete(&msptr->channode, &chptr->members);

		if(client_p->servptr == &me)
			dlinkDelete(&msptr->locchannode, &chptr->locmembers);

		chptr->users_last = CurrentTime;

		if(dlink_list_length(&chptr->members) <= 0)
			destroy_channel(chptr);

		BlockHeapFree(member_heap, msptr);
	}

	client_p->user->channel.head = client_p->user->channel.tail = NULL;
	client_p->user->channel.length = 0;
}

/* check_channel_name()
 *
 * input	- channel name
 * output	- 1 if valid channel name, else 0
 * side effects -
 */
int
check_channel_name(const char *name)
{
	s_assert(name != NULL);
	if(name == NULL)
		return 0;

	for (; *name; ++name)
	{
		if(!IsChanChar(*name))
			return 0;
	}

	return 1;
}

/* free_channel_list()
 *
 * input	- dlink list to free
 * output	-
 * side effects - list of b/e/I modes is cleared
 */
void
free_channel_list(dlink_list *list)
{
	dlink_node *ptr;
	dlink_node *next_ptr;
	struct Ban *actualBan;

	DLINK_FOREACH_SAFE(ptr, next_ptr, list->head)
	{
		actualBan = ptr->data;
		BlockHeapFree(ban_heap, actualBan);
	}

	list->head = list->tail = NULL;
	list->length = 0;
}

/* destroy_channel()
 *
 * input	- channel to destroy
 * output	-
 * side effects - channel is obliterated
 */
static void
destroy_channel(struct Channel *chptr)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->invites.head)
	{
		del_invite(chptr, ptr->data);
	}

	/* free all bans/exceptions/denies */
	free_channel_list(&chptr->banlist);
	free_channel_list(&chptr->exceptlist);
	free_channel_list(&chptr->invexlist);

	/* Free the topic */
	free_topic(chptr);

	dlinkDelete(&chptr->node, &global_channel_list);
	del_from_channel_hash(chptr->chname, chptr);
	BlockHeapFree(channel_heap, chptr);
}

/* channel_pub_or_secret()
 *
 * input	- channel
 * output	- "=" if public, "@" if secret, else "*"
 * side effects	-
 */
static const char *
channel_pub_or_secret(struct Channel *chptr)
{
	if(PubChannel(chptr))
		return ("=");
	else if(SecretChannel(chptr))
		return ("@");
	return ("*");
}

/* channel_member_names()
 *
 * input	- channel to list, client to list to, show endofnames
 * output	-
 * side effects - client is given list of users on channel
 */
void
channel_member_names(struct Channel *chptr, struct Client *client_p, int show_eon)
{
	struct membership *msptr;
	struct Client *target_p;
	dlink_node *ptr;
	char lbuf[BUFSIZE];
	char *t;
	int mlen;
	int tlen;
	int cur_len;
	int is_member;

	if(ShowChannel(client_p, chptr))
	{
		is_member = IsMember(client_p, chptr);

		cur_len = mlen = ircsprintf(lbuf, form_str(RPL_NAMREPLY),
					    me.name, client_p->name, 
					    channel_pub_or_secret(chptr),
					    chptr->chname);

		t = lbuf + cur_len;

		DLINK_FOREACH(ptr, chptr->members.head)
		{
			msptr = ptr->data;
			target_p = msptr->client_p;

			if(IsInvisible(target_p) && !is_member)
				continue;

			tlen = strlen(target_p->name) + 1;
			if(is_chanop_voiced(msptr))
				tlen++;

			if(cur_len + tlen >= BUFSIZE - 3)
			{
				*(t - 1) = '\0';
				sendto_one(client_p, "%s", lbuf);
				cur_len = mlen;
				t = lbuf + mlen;
			}

			ircsprintf(t, "%s%s ", find_channel_status(msptr, 0),
				   target_p->name);

			cur_len += tlen;
			t += tlen;
		}

		*(t - 1) = '\0';
		sendto_one(client_p, "%s", lbuf);
	}

	if(show_eon)
		sendto_one(client_p, form_str(RPL_ENDOFNAMES),
			   me.name, client_p->name, chptr->chname);
}

/* del_invite()
 *
 * input	- channel to remove invite from, client to remove
 * output	-
 * side effects - user is removed from invite list, if exists
 */
void
del_invite(struct Channel *chptr, struct Client *who)
{
	dlinkFindDestroy(&chptr->invites, who);
	dlinkFindDestroy(&who->user->invited, chptr);
}

/* is_banned()
 *
 * input	- channel to check bans for, user to check bans against
 *                optional prebuilt buffers
 * output	- 1 if banned, else 0
 * side effects -
 */
int
is_banned(struct Channel *chptr, struct Client *who, struct membership *msptr,
	  const char *s, const char *s2)
{
	char src_host[NICKLEN + USERLEN + HOSTLEN + 6];
	char src_iphost[NICKLEN + USERLEN + HOSTLEN + 6];
	dlink_node *ptr;
	struct Ban *actualBan = NULL;
	struct Ban *actualExcept = NULL;

	if(!MyClient(who))
		return 0;

	/* if the buffers havent been built, do it here */
	if(s == NULL)
	{
		ircsprintf(src_host, "%s!%s@%s",
			   who->name, who->username, who->host);
		ircsprintf(src_iphost, "%s!%s@%s",
			   who->name, who->username, who->sockhost);

		s = src_host;
		s2 = src_iphost;
	}

	DLINK_FOREACH(ptr, chptr->banlist.head)
	{
		actualBan = ptr->data;
		if(match(actualBan->banstr, s) ||
		   match(actualBan->banstr, s2) || 
		   match_cidr(actualBan->banstr, s2))
			break;
		else
			actualBan = NULL;
	}

	if((actualBan != NULL) && ConfigChannel.use_except)
	{
		DLINK_FOREACH(ptr, chptr->exceptlist.head)
		{
			actualExcept = ptr->data;

			/* theyre exempted.. */
			if(match(actualExcept->banstr, s) ||
			   match(actualExcept->banstr, s2) || 
			   match_cidr(actualExcept->banstr, s2))
			{
				/* cache the fact theyre not banned */
				if(msptr != NULL)
				{
					msptr->bants = chptr->bants;
					msptr->flags &= ~CHFL_BANNED;
				}

				return CHFL_EXCEPTION;
			}
		}
	}

	/* cache the banned/not banned status */
	if(msptr != NULL)
	{
		msptr->bants = chptr->bants;

		if(actualBan != NULL)
		{
			msptr->flags |= CHFL_BANNED;
			return CHFL_BAN;
		}
		else
		{
			msptr->flags &= ~CHFL_BANNED;
			return 0;
		}
	}

	return ((actualBan ? CHFL_BAN : 0));
}

/* can_join()
 *
 * input	- client to check, channel to check for, key
 * output	- reason for not being able to join, else 0
 * side effects -
 */
int
can_join(struct Client *source_p, struct Channel *chptr, char *key)
{
	dlink_node *lp;
	dlink_node *ptr;
	struct Ban *invex = NULL;
	char src_host[NICKLEN + USERLEN + HOSTLEN + 6];
	char src_iphost[NICKLEN + USERLEN + HOSTLEN + 6];

	s_assert(source_p->localClient != NULL);

	ircsprintf(src_host, "%s!%s@%s", 
		   source_p->name, source_p->username, source_p->host);
	ircsprintf(src_iphost, "%s!%s@%s",
		   source_p->name, source_p->username, source_p->sockhost);

	if((is_banned(chptr, source_p, NULL, src_host, src_iphost)) == CHFL_BAN)
		return (ERR_BANNEDFROMCHAN);

	if(chptr->mode.mode & MODE_INVITEONLY)
	{
		DLINK_FOREACH(lp, source_p->user->invited.head)
		{
			if(lp->data == chptr)
				break;
		}
		if(lp == NULL)
		{
			if(!ConfigChannel.use_invex)
				return (ERR_INVITEONLYCHAN);
			DLINK_FOREACH(ptr, chptr->invexlist.head)
			{
				invex = ptr->data;
				if(match(invex->banstr, src_host)
				   || match(invex->banstr, src_iphost)
				   || match_cidr(invex->banstr, src_iphost))
					break;
			}
			if(ptr == NULL)
				return (ERR_INVITEONLYCHAN);
		}
	}

	if(*chptr->mode.key && (EmptyString(key) || irccmp(chptr->mode.key, key)))
		return (ERR_BADCHANNELKEY);

	if(chptr->mode.limit && 
	   dlink_list_length(&chptr->members) >= chptr->mode.limit)
		return (ERR_CHANNELISFULL);

	return 0;
}

/* can_send()
 *
 * input	- user to check in channel, membership pointer
 * output	- whether can explicitly send or not, else CAN_SEND_NONOP
 * side effects -
 */
int
can_send(struct Channel *chptr, struct Client *source_p, 
	 struct membership *msptr)
{
	if(IsServer(source_p))
		return CAN_SEND_OPV;

	if(MyClient(source_p) && find_channel_resv(chptr->chname) &&
	   (!IsOper(source_p) || !ConfigChannel.no_oper_resvs))
		return CAN_SEND_NO;

	if(msptr == NULL)
	{
		msptr = find_channel_membership(chptr, source_p);

		if(msptr == NULL)
		{
			if(chptr->mode.mode & MODE_NOPRIVMSGS)
				return CAN_SEND_NO;
		}
	}

	if(is_chanop_voiced(msptr))
		return CAN_SEND_OPV;

	if(chptr->mode.mode & MODE_MODERATED)
		return CAN_SEND_NO;

	if(ConfigChannel.quiet_on_ban && MyClient(source_p))
	{
		/* cached can_send */
		if(msptr->bants == chptr->bants)
		{
			if(can_send_banned(msptr))
				return CAN_SEND_NO;
		}
		else if(is_banned(chptr, source_p, msptr, NULL, NULL) == CHFL_BAN)
			return CAN_SEND_NO;
	}

	return CAN_SEND_NONOP;
}

/* void check_spambot_warning(struct Client *source_p)
 * Input: Client to check, channel name or NULL if this is a part.
 * Output: none
 * Side-effects: Updates the client's oper_warn_count_down, warns the
 *    IRC operators if necessary, and updates join_leave_countdown as
 *    needed.
 */
void
check_spambot_warning(struct Client *source_p, const char *name)
{
	int t_delta;
	int decrement_count;
	if((GlobalSetOptions.spam_num &&
	    (source_p->localClient->join_leave_count >= GlobalSetOptions.spam_num)))
	{
		if(source_p->localClient->oper_warn_count_down > 0)
			source_p->localClient->oper_warn_count_down--;
		else
			source_p->localClient->oper_warn_count_down = 0;
		if(source_p->localClient->oper_warn_count_down == 0)
		{
			/* Its already known as a possible spambot */
			if(name != NULL)
				sendto_realops_flags(UMODE_BOTS, L_ALL,
						     "User %s (%s@%s) trying to join %s is a possible spambot",
						     source_p->name,
						     source_p->username, source_p->host, name);
			else
				sendto_realops_flags(UMODE_BOTS, L_ALL,
						     "User %s (%s@%s) is a possible spambot",
						     source_p->name,
						     source_p->username, source_p->host);
			source_p->localClient->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
		}
	}
	else
	{
		if((t_delta =
		    (CurrentTime - source_p->localClient->last_leave_time)) >
		   JOIN_LEAVE_COUNT_EXPIRE_TIME)
		{
			decrement_count = (t_delta / JOIN_LEAVE_COUNT_EXPIRE_TIME);
			if(decrement_count > source_p->localClient->join_leave_count)
				source_p->localClient->join_leave_count = 0;
			else
				source_p->localClient->join_leave_count -= decrement_count;
		}
		else
		{
			if((CurrentTime -
			    (source_p->localClient->last_join_time)) < GlobalSetOptions.spam_time)
			{
				/* oh, its a possible spambot */
				source_p->localClient->join_leave_count++;
			}
		}
		if(name != NULL)
			source_p->localClient->last_join_time = CurrentTime;
		else
			source_p->localClient->last_leave_time = CurrentTime;
	}
}

/* finish_splitmode()
 *
 * inputs	-
 * outputs	-
 * side effects - splitmode is finished
 */
static void
finish_splitmode(void *unused)
{
	splitmode = 0;

	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "Network rejoined, deactivating splitmode");
}

/* check_splitmode()
 *
 * input	-
 * output	-
 * side effects - compares usercount and servercount against their split
 *                values and adjusts splitmode accordingly
 */
void
check_splitmode(void *unused)
{
	if(splitchecking && (ConfigChannel.no_join_on_split || ConfigChannel.no_create_on_split))
	{
		if(!splitmode && 
		   ((dlink_list_length(&global_serv_list) < split_servers) ||
		    (Count.total < split_users)))
		{
			splitmode = 1;

			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Network split, activating splitmode");
			eventAddIsh("check_splitmode", check_splitmode, NULL, 10);
		}
		else if(splitmode && 
			(dlink_list_length(&global_serv_list) >= split_servers) &&
			(Count.total >= split_users))
		{
			/* splitmode ended, if we're delaying the
			 * end of split, add an event, else finish it
			 */
			if(GlobalSetOptions.split_delay > 0)
				eventAddOnce("finish_splitmode", finish_splitmode,
					     NULL, GlobalSetOptions.split_delay);
			else
				finish_splitmode(NULL);

			eventDelete(check_splitmode, NULL);
		}
	}
}


/* allocate_topic()
 *
 * input	- channel to allocate topic for
 * output	- 1 on success, else 0
 * side effects - channel gets a topic allocated
 */
static void
allocate_topic(struct Channel *chptr)
{
	void *ptr;

	if(chptr == NULL)
		return;

	ptr = BlockHeapAlloc(topic_heap);

	/* Basically we allocate one large block for the topic and
	 * the topic info.  We then split it up into two and shove it
	 * in the chptr 
	 */
	chptr->topic = ptr;
	chptr->topic_info = (char *) ptr + TOPICLEN + 1;
	*chptr->topic = '\0';
	*chptr->topic_info = '\0';
}

/* free_topic()
 *
 * input	- channel which has topic to free
 * output	-
 * side effects - channels topic is free'd
 */
static void
free_topic(struct Channel *chptr)
{
	void *ptr;

	if(chptr == NULL || chptr->topic == NULL)
		return;

	/* This is safe for now - If you change allocate_topic you
	 * MUST change this as well
	 */
	ptr = chptr->topic;
	BlockHeapFree(topic_heap, ptr);
	chptr->topic = NULL;
	chptr->topic_info = NULL;
}

/* set_channel_topic()
 *
 * input	- channel, topic to set, topic info and topic ts
 * output	-
 * side effects - channels topic, topic info and TS are set.
 */
void
set_channel_topic(struct Channel *chptr, const char *topic,
		  const char *topic_info, time_t topicts)
{
	if(strlen(topic) > 0)
	{
		if(chptr->topic == NULL)
			allocate_topic(chptr);
		strlcpy(chptr->topic, topic, TOPICLEN + 1);
		strlcpy(chptr->topic_info, topic_info, USERHOST_REPLYLEN);
		chptr->topic_time = topicts;
	}
	else
	{
		if(chptr->topic != NULL)
			free_topic(chptr);
		chptr->topic_time = 0;
	}
}

/* channel_modes()
 *
 * input	- channel, client to build for, modebufs to build to
 * output	-
 * side effects - user gets list of "simple" modes based on channel access.
 *                NOTE: m_join.c depends on trailing spaces in pbuf
 */
void
channel_modes(struct Channel *chptr, struct Client *client_p, char *mbuf, char *pbuf)
{
	int len;
	*mbuf++ = '+';
	*pbuf = '\0';

	if(chptr->mode.mode & MODE_SECRET)
		*mbuf++ = 's';
	if(chptr->mode.mode & MODE_PRIVATE)
		*mbuf++ = 'p';
	if(chptr->mode.mode & MODE_MODERATED)
		*mbuf++ = 'm';
	if(chptr->mode.mode & MODE_TOPICLIMIT)
		*mbuf++ = 't';
	if(chptr->mode.mode & MODE_INVITEONLY)
		*mbuf++ = 'i';
	if(chptr->mode.mode & MODE_NOPRIVMSGS)
		*mbuf++ = 'n';

	if(chptr->mode.limit)
	{
		*mbuf++ = 'l';
		if(IsMember(client_p, chptr) || IsServer(client_p))
		{
			len = ircsprintf(pbuf, "%d ", chptr->mode.limit);
			pbuf += len;
		}
	}
	if(*chptr->mode.key)
	{
		*mbuf++ = 'k';
		if(IsMember(client_p, chptr) || IsServer(client_p))
			ircsprintf(pbuf, "%s ", chptr->mode.key);
	}

	*mbuf++ = '\0';
	return;
}

/* Now lets do some stuff to keep track of what combinations of
 * servers exist...
 * Note that the number of combinations doubles each time you add
 * something to this list. Each one is only quick if no servers use that
 * combination, but if the numbers get too high here MODE will get too
 * slow. I suggest if you get more than 7 here, you consider getting rid
 * of some and merging or something. If it wasn't for irc+cs we would
 * probably not even need to bother about most of these, but unfortunately
 * we do. -A1kmm
 */

/* void init_chcap_usage_counts(void)
 *
 * Inputs	- none
 * Output	- none
 * Side-effects	- Initialises the usage counts to zero. Fills in the
 *                chcap_yes and chcap_no combination tables.
 */
void
init_chcap_usage_counts(void)
{
	unsigned long m, c, y, n;

	memset(chcap_combos, 0, sizeof(chcap_combos));

	/* For every possible combination */
	for (m = 0; m < NCHCAP_COMBOS; m++)
	{
		/* Check each capab */
		for (c = y = n = 0; c < NCHCAPS; c++)
		{
			if((m & (1 << c)) == 0)
				n |= channel_capabs[c];
			else
				y |= channel_capabs[c];
		}
		chcap_combos[m].cap_yes = y;
		chcap_combos[m].cap_no = n;
	}
}

/* void set_chcap_usage_counts(struct Client *serv_p)
 * Input: serv_p; The client whose capabs to register.
 * Output: none
 * Side-effects: Increments the usage counts for the correct capab
 *               combination.
 */
void
set_chcap_usage_counts(struct Client *serv_p)
{
	int n;

	for (n = 0; n < NCHCAP_COMBOS; n++)
	{
		if(((serv_p->localClient->caps & chcap_combos[n].cap_yes) ==
		    chcap_combos[n].cap_yes) &&
		   ((serv_p->localClient->caps & chcap_combos[n].cap_no) == 0))
		{
			chcap_combos[n].count++;
			return;
		}
	}

	/* This should be impossible -A1kmm. */
	s_assert(0);
}

/* void set_chcap_usage_counts(struct Client *serv_p)
 *
 * Inputs	- serv_p; The client whose capabs to register.
 * Output	- none
 * Side-effects	- Decrements the usage counts for the correct capab
 *                combination.
 */
void
unset_chcap_usage_counts(struct Client *serv_p)
{
	int n;

	for (n = 0; n < NCHCAP_COMBOS; n++)
	{
		if((serv_p->localClient->caps & chcap_combos[n].cap_yes) ==
		   chcap_combos[n].cap_yes &&
		   (serv_p->localClient->caps & chcap_combos[n].cap_no) == 0)
		{
			/* Hopefully capabs can't change dynamically or anything... */
			s_assert(chcap_combos[n].count > 0);

			if(chcap_combos[n].count > 0)
				chcap_combos[n].count--;
			return;
		}
	}

	/* This should be impossible -A1kmm. */
	s_assert(0);
}

/* void send_cap_mode_changes(struct Client *client_p,
 *                        struct Client *source_p,
 *                        struct Channel *chptr, int cap, int nocap)
 * Input: The client sending(client_p), the source client(source_p),
 *        the channel to send mode changes for(chptr)
 * Output: None.
 * Side-effects: Sends the appropriate mode changes to capable servers.
 *
 * Reverted back to my original design, except that we now keep a count
 * of the number of servers which each combination as an optimisation, so
 * the capabs combinations which are not needed are not worked out. -A1kmm
 */
void
send_cap_mode_changes(struct Client *client_p, struct Client *source_p,
		      struct Channel *chptr, struct ChModeChange mode_changes[],
		      int mode_count)
{
	static char modebuf[MODEBUFLEN];
	static char parabuf[MODEBUFLEN];
	int i, mbl, pbl, nc, mc;
	const char *arg;
	int dir;
	int j;
	int cap;
	int nocap;

	mc = 0;
	nc = 0;
	pbl = 0;
	parabuf[0] = 0;
	dir = MODE_QUERY;

	/* Now send to servers... */
	for (j = 0; j < NCHCAP_COMBOS; j++)
	{
		if(chcap_combos[j].count == 0)
			continue;

		cap = chcap_combos[j].cap_yes;
		nocap = chcap_combos[j].cap_no;

		if(cap & CAP_TS6)
			mbl = ircsprintf(modebuf, ":%s MODE %s ",
					 use_id(source_p), chptr->chname);
		else
			mbl = ircsprintf(modebuf, ":%s MODE %s ",
					 source_p->name, chptr->chname);

		/* loop the list of - modes we have */
		for (i = 0; i < mode_count; i++)
		{
			/* if they dont support the cap we need, or they do support a cap they
			 * cant have, then dont add it to the modebuf.. that way they wont see
			 * the mode
			 */
			if((mode_changes[i].letter == 0) ||
					((cap & mode_changes[i].caps) != mode_changes[i].caps)
					|| ((nocap & mode_changes[i].nocaps) != mode_changes[i].nocaps))
				continue;

			arg = "";
			if((cap & CAP_TS6) && mode_changes[i].id)
				arg = mode_changes[i].id;
			if(!*arg)
				arg = mode_changes[i].arg;

			/* if we're creeping past the buf size, we need to send it and make
			 * another line for the other modes
			 * XXX - this could give away server topology with uids being
			 * different lengths, but not much we can do, except possibly break
			 * them as if they were the longest of the nick or uid at all times,
			 * which even then won't work as we don't always know the uid -A1kmm.
			 */
			if((arg != NULL) && ((mc == MAXMODEPARAMS) ||
						((strlen(arg) + mbl + pbl + 2) > BUFSIZE)))
			{
				if(nc != 0)
					sendto_server(client_p, chptr, cap, nocap,
							"%s %s", modebuf, parabuf);
				nc = 0;
				mc = 0;

				if(cap & CAP_TS6)
					mbl = ircsprintf(modebuf, ":%s MODE %s ",
							 use_id(source_p), chptr->chname);
				else
					mbl = ircsprintf(modebuf, ":%s MODE %s ",
							 source_p->name, chptr->chname);

				pbl = 0;
				parabuf[0] = 0;
				dir = MODE_QUERY;
			}

			if(dir != mode_changes[i].dir)
			{
				modebuf[mbl++] = (mode_changes[i].dir == MODE_ADD) ? '+' : '-';
				dir = mode_changes[i].dir;
			}

			modebuf[mbl++] = mode_changes[i].letter;
			modebuf[mbl] = 0;
			nc++;

			if(arg != NULL)
			{
				pbl = strlcat(parabuf, arg, MODEBUFLEN);
				parabuf[pbl++] = ' ';
				parabuf[pbl] = '\0';
				mc++;
			}
		}

		if(pbl && parabuf[pbl - 1] == ' ')
			parabuf[pbl - 1] = 0;

		if(nc != 0)
			sendto_server(client_p, chptr, cap, nocap, "%s %s", modebuf, parabuf);
	}
}
