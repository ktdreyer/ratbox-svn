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
#include "modebuild.h"

static void c_mode(struct client *, const char *parv[], int parc);
struct scommand_handler mode_command = { "MODE", c_mode, 0, DLINK_EMPTY };

/* linked list of services that were deopped */
static dlink_list deopped_list;
static dlink_list opped_list;

/* valid_key()
 *   validates key, and transforms to lower ascii
 *
 * inputs  - key
 * outputs - 'fixed' version of key, NULL if invalid
 */
static const char *
valid_key(const char *data)
{
	static char buf[KEYLEN+1];
	u_char *s, c;
	u_char *fix = buf;

	strlcpy(buf, data, sizeof(buf));

	for(s = (u_char *) buf; (c = *s); s++)
	{
		c &= 0x7f;

		if(c == ':' || c <= ' ')
			return NULL;

		*fix++ = c;
	}

	*fix = '\0';

	return buf;
}

static void
add_ban(const char *banstr, dlink_list *list)
{
	char *ban;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		if(!irccmp((const char *) ptr->data, banstr))
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


int
parse_simple_mode(struct chmode *mode, const char *parv[], int parc, int start)
{
	const char *p = parv[start];
	int dir = 1;

	if(parc <= start)
		return 0;

	start++;

	for(; *p; p++)
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
					mode->mode |= MODE_INVITEONLY;
				else
					mode->mode &= ~MODE_INVITEONLY;
				break;
			case 'm':
				if(dir)
					mode->mode |= MODE_MODERATED;
				else
					mode->mode &= ~MODE_MODERATED;
				break;
			case 'n':
				if(dir)
					mode->mode |= MODE_NOEXTERNAL;
				else
					mode->mode &= ~MODE_NOEXTERNAL;
				break;
			case 'p':
				if(dir)
					mode->mode |= MODE_PRIVATE;
				else
					mode->mode &= ~MODE_PRIVATE;
				break;
			case 's':
				if(dir)
					mode->mode |= MODE_SECRET;
				else
					mode->mode &= ~MODE_SECRET;
				break;
			case 't':
				if(dir)
					mode->mode |= MODE_TOPIC;
				else
					mode->mode &= ~MODE_TOPIC;
				break;

			case 'k':
				if(EmptyString(parv[start]))
					return 0;

				if(dir)
				{
					const char *fixed = valid_key(parv[start]);

					if(fixed == NULL)
						return 0;

					mode->mode |= MODE_KEY;
					strlcpy(mode->key, fixed,
						sizeof(mode->key));
				}
				else
				{
					mode->mode &= ~MODE_KEY;
					mode->key[0] = '\0';
				}

				start++;
				break;
			case 'l':
				if(dir)
				{
					if(EmptyString(parv[start]))
						return 0;

					mode->mode |= MODE_LIMIT;
					mode->limit = atoi(parv[start]);
					start++;
				}
				else
				{
					mode->mode &= ~MODE_LIMIT;
					mode->limit = 0;
				}

				break;

			default:
				return 0;
				break;
		}
	}

	return 1;
}

