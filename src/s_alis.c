/* src/s_alis.c
 *  Contains the code for ALIS, the Advanced List Service
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "io.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"

#define ALIS_MAX_MATCH	60
#define ALIS_MAX_PARC	10

#define DIR_UNSET	0
#define DIR_SET		1
#define DIR_EQUAL	2

#define ERROR_PARAM		0
#define ERROR_MODE		1
#define ERROR_UNKNOWNOPTION	2

static void s_alis(struct client *, char *text);

struct service_handler alis_service = {
	"ALIS", "alis", "services.alis", "Advanced List Service", &s_alis
};

static const char *help_list[] =
{
	":%s NOTICE %s :LIST <mask> [options]",
	":%s NOTICE %s :  <mask>   : mask to search for (accepts wildcard '*')",
	":%s NOTICE %s :  [options]:",
	":%s NOTICE %s :    -min <n>: minimum users in channel",
	":%s NOTICE %s :    -max <n>: maximum users in channel",
	":%s NOTICE %s :    -mode <+|-|=><iklmnt>: modes set/unset/equal on channel",
	NULL
};

static void
alis_error(struct client *client_p, int type)
{
	switch(type)
	{
		case ERROR_PARAM:
			sendto_server(":%s NOTICE %s :Missing parameters. "
					"Please see: HELP LIST",
					MYNAME, client_p->name);
			break;

		case ERROR_MODE:
			sendto_server(":%s NOTICE %s :Error parsing -mode option. "
					"Please see: HELP LIST",
					MYNAME, client_p->name);
			break;

		case ERROR_UNKNOWNOPTION:
			sendto_server(":%s NOTICE %s :Unknown option. "
					"Please see: HELP LIST",
					MYNAME, client_p->name);
			break;

		default:
			break;
	}
}

/* parse_mode()
 *   parses a given string into modes
 *
 * inputs	- text to parse, pointer to key, pointer to limit
 * outputs	- mode, or -1 on error.
 */
static int
parse_mode(const char *text, int *key, int *limit)
{
	int mode = 0;

	if(EmptyString(text))
		return -1;

	while(*text)
	{
		switch(*text)
		{
			case 'i':
				mode |= MODE_INVITEONLY;
				break;
			case 'm':
				mode |= MODE_MODERATED;
				break;
			case 'n':
				mode |= MODE_NOEXTERNAL;
				break;
			case 't':
				mode |= MODE_TOPIC;
				break;
			case 'l':
				*limit = 1;
				break;
			case 'k':
				*key = 1;
				break;
			default:
				return -1;
		}

		text++;
	}

	return mode;
}

static int
parse_alis(char *text, char *parv[])
{
	char *p;
	int x = 0;

	parv[0] = NULL;

	while(*text == ' ')
		text++;
	if(*text == '\0')
		return 0;

	do
	{
		parv[x++] = text;
		parv[x] = NULL;

		if((p = strchr(text, ' ')) != NULL)
		{
			*p++ = '\0';
			text = p;
		}
		else
			return x;

		while(*text == ' ')
			text++;
		if(*text == '\0')
			return x;
	}
	while(x < ALIS_MAX_PARC);

	return x;
}

/* s_alis()
 *   Handles the listing of channels for ALIS.
 *
 * inputs	- client requesting list, params
 * outputs	-
 */
