/* src/channel.c
 *   Contains code for handling changes within channels.
 *
 * Copyright (C) 2003-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2012 ircd-ratbox development team
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
#include "rserv.h"
#include "client.h"
#include "conf.h"

#include "channel.h"
#include "scommand.h"
#include "log.h"
#include "balloc.h"
#include "io.h"
#include "hook.h"
#include "modebuild.h"
#include "tools.h"

static rb_dlink_list channel_table[MAX_CHANNEL_TABLE];
rb_dlink_list channel_list;

static rb_bh *channel_heap;
static rb_bh *chmember_heap;

static void c_join(struct client *, const char *parv[], int parc);
static void c_kick(struct client *, const char *parv[], int parc);
static void c_part(struct client *, const char *parv[], int parc);
static void c_sjoin(struct client *, const char *parv[], int parc);
static void c_tb(struct client *, const char *parv[], int parc);
static void c_topic(struct client *, const char *parv[], int parc);

static struct scommand_handler join_command = { "JOIN", c_join, 0, DLINK_EMPTY };
static struct scommand_handler kick_command = { "KICK", c_kick, 0, DLINK_EMPTY };
static struct scommand_handler part_command = { "PART", c_part, 0, DLINK_EMPTY };
static struct scommand_handler sjoin_command = { "SJOIN", c_sjoin, 0, DLINK_EMPTY };
static struct scommand_handler tb_command = { "TB", c_tb, 0, DLINK_EMPTY };
static struct scommand_handler topic_command = { "TOPIC", c_topic, 0, DLINK_EMPTY };

static struct _chmode_table
{
	unsigned long mode;	/* bitmask of mode */
	const char *modestr;	/* mode character */
	int prevent_join;	/* this mode prevents users joining? */
} chmode_table[] = {
	{ MODE_INVITEONLY,	"i", 1 },
	{ MODE_MODERATED,	"m", 0 },
	{ MODE_NOEXTERNAL,	"n", 0 },
	{ MODE_PRIVATE,		"p", 0 },
	{ MODE_SECRET,		"s", 0 },
	{ MODE_TOPIC,		"t", 0 },
	{ MODE_REGONLY,		"r", 1 },
	{ MODE_SSLONLY,		"S", 1 },
	{ 0, NULL }
};
	

/* init_channel()
 *   initialises various things
 */
void
init_channel(void)
{
        channel_heap = rb_bh_create(sizeof(struct channel), HEAP_CHANNEL, "Channel");
        chmember_heap = rb_bh_create(sizeof(struct chmember), HEAP_CHMEMBER, "Channel Member");

	add_scommand_handler(&join_command);
	add_scommand_handler(&kick_command);
	add_scommand_handler(&part_command);
	add_scommand_handler(&sjoin_command);
	add_scommand_handler(&tb_command);
	add_scommand_handler(&topic_command);
}

/* hash_channel()
 *   hashes the name of a channel
 *
 * inputs	- channel name
 * outputs	- hash value
 */
unsigned int
hash_channel(const char *p)
{
	int i = 30;
	unsigned int h = 0;

	while(*p && --i)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*p++));
	}

	return(h & (MAX_CHANNEL_TABLE-1));
}

int
valid_chname(const char *name)
{
	if(strlen(name) > CHANNELLEN)
		return 0;

	if(name[0] != '#')
		return 0;

	return 1;
}

/* add_channel()
 *   adds a channel to the internal hash, and channel_list
 *
 * inputs	- channel to add
 * outputs	-
 */
void
add_channel(struct channel *chptr)
{
	unsigned int hashv = hash_channel(chptr->name);
	rb_dlinkAdd(chptr, &chptr->nameptr, &channel_table[hashv]);
	rb_dlinkAdd(chptr, &chptr->listptr, &channel_list);
}

/* del_channel()
 *   removes a channel from the internal hash and channel_list
 *
 * inputs	- channel to remove
 * outputs	-
 */
void
del_channel(struct channel *chptr)
{
	unsigned int hashv = hash_channel(chptr->name);
	rb_dlinkDelete(&chptr->nameptr, &channel_table[hashv]);
	rb_dlinkDelete(&chptr->listptr, &channel_list);
}

