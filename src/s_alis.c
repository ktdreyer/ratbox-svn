/* src/s_alis.c
 *   Contains the code for ALIS, the Advanced List Service
 *
 * Copyright (C) 2003-2004 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2004 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"

#ifdef ALIS_SERVICE
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

#define ALIS_ERR_PARAM		1
#define ALIS_ERR_MODE		2
#define ALIS_ERR_UNKNOWN	3
#define ALIS_ERR_MIN		4
#define ALIS_ERR_MAX		5
#define ALIS_ERR_SKIP		6

static struct client *alis_p;

static int s_alis_list(struct client *, char *parv[], int parc);

static struct service_command alis_command[] =
{
	{ "LIST",	&s_alis_list,	1, NULL, 0, 1, 0L },
        { "\0",		NULL,		0, NULL, 0, 0, 0 }
};

static struct service_error alis_error[] =
{
        { ALIS_ERR_PARAM,       "Missing parameters."   },
        { ALIS_ERR_MODE,        "Invalid -mode option." },
        { ALIS_ERR_UNKNOWN,     "Unknown option."       },
        { ALIS_ERR_MIN,         "Invalid -min option."  },
        { ALIS_ERR_MAX,         "Invalid -max option."  },
        { ALIS_ERR_SKIP,        "Invalid -skip option." },
        { 0,                    NULL                    }
};

static struct service_handler alis_service = {
	"ALIS", "ALIS", "alis", "services.alis", "Advanced List Service", 0,
        60, 80, alis_command, alis_error, NULL, NULL
};

void
init_s_alis(void)
{
	alis_p = add_service(&alis_service);
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

struct alis_query
{
	const char *mask;
	const char *topic;
	int min;
	int max;
	int show_mode;
	int show_topicwho;
	int mode;
	int mode_dir;
	int mode_key;
	int mode_limit;
	int skip;
};

static int
parse_alis(struct client *client_p, struct alis_query *query,
	   char *parv[], int parc)
{
	int i = 1;
	int param = 2;

	while(i < parc)
	{
		if(param >= parc || EmptyString(parv[param]))
		{
			service_error(alis_p, client_p, ALIS_ERR_PARAM);
			return 0;
		}

		if(!strcasecmp(parv[i], "-min"))
		{
			if((query->min = atoi(parv[param])) < 1)
			{
				service_error(alis_p, client_p, ALIS_ERR_MIN);
				return 0;
			}
		}
		else if(!strcasecmp(parv[i], "-max"))
		{
			if((query->max = atoi(parv[param])) < 1)
			{
				service_error(alis_p, client_p, ALIS_ERR_MAX);
				return 0;
			}
		}
		else if(!strcasecmp(parv[i], "-skip"))
		{
			if((query->skip = atoi(parv[param])) < 1)
			{
				service_error(alis_p, client_p, ALIS_ERR_SKIP);
				return 0;
			}
		}
		else if(!strcasecmp(parv[i], "-topic"))
		{
			query->topic = parv[param];
		}
		else if(!strcasecmp(parv[i], "-show"))
		{
			if(parv[param][0] == 'm')
			{
				query->show_mode = 1;

				if(parv[param][1] == 't')
					query->show_topicwho = 1;
			}
			else if(parv[param][0] == 't')
			{
				query->show_topicwho = 1;

				if(parv[param][1] == 'm')
					query->show_mode = 1;
			}
		}
		else if(!strcasecmp(parv[i], "-mode"))
		{
			const char *modestring;

			modestring = parv[param];

			switch(*modestring)
			{
				case '+':
					query->mode_dir = DIR_SET;
					break;
				case '-':
					query->mode_dir = DIR_UNSET;
					break;
				case '=':
					query->mode_dir = DIR_EQUAL;
					break;
				default:
					service_error(alis_p, client_p, ALIS_ERR_MODE);
					return 0;
			}

			query->mode = parse_mode(modestring+1, 
					&query->mode_key, 
					&query->mode_limit);

			if(query->mode == -1)
			{
				service_error(alis_p, client_p, ALIS_ERR_MODE);
				return 0;
			}
		}
		else
		{
			service_error(alis_p, client_p, ALIS_ERR_UNKNOWN);
			return 0;
		}

		i += 2;
		param += 2;
	}

	return 1;
}

static void
print_channel(struct client *client_p, struct channel *chptr,
	     struct alis_query *query)
{
	if(query->show_mode && query->show_topicwho)
		sendto_server(":%s NOTICE %s :%-50s %-8s %3d :%s (%s)",
				MYNAME, client_p->name, chptr->name,
				chmode_to_string_simple(chptr),
				dlink_list_length(&chptr->users),
				chptr->topic, chptr->topicwho);
	else if(query->show_mode)
		sendto_server(":%s NOTICE %s :%-50s %-8s %3d :%s",
				MYNAME, client_p->name, chptr->name,
				chmode_to_string_simple(chptr),
				dlink_list_length(&chptr->users),
				chptr->topic);
	else if(query->show_topicwho)
		sendto_server(":%s NOTICE %s :%-50s %3d :%s (%s)",
				MYNAME, client_p->name, chptr->name,
				dlink_list_length(&chptr->users),
				chptr->topic, chptr->topicwho);
	else
		sendto_server(":%s NOTICE %s :%-50s %3d :%s",
				MYNAME, client_p->name, chptr->name,
				dlink_list_length(&chptr->users),
				chptr->topic);
}

static int
show_channel(struct channel *chptr, struct alis_query *query)
{
        /* skip +p/+s channels */
        if(chptr->mode.mode & MODE_SECRET || chptr->mode.mode & MODE_PRIVATE)
                return 0;

        if(dlink_list_length(&chptr->users) < query->min ||
           (query->max && dlink_list_length(&chptr->users) > query->max))
                return 0;

        if(query->mode)
        {
                if(query->mode_dir == DIR_SET)
                {
                        if(((chptr->mode.mode & query->mode) == 0) ||
                           (query->mode_key && chptr->mode.key[0] == '\0') ||
                           (query->mode_limit && !chptr->mode.limit))
                                return 0;
                }
                else if(query->mode_dir == DIR_UNSET)
                {
                        if((chptr->mode.mode & query->mode) ||
                           (query->mode_key && chptr->mode.key[0] != '\0') ||
                           (query->mode_limit && chptr->mode.limit))
                                return 0;
                }
                else if(query->mode_dir == DIR_EQUAL)
                {
                        if((chptr->mode.mode != query->mode) ||
                           (query->mode_key && chptr->mode.key[0] == '\0') ||
                           (query->mode_limit && !chptr->mode.limit))
                                return 0;
                }
        }

        /* cant show a topicwho, when a channel has no topic. */
        if(chptr->topic[0] == '\0')
                query->show_topicwho = 0;

        if(!match(query->mask, chptr->name))
                return 0;

        if(query->topic != NULL && !match(query->topic, chptr->topic))
                return 0;

        if(query->skip)
        {
                query->skip--;
                return 0;
        }

        return 1;
}

