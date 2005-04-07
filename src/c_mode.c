/* src/c_mode.c
 *   Contains code for handling "MODE" command.
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
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
			case 'r':
				if(dir)
					mode->mode |= MODE_REGONLY;
				else
					mode->mode &= ~MODE_REGONLY;
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
		case 'r':
			if(dir)
				chptr->mode.mode |= MODE_REGONLY;
			else
				chptr->mode.mode &= ~MODE_REGONLY;

			if(source_p)
				modebuild_add(dir, "r", NULL);

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
				const char *limit_s;
				char *endptr;
				int limit;

				if(EmptyString(parv[start]))
					return;

				limit_s = parv[start];
				start++;

				limit = strtol(limit_s, &endptr, 10);

				if(limit <= 0)
					return;

				if(source_p)
				{
					/* we used what they passed as the
					 * mode issued, so it has to be valid
					 */
					if(!EmptyString(endptr))
						return;

					modebuild_add(dir, "l", limit_s);
				}

				chptr->mode.mode |= MODE_LIMIT;
				chptr->mode.limit = limit;
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
	struct chmember *msptr;
	dlink_node *ptr;
	dlink_node *next_ptr;
	struct chmode oldmode;

	if(parc < 1 || EmptyString(parv[0]))
		return;

	/* user setting mode:
	 * :<user> MODE <user> +<modes>
	 */
	if(!IsChanPrefix(parv[0][0]))
	{
		if(parc < 2 || EmptyString(parv[1]))
			return;

		if((target_p = find_user(parv[0])) == NULL)
			return;

		if(target_p != client_p)
			return;

		target_p->user->umode = string_to_umode(parv[1], target_p->user->umode);
		return;
	}

	/* channel mode, need 3 params */
	if(parc < 2 || EmptyString(parv[1]))
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	/* user marked as being deopped, bounce mode changes */
	if(IsUser(client_p) && (msptr = find_chmember(chptr, client_p)) &&
	   (msptr->flags & MODE_DEOPPED))
		return;

	oldmode.mode = chptr->mode.mode;
	oldmode.limit = chptr->mode.limit;
	if(EmptyString(chptr->mode.key))
		oldmode.key[0] = '\0';
	else
		strlcpy(oldmode.key, chptr->mode.key, sizeof(chptr->mode.key));

	parse_full_mode(chptr, NULL, (const char **) parv, parc, 1);

	if(dlink_list_length(&opped_list))
		hook_call(HOOK_MODE_OP, chptr, &opped_list);

	if(oldmode.mode != chptr->mode.mode || oldmode.limit != chptr->mode.limit ||
	   strcasecmp(oldmode.key, chptr->mode.key))
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