static void
s_alis(struct client *client_p, char *text)
{
	struct channel *chptr;
	dlink_node *ptr;
	const char *mask;
	char *aparv[ALIS_MAX_PARC];
	char *p;
	int maxmatch = ALIS_MAX_MATCH;
	int aparc = 0;
	int x = 0;
	int s_min = 0;
	int s_max = 0;
	int s_mode = 0;
	int s_mode_dir;
	int s_mode_key = 0;
	int s_mode_limit = 0;

	if(!IsUser(client_p))
		return;

	if((p = strchr(text, ' ')) != NULL)
		*p++ = '\0';

	if(!strcasecmp(text, "HELP"))
	{
		/* help index */
		if(EmptyString(p))
		{
			sendto_server(":%s NOTICE %s :ALIS Help Index. "
				      "Use HELP <topic> for more information", 
				      MYNAME, client_p->name);
			sendto_server(":%s NOTICE %s :Topics: LIST",
				      MYNAME, client_p->name);
		}
		else if(!strcasecmp(p, "LIST"))
		{
			int x = 0;

			while(help_list[x] != NULL)
			{
				sendto_server(help_list[x], MYNAME, client_p->name);
				x++;
			}

			return;
		}
		else
			sendto_server(":%s NOTICE %s :Unknown topic '%s'",
				      MYNAME, client_p->name);

		return;
	}
	else if(!strcasecmp(text, "LIST"))
	{
		mask = p;

		if(EmptyString(mask))
		{
			alis_error(client_p, ERROR_PARAM);
			return;
		}

		if((p = strchr(mask, ' ')) != NULL)
		{
			*p++ = '\0';

			if(*p != '\0')
				aparc = parse_alis(p, aparv);
		}

		if(aparc > 0)
		{
			while(x < aparc)
			{
				if(!strcasecmp(aparv[x], "-min"))
				{
					if(++x >= aparc)
					{
						alis_error(client_p, ERROR_PARAM);
						return;
					}

					s_min = atoi(aparv[x]);
				}
				else if(!strcasecmp(aparv[x], "-max"))
				{
					if(++x >= aparc)
					{
						alis_error(client_p, ERROR_PARAM);
						return;
					}

					s_max = atoi(aparv[x]);
				}
				else if(!strcasecmp(aparv[x], "-mode"))
				{
					const char *modestring;

					if(++x >= aparc)
					{
						alis_error(client_p, ERROR_PARAM);
						return;
					}

					modestring = aparv[x];

					switch(*modestring)
					{
					case '+':
						s_mode_dir = DIR_SET;
						break;
					case '-':
						s_mode_dir = DIR_UNSET;
						break;
					case '=':
						s_mode_dir = DIR_EQUAL;
						break;
					default:
						alis_error(client_p, ERROR_MODE);
						return;
					}

					s_mode = parse_mode(modestring+1, &s_mode_key, &s_mode_limit);

					if(s_mode == -1)
					{
						alis_error(client_p, ERROR_MODE);
						return;
					}
				}
				else
				{
					alis_error(client_p, ERROR_UNKNOWNOPTION);
					return;
				}

				x++;
			}
		}

		sendto_server(":%s NOTICE %s :Returning maximum of %d channel names "
				"matching '%s'",
				MYNAME, client_p->name, ALIS_MAX_MATCH, mask);

		if(strchr(mask, '*') == NULL)
			return;

		DLINK_FOREACH(ptr, channel_list.head)
		{
			chptr = ptr->data;

			/* skip +p/+s channels */
			if(chptr->mode.mode & MODE_SECRET ||
			   chptr->mode.mode & MODE_PRIVATE)
				continue;

			if(dlink_list_length(&chptr->users) < s_min ||
			   (s_max && dlink_list_length(&chptr->users) > s_max))
				continue;

			if(s_mode)
			{
				if(s_mode_dir == DIR_SET)
				{
					if(((chptr->mode.mode & s_mode) == 0) ||
					   (s_mode_key && chptr->mode.key[0] == '\0') ||
					   (s_mode_limit && !chptr->mode.limit))
						continue;
				}
				else if(s_mode_dir == DIR_UNSET)
				{
					if((chptr->mode.mode & s_mode) ||
					   (s_mode_key && chptr->mode.key[0] != '\0') ||
					   (s_mode_limit && chptr->mode.limit))
						continue;
				}
				else if(s_mode_dir == DIR_EQUAL)
				{
					if((chptr->mode.mode != s_mode) ||
					   (s_mode_key && chptr->mode.key[0] == '\0') ||
					   (s_mode_limit && !chptr->mode.limit))
						continue;
				}
			}

			if(!match(mask, chptr->name))
				continue;

			sendto_server(":%s NOTICE %s :%-50s %3d :",
					MYNAME, client_p->name, chptr->name,
					dlink_list_length(&chptr->users));

			if(--maxmatch == 0)
			{
				sendto_server(":%s NOTICE %s :Maximum channel output reached.",
						MYNAME, client_p->name);
				break;
			}
		}
		
		return;
	}
}