/* s_alis()
 *   Handles the listing of channels for ALIS.
 *
 * inputs	- client requesting list, params
 * outputs	-
 */
static int
s_alis_list(struct client *client_p, char *parv[], int parc)
{
	struct channel *chptr;
	struct alis_query query;
	dlink_node *ptr;
	int maxmatch = ALIS_MAX_MATCH;

	memset(&query, 0, sizeof(struct alis_query));

        query.mask = parv[0];

        if(parc > 1)
        {
                if(!parse_alis(client_p, &query, parv, parc))
                        return 1;
        }

        sendto_server(":%s NOTICE %s :Returning maximum of %d channel names "
                        "matching '%s'",
                        MYNAME, client_p->name, ALIS_MAX_MATCH, query.mask);

        /* hunting for one channel.. */
        if(strchr(query.mask, '*') == NULL)
        {
                if((chptr = find_channel(query.mask)) != NULL)
                {
                        if(chptr->topic[0] == '\0')
                                query.show_topicwho = 0;

                        if(!(chptr->mode.mode & MODE_SECRET) &&
                                        !(chptr->mode.mode & MODE_PRIVATE))
                                print_channel(client_p, chptr, &query);
                }

                sendto_server(":%s NOTICE %s :End of output.",
                                MYNAME, client_p->name);
                return 1;
        }

        DLINK_FOREACH(ptr, channel_list.head)
        {
                chptr = ptr->data;

                /* matches, so show it */
                if(show_channel(chptr, &query))
                        print_channel(client_p, chptr, &query);

                if(--maxmatch == 0)
                {
                        sendto_server(":%s NOTICE %s :Maximum channel output reached.",
                                        MYNAME, client_p->name);
                        break;
                }
        }

        sendto_server(":%s NOTICE %s :End of output.", MYNAME, client_p->name);
        return 3;
}

#endif