/* find_channel()
 *   hunts for a channel in the hash
 *
 * inputs	- channel name to find
 * outputs	- channel struct, or NULL if not found
 */
struct channel *
find_channel(const char *name)
{
	struct channel *chptr;
	rb_dlink_node *ptr;
	unsigned int hashv = hash_channel(name);

	RB_DLINK_FOREACH(ptr, channel_table[hashv].head)
	{
		chptr = ptr->data;

		if(!irccmp(chptr->name, name))
			return chptr;
	}

	return NULL;
}

/* free_channel()
 *   removes a channel from hash, and free's the memory its using
 *
 * inputs	- channel to free
 * outputs	-
 */
void
free_channel(struct channel *chptr)
{
	if(chptr == NULL)
		return;

	hook_call(HOOK_CHANNEL_DESTROY, chptr, NULL);

	del_channel(chptr);

	rb_bh_free(channel_heap, chptr);
}

/* add_chmember()
 *   adds a given client to a given channel with given flags
 *
 * inputs	- channel to add to, client to add, flags
 * outputs	-
 */
struct chmember *
add_chmember(struct channel *chptr, struct client *target_p, int flags)
{
	struct chmember *mptr;

	mptr = rb_bh_alloc(chmember_heap);

	mptr->client_p = target_p;
	mptr->chptr = chptr;
	mptr->flags = flags;

	rb_dlinkAdd(mptr, &mptr->chnode, &chptr->users);
	rb_dlinkAdd(mptr, &mptr->usernode, &target_p->user->channels);

	if(is_opped(mptr))
		rb_dlinkAdd(mptr, &mptr->choppednode, &chptr->users_opped);
	else
		rb_dlinkAdd(mptr, &mptr->choppednode, &chptr->users_unopped);

	return mptr;
}

/* del_chmember()
 *   removes a given member from a channel
 *
 * inputs	- chmember to remove
 * outputs	-
 */
void
del_chmember(struct chmember *mptr)
{
	struct channel *chptr;
	struct client *client_p;

	if(mptr == NULL)
		return;

	chptr = mptr->chptr;
	client_p = mptr->client_p;

	rb_dlinkDelete(&mptr->chnode, &chptr->users);
	rb_dlinkDelete(&mptr->usernode, &client_p->user->channels);

	if(is_opped(mptr))
	{
		rb_dlinkDelete(&mptr->choppednode, &chptr->users_opped);
		if(rb_dlink_list_length(&chptr->users_opped) == 0)
			hook_call(HOOK_CHANNEL_OPLESS, chptr, NULL);
	}
	else
		rb_dlinkDelete(&mptr->choppednode, &chptr->users_unopped);

	if(rb_dlink_list_length(&chptr->users) == 0 &&
			rb_dlink_list_length(&chptr->services) == 0)
		free_channel(chptr);

	rb_bh_free(chmember_heap, mptr);
}

/* find_chmember()
 *   hunts for a chmember struct for the given user in given channel
 *
 * inputs	- channel to search, client to search for
 * outputs	- chmember struct if found, else NULL
 */
struct chmember *
find_chmember(struct channel *chptr, struct client *target_p)
{
	struct chmember *mptr;
	rb_dlink_node *ptr;

	if (rb_dlink_list_length(&chptr->users) < rb_dlink_list_length(&target_p->user->channels))
	{
		RB_DLINK_FOREACH(ptr, chptr->users.head)
		{
			mptr = ptr->data;
			if(mptr->client_p == target_p)
				return mptr;
		}
	}
	else
	{
		RB_DLINK_FOREACH(ptr, target_p->user->channels.head)
		{
			mptr = ptr->data;
			if(mptr->chptr == chptr)
				return mptr;
		}
	}

	return NULL;
}

/* op_chmember()
 *   ops a chmember, ensuring list integrity is saved
 *
 * inputs	- membership struct to op
 * outputs	-
 */
void
op_chmember(struct chmember *member_p)
{
	if(is_opped(member_p))
		return;

	member_p->flags &= ~MODE_DEOPPED;
	member_p->flags |= MODE_OPPED;
	rb_dlinkMoveNode(&member_p->choppednode, &member_p->chptr->users_unopped,
			&member_p->chptr->users_opped);
}

