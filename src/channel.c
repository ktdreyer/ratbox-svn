/* src/channel.c
 *  Contains code for handling channels.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "client.h"
#include "tools.h"
#include "channel.h"
#include "command.h"
#include "log.h"

static dlink_list channel_table[MAX_CHANNEL_TABLE];
dlink_list channel_list;

static void c_join(struct client *, char *parv[], int parc);
static void c_kick(struct client *, char *parv[], int parc);
static void c_part(struct client *, char *parv[], int parc);
static void c_sjoin(struct client *, char *parv[], int parc);
static void c_topic(struct client *, char *parv[], int parc);

static struct scommand_handler join_command = { "JOIN", c_join, 0 };
static struct scommand_handler kick_command = { "KICK", c_kick, 0 };
static struct scommand_handler part_command = { "PART", c_part, 0 };
static struct scommand_handler sjoin_command = { "SJOIN", c_sjoin, 0 };
static struct scommand_handler topic_command = { "TOPIC", c_topic, 0 };

void
init_channel(void)
{
	add_scommand_handler(&join_command);
	add_scommand_handler(&kick_command);
	add_scommand_handler(&part_command);
	add_scommand_handler(&sjoin_command);
	add_scommand_handler(&topic_command);
}

static unsigned int
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

void
add_channel(struct channel *chptr)
{
	unsigned int hashv = hash_channel(chptr->name);
	dlink_add(chptr, &chptr->nameptr, &channel_table[hashv]);
	dlink_add(chptr, &chptr->listptr, &channel_list);
}

void
del_channel(struct channel *chptr)
{
	unsigned int hashv = hash_channel(chptr->name);
	dlink_delete(&chptr->nameptr, &channel_table[hashv]);
	dlink_delete(&chptr->listptr, &channel_list);
}

struct channel *
find_channel(const char *name)
{
	struct channel *chptr;
	dlink_node *ptr;
	unsigned int hashv = hash_channel(name);

	DLINK_FOREACH(ptr, channel_table[hashv].head)
	{
		chptr = ptr->data;

		if(!irccmp(chptr->name, name))
			return chptr;
	}

	return NULL;
}

void
free_channel(struct channel *chptr)
{
	if(chptr == NULL)
		return;

	del_channel(chptr);

	my_free(chptr);
}

void
add_chmember(struct channel *chptr, struct client *target_p, int flags)
{
	struct chmember *mptr;

	mptr = my_malloc(sizeof(struct chmember));
	mptr->client_p = target_p;
	mptr->chptr = chptr;
	mptr->flags = flags;

	dlink_add(mptr, &mptr->chnode, &chptr->users);
	dlink_add(mptr, &mptr->usernode, &target_p->user->channels);

	slog("added %s to channel %s", target_p->name, chptr->name);
}

void
del_chmember(struct chmember *mptr)
{
	struct channel *chptr;
	struct client *client_p;

	if(mptr == NULL)
		return;

	chptr = mptr->chptr;
	client_p = mptr->client_p;

	slog("removed %s from channel %s", client_p->name, chptr->name);

	dlink_delete(&mptr->chnode, &chptr->users);
	dlink_delete(&mptr->usernode, &client_p->user->channels);

	if(dlink_list_length(&chptr->users) == 0)
		free_channel(chptr);

	my_free(mptr);
}

struct chmember *
find_chmember(struct channel *chptr, struct client *target_p)
{
	struct chmember *mptr;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, target_p->user->channels.head)
	{
		mptr = ptr->data;
		if(mptr->chptr == chptr)
			return mptr;
	}

	return NULL;
}

static void
c_join(struct client *client_p, char *parv[], int parc)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(!IsUser(client_p))
		return;

	/* only thing we should ever get here is join 0 */
	if(parv[1][0] == '0')
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, client_p->user->channels.head)
		{
			del_chmember(ptr->data);
		}
	}
}

static void
c_kick(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;
	struct channel *chptr;
	struct chmember *mptr;

	if(parc < 3 || EmptyString(parv[2]))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
		return;

	if((target_p = find_user(parv[2])) == NULL)
		return;

	if((mptr = find_chmember(chptr, target_p)) == NULL)
		return;

	del_chmember(mptr);
}
		

static void
c_part(struct client *client_p, char *parv[], int parc)
{
	struct chmember *mptr;
	struct channel *chptr;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	if(!IsUser(client_p))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
		return;

	if((mptr = find_chmember(chptr, client_p)) == NULL)
		return;

	del_chmember(mptr);
}

