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

static void c_mode(struct client *, char *parv[], int parc);
struct scommand_handler mode_command = { "MODE", c_mode, 0 };

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

/* c_mode()
 *   the MODE handler
 */
static void
c_mode(struct client *client_p, char *parv[], int parc)
{
	struct client *target_p;
	struct channel *chptr;
	char *p;
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
			case 'e':
			case 'I':
				if(EmptyString(parv[3+args]))
					return;

				args++;
				break;
		}

		p++;
	}
}