/* deop_chmember()
 *   deops a chmember, ensuring list integrity is saved
 *
 * inputs	- membership struct to deop
 * outputs	-
 */
void
deop_chmember(struct chmember *member_p)
{
	if(!is_opped(member_p))
		return;

	member_p->flags &= ~MODE_OPPED;
	rb_dlinkMoveNode(&member_p->choppednode, &member_p->chptr->users_opped,
			&member_p->chptr->users_unopped);

	if(rb_dlink_list_length(&member_p->chptr->users_opped) == 0)
		hook_call(HOOK_CHANNEL_OPLESS, member_p->chptr, NULL);
}


int
find_exempt(struct channel *chptr, struct client *target_p)
{
	rb_dlink_node *ptr;

	RB_DLINK_FOREACH(ptr, chptr->excepts.head)
	{
		if(match((const char *) ptr->data, target_p->user->mask))
			return 1;
	}

	return 0;
}

/* count_topics()
 *   counts the number of channels which have a topic
 *
 * inputs       -
 * outputs      - number of channels with topics
 */
unsigned long
count_topics(void)
{
        struct channel *chptr;
        rb_dlink_node *ptr;
        unsigned long topic_count = 0;

        RB_DLINK_FOREACH(ptr, channel_list.head)
        {
                chptr = ptr->data;

                if(chptr->topic[0] != '\0')
                        topic_count++;
        }

        return topic_count;
}

/* join service to chname, create channel with TS tsinfo, using mode in the
 * SJOIN. if channel already exists, don't use tsinfo -- jilles */
/* that is, unless override is specified */
void
join_service(struct client *service_p, const char *chname, time_t tsinfo,
		struct chmode *mode, int override)
{
	struct channel *chptr;

	/* channel doesnt exist, have to join it */
	if((chptr = find_channel(chname)) == NULL)
	{
		chptr = rb_bh_alloc(channel_heap);

		rb_strlcpy(chptr->name, chname, sizeof(chptr->name));
		chptr->tsinfo = tsinfo ? tsinfo : rb_time();

		if(mode != NULL)
		{
			chptr->mode.mode = mode->mode;
			chptr->mode.limit = mode->limit;

			if(mode->key[0])
				rb_strlcpy(chptr->mode.key, mode->key,
					sizeof(chptr->mode.key));
		}
		else
			chptr->mode.mode = MODE_NOEXTERNAL|MODE_TOPIC;

		add_channel(chptr);
	}
	/* may already be joined.. */
	else if(rb_dlinkFind(service_p, &chptr->services) != NULL)
	{
		return;
	}
	else if(override && tsinfo < chptr->tsinfo)
	{
		chptr->tsinfo = tsinfo;

		if(mode != NULL)
		{
			chptr->mode.mode = mode->mode;
			chptr->mode.limit = mode->limit;

			if(mode->key[0])
				rb_strlcpy(chptr->mode.key, mode->key,
					sizeof(chptr->mode.key));
		}
		else
			chptr->mode.mode = MODE_NOEXTERNAL|MODE_TOPIC;
	}

	rb_dlinkAddAlloc(service_p, &chptr->services);
	rb_dlinkAddAlloc(chptr, &service_p->service->channels);

	if(sent_burst)
		sendto_server(":%s SJOIN %lu %s %s :@%s",
				MYUID, (unsigned long) chptr->tsinfo, 
				chptr->name, chmode_to_string(&chptr->mode), 
				SVC_UID(service_p));
}

int
part_service(struct client *service_p, const char *chname)
{
	struct channel *chptr;

	if((chptr = find_channel(chname)) == NULL)
		return 0;

	if(rb_dlinkFind(service_p, &chptr->services) == NULL)
		return 0;

	rb_dlinkFindDestroy(service_p, &chptr->services);
	rb_dlinkFindDestroy(chptr, &service_p->service->channels);

	if(sent_burst)
		sendto_server(":%s PART %s", SVC_UID(service_p), chptr->name);

	if(rb_dlink_list_length(&chptr->users) == 0 &&
	   rb_dlink_list_length(&chptr->services) == 0)
		free_channel(chptr);

	return 1;
}

