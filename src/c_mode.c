/* src/c_mode.c
 *  Contains code for handling "MODE" command.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */

#include "stdinc.h"
#include "c_init.h"
#include "client.h"
#include "channel.h"
#include "command.h"
#include "log.h"

static void c_mode(struct client *, char *parv[], int parc);
struct scommand_handler mode_command = { "MODE", c_mode };

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

		slog("OLD UMODE: %s", umode_to_string(target_p->user->umode));
		target_p->user->umode = string_to_umode(parv[2], target_p->user->umode);
		slog("NEW UMODE: %s", umode_to_string(target_p->user->umode));
		return;
	}

	if(parc < 3 || EmptyString(parv[2]))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
		return;

	{
		char buf[512];
		struct chmember *mptr;
		dlink_node *ptr;

		slog("MODE: Changing mode for channel %s", chptr->name);

		buf[0] = '\0';

		strcat(buf, chmode_to_string(chptr));

		DLINK_FOREACH(ptr, chptr->users.head)
		{
			mptr = ptr->data;
			strcat(buf, " ");
			if(mptr->flags & MODE_OPPED)
				strcat(buf, "@");
			if(mptr->flags & MODE_VOICED)
				strcat(buf, "+");
			strcat(buf, mptr->client_p->name);
		}

		slog("WAS: %s", buf);
	}

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
	{
		char buf[512];
		struct chmember *mptr;
		dlink_node *ptr;

		buf[0] = '\0';
		strcat(buf, chmode_to_string(chptr));

		DLINK_FOREACH(ptr, chptr->users.head)
		{
			mptr = ptr->data;
			strcat(buf, " ");
			if(mptr->flags & MODE_OPPED)
				strcat(buf, "@");
			if(mptr->flags & MODE_VOICED)
				strcat(buf, "+");
			strcat(buf, mptr->client_p->name);
		}

		slog("IS: %s", buf);
	}


}