static void
c_topic(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;

	if(parc < 3)
		return;

	if((chptr = find_channel(parv[1])) == NULL)
		return;

	if(EmptyString(parv[2]))
	{
		chptr->topic[0] = '\0';
		chptr->topicwho[0] = '\0';
	}
	else
	{
		strlcpy(chptr->topic, parv[2], sizeof(chptr->topic));

		if(IsUser(client_p))
			snprintf(chptr->topicwho, sizeof(chptr->topicwho), "%s!%s@%s", 
				 client_p->name, client_p->user->username, 
				 client_p->user->host);
		else
			strlcpy(chptr->topicwho, client_p->name, sizeof(chptr->topicwho));
	}
}

static void
remove_our_modes(struct channel *chptr)
{
	chptr->mode.mode = 0;
	chptr->mode.key[0] = '\0';
	chptr->mode.limit = 0;
}

const char *
chmode_to_string(struct channel *chptr)
{
	static char buf[10];
	char *p;

	p = buf;

	*p++ = '+';

	if(chptr->mode.mode & MODE_INVITEONLY)
		*p++ = 'i';
	if(chptr->mode.mode & MODE_MODERATED)
		*p++ = 'm';
	if(chptr->mode.mode & MODE_NOEXTERNAL)
		*p++ = 'n';
	if(chptr->mode.mode & MODE_PRIVATE)
		*p++ = 'p';
	if(chptr->mode.mode & MODE_SECRET)
		*p++ = 's';
	if(chptr->mode.mode & MODE_TOPIC)
		*p++ = 't';
	if(chptr->mode.limit)
		*p++ = 'l';
	if(chptr->mode.key[0])
		*p++ = 'k';

	*p = '\0';
	return buf;
}

static void
c_sjoin(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;
	struct client *target_p;
	struct chmode newmode;
	char *p;
	char *s;
	time_t newts;
	int flags = 0;
	int isnew = 0;
	int keep_new_modes = 1;
	int args = 0;

	if(parc < 5 || EmptyString(parv[4]))
		return;

	if((chptr = find_channel(parv[2])) == NULL)
	{
		chptr = my_malloc(sizeof(struct channel));
		strlcpy(chptr->name, parv[2], sizeof(chptr->name));
		chptr->tsinfo = atol(parv[1]);
		add_channel(chptr);

		isnew = 1;

		slog("CHAN: created channel %s", chptr->name);
	}
	else
	{
		newts = atol(parv[1]);

		if(newts == 0 || chptr->tsinfo == 0)
		{
			chptr->tsinfo = 0;
		}
		else if(newts < chptr->tsinfo)
		{
			chptr->tsinfo = newts;
			remove_our_modes(chptr);
		}
		else if(chptr->tsinfo < newts)
		{
			keep_new_modes = 0;
		}
	}

	newmode.mode = 0;
	newmode.key[0] = '\0';
	newmode.limit = 0;

	s = parv[3];

	while(*s)
	{
		/* skips the leading '+' */
		switch(*(s++))
		{
		case 'i':
			newmode.mode |= MODE_INVITEONLY;
			break;
		case 'm':
			newmode.mode |= MODE_MODERATED;
			break;
		case 'n':
			newmode.mode |= MODE_NOEXTERNAL;
			break;
		case 'p':
			newmode.mode |= MODE_PRIVATE;
			break;
		case 's':
			newmode.mode |= MODE_SECRET;
			break;
		case 't':
			newmode.mode |= MODE_TOPIC;
			break;
		case 'k':
			strlcpy(newmode.key, parv[4+args], sizeof(newmode.key));
			args++;
			
			if(parc < 5+args)
				return;
			break;
		case 'l':
			newmode.limit = atoi(parv[4+args]);
			args++;

			if(parc < 5+args)
				return;
			break;
		default:
			break;
		}
	}

	if(keep_new_modes)
	{
		chptr->mode.mode |= newmode.mode;

		if(!chptr->mode.limit || chptr->mode.limit < newmode.limit)
			chptr->mode.limit = newmode.limit;

		if(!chptr->mode.key[0] || strcmp(chptr->mode.key, newmode.key) > 0)
			strlcpy(chptr->mode.key, newmode.key,
				sizeof(chptr->mode.key));
	}

	if(EmptyString(parv[4+args]))
		return;

	for(s = parv[4+args]; !EmptyString(s); s = p)
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

		if((target_p = find_user(s)) == NULL)
			continue;

		if(!is_member(chptr, target_p))
			add_chmember(chptr, target_p, flags);
	}

	/* didnt join any members, nuke it */
	if(dlink_list_length(&chptr->users) == 0)
		free_channel(chptr);
}