void
rejoin_service(struct client *service_p, struct channel *chptr, int reop)
{
	/* we are only doing this because we need a reop */
	if(reop)
	{
		/* can do this rather more simply */
		if(config_file.ratbox)
		{
			sendto_server(":%s MODE %s +o %s",
					MYUID, chptr->name, SVC_UID(service_p));
			return;
		}

		sendto_server(":%s PART %s", SVC_UID(service_p), chptr->name);
	}

	sendto_server(":%s SJOIN %lu %s %s :@%s",
			MYUID, (unsigned long) chptr->tsinfo, chptr->name, 
			chmode_to_string(&chptr->mode),  SVC_UID(service_p));
}

/* c_kick()
 *   the KICK handler
 */
static void
c_kick(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;
	struct channel *chptr;
	struct chmember *mptr;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	if((target_p = find_service(parv[1])) != NULL)
	{
		rejoin_service(target_p, chptr, 0);
		return;
	}

	if((target_p = find_user(parv[1], 1)) == NULL)
		return;

	if((mptr = find_chmember(chptr, target_p)) == NULL)
		return;

	del_chmember(mptr);
}
		
/* c_part()
 *   the PART handler
 */
static void
c_part(struct client *client_p, const char *parv[], int parc)
{
	struct chmember *mptr;
	struct channel *chptr;

	if(parc < 1 || EmptyString(parv[0]))
		return;

	if(!IsUser(client_p))
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	if((mptr = find_chmember(chptr, client_p)) == NULL)
		return;

	del_chmember(mptr);
}

/* c_topic()
 *   the TOPIC handler
 */
static void
c_topic(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if(parc < 2)
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	if(EmptyString(parv[1]))
	{
		chptr->topic[0] = '\0';
		chptr->topicwho[0] = '\0';
		chptr->topic_tsinfo = 0;
	}
	else
	{
		rb_strlcpy(chptr->topic, parv[1], sizeof(chptr->topic));

		if(IsUser(client_p))
			snprintf(chptr->topicwho, sizeof(chptr->topicwho), "%s!%s@%s", 
				 client_p->name, client_p->user->username, 
				 client_p->user->host);
		else
			rb_strlcpy(chptr->topicwho, client_p->name, sizeof(chptr->topicwho));

		chptr->topic_tsinfo = rb_time();
	}

	hook_call(HOOK_CHANNEL_TOPIC, chptr, NULL);
}

/* c_tb()
 *   the TB handler
 */
static void
c_tb(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;
	time_t topicts;

	if(parc < 3 || !IsServer(client_p))
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	topicts = atol(parv[1]);

	/* If we have a topic that is older than the one burst to us, ours
	 * wins -- otherwise process the topic burst
	 */
	if(!EmptyString(chptr->topic) && topicts > chptr->topic_tsinfo)
		return;

	/* :<server> TB <#channel> <topicts> <topicwho> :<topic> */
	if(parc == 4)
	{
		if(EmptyString(parv[3]))
			return;

		rb_strlcpy(chptr->topic, parv[3], sizeof(chptr->topic));
		rb_strlcpy(chptr->topicwho, parv[2], sizeof(chptr->topicwho));
		chptr->topic_tsinfo = rb_time();
	}
	/* :<server> TB <#channel> <topicts> :<topic> */
	else
	{
		if(EmptyString(parv[2]))
			return;

		rb_strlcpy(chptr->topic, parv[2], sizeof(chptr->topic));
		rb_strlcpy(chptr->topicwho, client_p->name, sizeof(chptr->topicwho));
		chptr->topic_tsinfo = rb_time();
	}

	hook_call(HOOK_CHANNEL_TOPIC, chptr, NULL);
}

/* remove_our_simple_modes()
 *   clears our simple channel modes from a channel
 *
 * inputs	- channel to remove simple modes from, service client to issue
 * 		  modebuilds from (or NULL to not), remove only modes
 * 		  preventing users joining or not
 * outputs	-
 */