void
parse_full_mode(struct channel *chptr, struct client *source_p,
		const char **parv, int parc, int start)
{
	const char *p = parv[start];
	int dir = DIR_ADD;

	if(parc <= start)
		return;

	if(source_p)
		modebuild_start(source_p, chptr);

	start++;

	for(; *p; p++)
	{
		switch(*p)
		{
		case '+':
			dir = DIR_ADD;
			break;
		case '-':
			dir = DIR_DEL;
			break;

		case 'i':
			if(dir)
				chptr->mode.mode |= MODE_INVITEONLY;
			else
				chptr->mode.mode &= ~MODE_INVITEONLY;

			if(source_p)
				modebuild_add(dir, "i", NULL);

			break;
		case 'm':
			if(dir)
				chptr->mode.mode |= MODE_MODERATED;
			else
				chptr->mode.mode &= ~MODE_MODERATED;

			if(source_p)
				modebuild_add(dir, "m", NULL);

			break;
		case 'n':
			if(dir)
				chptr->mode.mode |= MODE_NOEXTERNAL;
			else
				chptr->mode.mode &= ~MODE_NOEXTERNAL;

			if(source_p)
				modebuild_add(dir, "n", NULL);

			break;
		case 'p':
			if(dir)
				chptr->mode.mode |= MODE_PRIVATE;
			else
				chptr->mode.mode &= ~MODE_PRIVATE;

			if(source_p)
				modebuild_add(dir, "p", NULL);

			break;
		case 's':
			if(dir)
				chptr->mode.mode |= MODE_SECRET;
			else
				chptr->mode.mode &= ~MODE_SECRET;

			if(source_p)
				modebuild_add(dir, "s", NULL);

			break;
		case 't':
			if(dir)
				chptr->mode.mode |= MODE_TOPIC;
			else
				chptr->mode.mode &= ~MODE_TOPIC;

			if(source_p)
				modebuild_add(dir, "t", NULL);

			break;

		case 'k':
			if(EmptyString(parv[start]))
				return;

			if(dir)
			{
				chptr->mode.mode |= MODE_KEY;
				strlcpy(chptr->mode.key, parv[start],
					sizeof(chptr->mode.key));

				if(source_p)
					modebuild_add(dir, "k",	chptr->mode.key);
			}
			else
			{
				chptr->mode.mode &= ~MODE_KEY;
				chptr->mode.key[0] = '\0';

				if(source_p)
					modebuild_add(dir, "k", "*");
			}


			start++;
			break;
		case 'l':
			if(dir)
			{
				if(EmptyString(parv[start]))
					return;

				chptr->mode.mode |= MODE_LIMIT;
				chptr->mode.limit = atoi(parv[start]);

				/* XXX - modebuild */
				start++;
			}
			else
			{
				chptr->mode.mode &= ~MODE_LIMIT;
				chptr->mode.limit = 0;

				if(source_p)
					modebuild_add(dir, "l", NULL);
			}

			break;

		case 'o':
		case 'v':
		{
			struct client *target_p;
			struct chmember *mptr;
			const char *nick;

			if(EmptyString(parv[start]))
				return;

			nick = parv[start];
			start++;

			if((target_p = find_service(nick)) != NULL)
			{
				/* dont allow generating modes against
				 * services.. dont care about anything other
				 * than +o either.  We lose state of +v on
				 * services, but it doesnt matter.
				 */
				if(source_p || *p != 'o')
					break;

				/* handle -o+o */
				if(dir)
					dlink_find_destroy(target_p, &deopped_list);
				/* this is a -o */
				else if(dlink_find(target_p, &deopped_list) == NULL)
					dlink_add_alloc(target_p, &deopped_list);
			}

			if((target_p = find_user(nick)) == NULL)
				break;

			if((mptr = find_chmember(chptr, target_p)) == NULL)
				break;

			if(*p == 'o')
			{
				if(dir)
				{
					mptr->flags &= ~MODE_DEOPPED;
					mptr->flags |= MODE_OPPED;
					dlink_add_alloc(mptr, &opped_list);
				}
				else
					mptr->flags &= ~MODE_OPPED;

				if(source_p)
					modebuild_add(dir, "o", nick);
			}
			else
			{
				if(dir)
					mptr->flags |= MODE_VOICED;
				else
					mptr->flags &= ~MODE_VOICED;

				if(source_p)
					modebuild_add(dir, "v", nick);
			}

			break;
		}


		case 'b':
			if(EmptyString(parv[start]))
				return;

			if(dir)
				add_ban(parv[start], &chptr->bans);
			else
				del_ban(parv[start], &chptr->bans);

			if(source_p)
				modebuild_add(dir, "b", parv[start]);

			start++;
			break;

		case 'e':
			if(EmptyString(parv[start]))
				return;

			if(dir)
				add_ban(parv[start], &chptr->excepts);
			else
				del_ban(parv[start], &chptr->excepts);

			if(source_p)
				modebuild_add(dir, "e", parv[start]);

			start++;
			break;

		case 'I':
			if(EmptyString(parv[start]))
				return;

			if(dir)
				add_ban(parv[start], &chptr->invites);
			else
				del_ban(parv[start], &chptr->invites);

			if(source_p)
				modebuild_add(dir, "I", parv[start]);

			start++;
			break;
		}
	}

	if(source_p)
		modebuild_finish();
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
	int oldmode;

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

	oldmode = chptr->mode.mode;

	parse_full_mode(chptr, NULL, (const char **) parv, parc, 2);

	if(dlink_list_length(&opped_list))
		hook_call(HOOK_MODE_OP, chptr, &opped_list);

	if(oldmode != chptr->mode.mode)
		hook_call(HOOK_MODE_SIMPLE, chptr, NULL);

	DLINK_FOREACH_SAFE(ptr, next_ptr, opped_list.head)
	{
		free_dlink_node(ptr);
	}

	opped_list.head = opped_list.tail = NULL;
	opped_list.length = 0;

	/* some services were deopped.. */
	DLINK_FOREACH_SAFE(ptr, next_ptr, deopped_list.head)
	{
		target_p = ptr->data;
		rejoin_service(target_p, chptr, 1);
		dlink_destroy(ptr, &deopped_list);
	}
}
