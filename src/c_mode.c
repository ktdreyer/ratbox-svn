/* src/c_mode.c
 *   Contains code for handling "MODE" command.
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "c_init.h"
#include "client.h"
#include "channel.h"
#include "scommand.h"
#include "log.h"
#include "hook.h"

static void c_mode(struct client *, const char *parv[], int parc);
struct scommand_handler mode_command = { "MODE", c_mode, 0, DLINK_EMPTY };

/* linked list of services that were deopped */
dlink_list deopped_list;
dlink_list opped_list;

/* change_chmember_status()
 *   changes a channel members +ov status
 *
 * inputs	- channel to change on, nick to change, mode to change,
 *		  direction
 * outputs	-
 */
static void
change_chmember_status(struct channel *chptr, const char *nick, 
			char type, int dir)
{
	struct client *target_p;
	struct chmember *mptr;

	if((target_p = find_service(nick)) != NULL)
	{
		/* we only care about -o */
		if(type != 'o' || dir)
			return;

		if(dlink_find(&deopped_list, target_p) == NULL)
			dlink_add_alloc(target_p, &deopped_list);

		return;
	}

	if((target_p = find_user(nick)) == NULL)
		return;

	if((mptr = find_chmember(chptr, target_p)) == NULL)
		return;

	if(type == 'o')
	{
		if(dir)
		{
			mptr->flags &= ~MODE_DEOPPED;
			mptr->flags |= MODE_OPPED;
			dlink_add_alloc(mptr, &opped_list);
		}
		else
			mptr->flags &= ~MODE_OPPED;
	}
	else if(type == 'v')
	{
		if(dir)
			mptr->flags |= MODE_VOICED;
		else
			mptr->flags &= ~MODE_VOICED;
	}
}

static void
add_ban(const char *banstr, dlink_list *list)
{
	char *ban;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		if(match((const char *) ptr->data, banstr))
			return;
	}

	ban = my_strdup(banstr);
	dlink_add_alloc(ban, list);
}

static void
del_ban(const char *banstr, dlink_list *list)
{
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		if(!irccmp(banstr, (const char *) ptr->data))
		{
			my_free(ptr->data);
			dlink_destroy(ptr, list);
			return;
		}
	}
}

/* c_mode()
 *   the MODE handler
 */
static void
c_mode(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;
	struct channel *chptr;
	dlink_node *ptr;
	dlink_node *next_ptr;
	const char *p;
	int args = 0;
	int dir = 1;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	/* user setting mode:
	 * :<user> MODE <user> +<modes>
	 */
	if(!IsChanPrefix(parv[1][0]))
	{
		if(parc < 3 || EmptyString(parv[2]))
			return;

		if((target_p = find_user(parv[1])) == NULL)
			return;

		if(target_p != client_p)
			return;

		target_p->user->umode = string_to_umode(parv[2], target_p->user->umode);
		return;
	}

	/* channel mode, need 3 params */
	if(parc < 3 || EmptyString(parv[2]))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
		return;

	p = parv[2];

	while(*p)
	{
		switch(*p)
		{
			case '+':
				dir = 1;
				break;
			case '-':
				dir = 0;
				break;

			case 'i':
				if(dir)
					chptr->mode.mode |= MODE_INVITEONLY;
				else
					chptr->mode.mode &= ~MODE_INVITEONLY;
				break;
			case 'm':
				if(dir)
					chptr->mode.mode |= MODE_MODERATED;
				else
					chptr->mode.mode &= ~MODE_MODERATED;
				break;
			case 'n':
				if(dir)
					chptr->mode.mode |= MODE_NOEXTERNAL;
				else
					chptr->mode.mode &= ~MODE_NOEXTERNAL;
				break;
			case 'p':
				if(dir)
					chptr->mode.mode |= MODE_PRIVATE;
				else
					chptr->mode.mode &= ~MODE_PRIVATE;
				break;
			case 's':
				if(dir)
					chptr->mode.mode |= MODE_SECRET;
				else
					chptr->mode.mode &= ~MODE_SECRET;
				break;
			case 't':
				if(dir)
					chptr->mode.mode |= MODE_TOPIC;
				else
					chptr->mode.mode &= ~MODE_TOPIC;
				break;

			case 'k':
				if(EmptyString(parv[3+args]))
					return;

				if(dir)
					strlcpy(chptr->mode.key, parv[3+args],
						sizeof(chptr->mode.key));
				else
					chptr->mode.key[0] = '\0';

				args++;
				break;
			case 'l':
				if(dir)
				{
					if(EmptyString(parv[3+args]))
						return;

					chptr->mode.limit = atoi(parv[3+args]);
					args++;
				}
				else
					chptr->mode.limit = 0;

				break;

			case 'o':
			case 'v':
				if(EmptyString(parv[3+args]))
					return;

				change_chmember_status(chptr, parv[3+args], *p, dir);
				args++;
				break;

			/* we dont need to parse these at this point */
			case 'b':
				if(EmptyString(parv[3+args]))
					return;

				if(dir)
					add_ban(parv[3+args], &chptr->bans);
				else
					del_ban(parv[3+args], &chptr->bans);

				args++;
				break;
	
			case 'e':
				if(EmptyString(parv[3+args]))
					return;

				if(dir)
					add_ban(parv[3+args], &chptr->excepts);
				else
					del_ban(parv[3+args], &chptr->excepts);

				args++;
				break;

			case 'I':
				if(EmptyString(parv[3+args]))
					return;

				if(dir)
					add_ban(parv[3+args], &chptr->invites);
				else
					del_ban(parv[3+args], &chptr->invites);

				args++;
				break;
		}

		p++;
	}

	if(dlink_list_length(&opped_list))
		hook_call(HOOK_MODE_OP, &opped_list, NULL);

	DLINK_FOREACH_SAFE(ptr, next_ptr, opped_list.head)
	{
		free_dlink_node(ptr);
	}

	opped_list.head = opped_list.tail = NULL;
	opped_list.length = 0;

	/* some services were deopped.. */
	if(dlink_list_length(&deopped_list))
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, deopped_list.head)
		{
			target_p = ptr->data;
			rejoin_service(target_p, chptr);
			dlink_destroy(ptr, &deopped_list);
		}
	}
}