void
remove_our_simple_modes(struct channel *chptr, struct client *service_p,
			int prevent_join)
{
	if(service_p)
	{
		int i;

		modebuild_start(service_p, chptr);

		for(i = 0; chmode_table[i].mode; i++)
		{
			if(prevent_join & !chmode_table[i].prevent_join)
				continue;

			if(chptr->mode.mode & chmode_table[i].mode)
				modebuild_add(DIR_DEL, chmode_table[i].modestr, NULL);
		}

		if(chptr->mode.key[0])
		{
			modebuild_add(DIR_DEL, "k", "*");
			chptr->mode.mode &= ~MODE_KEY;
		}

		if(chptr->mode.limit)
		{
			modebuild_add(DIR_DEL, "l", NULL);
			chptr->mode.mode &= ~MODE_LIMIT;
		}

		modebuild_finish();
	}

	/* clear the modes */
	chptr->mode.mode = 0;
	chptr->mode.key[0] = '\0';
	chptr->mode.limit = 0;
}

/* remove_our_ov_modes()
 *   clears our +ov channel modes from a channel
 *
 * inputs	- channel to remove +ov modes from
 * outputs	-
 */
void
remove_our_ov_modes(struct channel *chptr)
{
	struct chmember *msptr;
	rb_dlink_node *ptr;

	RB_DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;

		msptr->flags &= ~(MODE_OPPED|MODE_VOICED);

		/* users_opped/users_unopped integrity done en mass below */
	}

	rb_dlinkMoveList(&chptr->users_opped, &chptr->users_unopped);
}


/* remove_our_bans()
 *   clears +beI modes from a channel
 *
 * inputs	- channel to remove modes from, service client to issue
 * 		  modebuilds from (or NULL to not), whether to remove +b, 
 *		  +e, +I
 * outputs	-
 */
void
remove_our_bans(struct channel *chptr, struct client *service_p, 
		int remove_bans, int remove_exceptions, int remove_invex)
{
	rb_dlink_node *ptr, *next_ptr;

	if(service_p)
		modebuild_start(service_p, chptr);

	if(remove_bans)
	{
		RB_DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->bans.head)
		{
			if(service_p)
				modebuild_add(DIR_DEL, "b", ptr->data);

			rb_free(ptr->data);
			rb_dlinkDestroy(ptr, &chptr->bans);
		}
	}

	if(remove_exceptions)
	{
		RB_DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->excepts.head)
		{
			if(service_p)
				modebuild_add(DIR_DEL, "e", ptr->data);

			rb_free(ptr->data);
			rb_dlinkDestroy(ptr, &chptr->excepts);
		}
	}

	if(remove_invex)
	{
		RB_DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->invites.head)
		{
			if(service_p)
				modebuild_add(DIR_DEL, "I", ptr->data);

			rb_free(ptr->data);
			rb_dlinkDestroy(ptr, &chptr->invites);
		}
	}

	if(service_p)
		modebuild_finish();
}

/* has_prevent_join_mode()
 *   checks whether a channel has modes preventing users from joining
 *
 * inputs	- channel to check for prevent join modes
 * outputs	- true or false
 */
bool
has_prevent_join_mode(struct channel *chptr)
{
	int i;

	for(i = 0; chmode_table[i].mode; i++)
	{
		if(chmode_table[i].prevent_join &&
				(chptr->mode.mode & chmode_table[i].mode))
			return true;
	}

	if(chptr->mode.key[0] || chptr->mode.limit)
		return true;

	return false;
}

/* chmode_to_string()
 *   converts a channels mode into a string
 *
 * inputs	- channel to get modes for
 * outputs	- string version of modes
 */
const char *
chmode_to_string(struct chmode *mode)
{
	static char buf[BUFSIZE];
	char *p;
	int i;

	p = buf;

	*p++ = '+';

	for(i = 0; chmode_table[i].mode; i++)
	{
		if(mode->mode & chmode_table[i].mode)
			*p++ = chmode_table[i].modestr[0];
	}

	if(mode->limit && mode->key[0])
	{
		sprintf(p, "lk %d %s", mode->limit, mode->key);
	}
	else if(mode->limit)
	{
		sprintf(p, "l %d", mode->limit);
	}
	else if(mode->key[0])
	{
		sprintf(p, "k %s", mode->key);
	}
	else
		*p = '\0';

	return buf;
}

/* chmode_to_string_string()
 *   converts a channels mode into a simple string (doesnt contain key/limit)
 *
 * inputs	- channel to get modes for
 * outputs	- string version of modes
 */
const char *
chmode_to_string_simple(struct chmode *mode)
{
	static char buf[BUFSIZE];
	char *p;
	int i;

	p = buf;

	*p++ = '+';

	for(i = 0; chmode_table[i].mode; i++)
	{
		if(mode->mode & chmode_table[i].mode)
			*p++ = chmode_table[i].modestr[0];
	}

	if(mode->limit)
		*p++ = 'l';
	if(mode->key[0])
		*p++ = 'k';

	*p = '\0';

	return buf;
}

/* c_sjoin()
 *   the SJOIN handler
 */
static void
c_sjoin(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct client *target_p;
	struct chmode newmode;
	struct chmember *member_p;
	rb_dlink_list joined_members;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	char *p;
	const char *s;
	char *nicks;
	time_t newts;
	int flags = 0;
	int keep_old_modes = 1;
	int keep_new_modes = 1;
	int args = 0;

	memset(&joined_members, 0, sizeof(rb_dlink_list));

	/* :<server> SJOIN <TS> <#channel> +[modes [key][limit]] :<nicks> */
	if(parc < 4 || EmptyString(parv[3]))
		return;

	if(!valid_chname(parv[1]))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
	{
		chptr = rb_bh_alloc(channel_heap);

		rb_strlcpy(chptr->name, parv[1], sizeof(chptr->name));
		newts = chptr->tsinfo = atol(parv[0]);
		add_channel(chptr);

		keep_old_modes = 0;
	}
	else
	{
		newts = atol(parv[0]);

		if(newts == 0 || chptr->tsinfo == 0)
			chptr->tsinfo = 0;
		else if(newts < chptr->tsinfo)
			keep_old_modes = 0;
		else if(chptr->tsinfo < newts)
			keep_new_modes = 0;
	}

	newmode.mode = 0;
	newmode.key[0] = '\0';
	newmode.limit = 0;

	/* mode of 0 is sent when someone joins remotely with higher TS. */
	if(strcmp(parv[2], "0"))
	{
		args = parse_simple_mode(&newmode, parv, parc, 2, 1);

		/* invalid mode */
		s_assert(args);
		if(!args)
		{
			mlog("PROTO: SJOIN issued with invalid mode: %s",
				rebuild_params(parv, parc, 2));
			return;
		}
	}
	else
		args = 3;

	if(!keep_old_modes)
	{
		chptr->tsinfo = newts;
		remove_our_simple_modes(chptr, NULL, 0);
		remove_our_ov_modes(chptr);
		/* If the source does TS6, also remove all +beI modes */
		if (!EmptyString(client_p->uid))
			remove_our_bans(chptr, NULL, 1, 1, 1);

		/* services is in there.. rejoin */
		if(sent_burst)
		{
			RB_DLINK_FOREACH(ptr, chptr->services.head)
			{
				rejoin_service(ptr->data, chptr, 1);
			}
		}
	}

	if(keep_new_modes)
	{
		chptr->mode.mode |= newmode.mode;

		if(!chptr->mode.limit || chptr->mode.limit < newmode.limit)
			chptr->mode.limit = newmode.limit;

		if(!chptr->mode.key[0] || strcmp(chptr->mode.key, newmode.key) > 0)
			rb_strlcpy(chptr->mode.key, newmode.key,
				sizeof(chptr->mode.key));

	}

	/* this must be done after we've updated the modes */
	if(!keep_old_modes)
		hook_call(HOOK_CHANNEL_SJOIN_LOWERTS, chptr, NULL);

	if(EmptyString(parv[args]))
		return;

	nicks = LOCAL_COPY(parv[args]);

        /* now parse the nicklist */
	for(s = nicks; !EmptyString(s); s = p)
	{
		flags = 0;

		/* remove any leading spaces.. */
		while(*s == ' ')
			s++;

		/* point p to the next nick */
		if((p = strchr(s, ' ')) != NULL)
			*p++ = '\0';

		if(*s == '@')
		{
			flags |= MODE_OPPED;
			s++;

			if(*s == '+')
			{
				flags |= MODE_VOICED;
				s++;
			}
		}
		else if(*s == '+')
		{
			flags |= MODE_VOICED;
			s++;

			if(*s == '@')
			{
				flags |= MODE_OPPED;
				s++;
			}
		}

		if(!keep_new_modes)
		{
			if(flags & MODE_OPPED)
				flags = MODE_DEOPPED;
			else
				flags = 0;
		}

		if((target_p = find_user(s, 1)) == NULL)
			continue;

		if(!is_member(chptr, target_p))
		{
			member_p = add_chmember(chptr, target_p, flags);
			rb_dlinkAddAlloc(member_p, &joined_members);
		}
	}

	/* we didnt join any members in the sjoin above, so destroy the
	 * channel we just created.  This has to be tested before we call the
	 * hook, as the hook may empty the channel and free it itself.
	 */
	if(rb_dlink_list_length(&chptr->users) == 0 &&
	   rb_dlink_list_length(&chptr->services) == 0)
		free_channel(chptr);
	else
		hook_call(HOOK_CHANNEL_JOIN, chptr, &joined_members);

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, joined_members.head)
	{
		rb_free_rb_dlink_node(ptr);
	}
}

/* c_join()
 *   the JOIN handler
 */
static void
c_join(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct chmember *member_p;
	struct chmode newmode;
	rb_dlink_list joined_members;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	time_t newts;
	int keep_old_modes = 1;
	int args = 0;

	if(parc < 0 || EmptyString(parv[0]))
		return;

	if(!IsUser(client_p))
		return;

	memset(&joined_members, 0, sizeof(rb_dlink_list));

	/* check for join 0 first */
	if(parc == 1 && parv[0][0] == '0')
	{
		RB_DLINK_FOREACH_SAFE(ptr, next_ptr, client_p->user->channels.head)
		{
			del_chmember(ptr->data);
		}
		return;
	}

	/* a TS6 join */
	if(parc < 3)
	{
		mlog("PROTO: JOIN issued with insufficient parameters");
		return;
	}

	if(!valid_chname(parv[1]))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
	{
		chptr = rb_bh_alloc(channel_heap);

		rb_strlcpy(chptr->name, parv[1], sizeof(chptr->name));
		newts = chptr->tsinfo = atol(parv[0]);
		add_channel(chptr);

		keep_old_modes = 0;
	}
	else
	{
		newts = atol(parv[0]);

		if(newts == 0 || chptr->tsinfo == 0)
			chptr->tsinfo = 0;
		else if(newts < chptr->tsinfo)
			keep_old_modes = 0;
	}

	newmode.mode = 0;
	newmode.key[0] = '\0';
	newmode.limit = 0;

	args = parse_simple_mode(&newmode, parv, parc, 2, 1);

	/* invalid mode */
	s_assert(args);
	if(!args)
	{
		mlog("PROTO: JOIN issued with invalid modestring: %s",
			rebuild_params(parv, parc, 2));
		return;
	}

	if(!keep_old_modes)
	{
		chptr->tsinfo = newts;
		remove_our_simple_modes(chptr, NULL, 0);
		remove_our_ov_modes(chptr);
		/* Note that JOIN does not remove bans */

		/* services is in there.. rejoin */
		if(sent_burst)
		{
			RB_DLINK_FOREACH(ptr, chptr->services.head)
			{
				rejoin_service(ptr->data, chptr, 1);
			}
		}
	}

	/* this must be done after we've updated the modes */
	if(!keep_old_modes)
		hook_call(HOOK_CHANNEL_SJOIN_LOWERTS, chptr, NULL);

	if(!is_member(chptr, client_p))
	{
		member_p = add_chmember(chptr, client_p, 0);
		rb_dlinkAddAlloc(member_p, &joined_members);
	}

	hook_call(HOOK_CHANNEL_JOIN, chptr, &joined_members);

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, joined_members.head)
	{
		rb_free_rb_dlink_node(ptr);
	}

}


