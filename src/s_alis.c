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

#define ALIS_ERR_PARAM		1
#define ALIS_ERR_MODE		2
#define ALIS_ERR_UNKNOWN	3
#define ALIS_ERR_MIN		4
#define ALIS_ERR_MAX		5
#define ALIS_ERR_SKIP		6

static struct client *alis_p;

static int s_alis_list(struct client *, char *text);

static const char *alis_help_list[] =
{
	":%s NOTICE %s :LIST <mask> [options]",
	":%s NOTICE %s :  <mask>   : mask to search for (accepts wildcard '*')",
	":%s NOTICE %s :  [options]:",
	":%s NOTICE %s :    -min <n>: minimum users in channel",
	":%s NOTICE %s :    -max <n>: maximum users in channel",
	":%s NOTICE %s :    -skip <n>: skip first n matches",
	":%s NOTICE %s :    -show [m][t]: show modes/topicwho",
	":%s NOTICE %s :    -mode <+|-|=><iklmnt>: modes set/unset/equal on channel",
	":%s NOTICE %s :    -topic <string>: require topic to contain string",
	NULL
};

static struct service_command alis_command[] =
{
        { "LIST",       &s_alis_list,   alis_help_list, 1, 0, 0  },
        { "\0",         NULL,           NULL,           0, 0, 0  }
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

static void s_alis_stats(struct connection_entry *conn_p, char *parv[], int parc);

static struct service_handler alis_service = {
	"ALIS", "ALIS", "alis", "services.alis", "Advanced List Service", 0,
        40, 55, alis_command, alis_error, &s_alis_stats
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
parse_alis(struct client *client_p, struct alis_query *query, char *text)
{
	char *option;
	char *param;
	char *p;

	while(*text == ' ')
		text++;
	if(*text == '\0')
		return 0;

	while(1)
	{
		option = text;
		param = NULL;

		if((p = strchr(text, ' ')) != NULL)
		{
			*p++ = '\0';
			param = p;

			if((p = strchr(param, ' ')) != NULL)
				*p++ = '\0';
		}

		if(EmptyString(param))
		{
			service_error(alis_p, client_p, ALIS_ERR_PARAM);
			return 0;
		}

		if(!strcasecmp(option, "-min"))
		{
			if((query->min = atoi(param)) < 1)
			{
				service_error(alis_p, client_p, ALIS_ERR_MIN);
				return 0;
			}
		}
		else if(!strcasecmp(option, "-max"))
		{
			if((query->max = atoi(param)) < 1)
			{
				service_error(alis_p, client_p, ALIS_ERR_MAX);
				return 0;
			}
		}
		else if(!strcasecmp(option, "-skip"))
		{
			if((query->skip = atoi(param)) < 1)
			{
				service_error(alis_p, client_p, ALIS_ERR_SKIP);
				return 0;
			}
		}
		else if(!strcasecmp(option, "-topic"))
		{
			query->topic = param;
		}
		else if(!strcasecmp(option, "-show"))
		{
			if(param[0] == 'm')
			{
				query->show_mode = 1;

				if(param[1] == 't')
					query->show_topicwho = 1;
			}
			else if(param[0] == 't')
			{
				query->show_topicwho = 1;

				if(param[1] == 'm')
					query->show_mode = 1;
			}
		}
		else if(!strcasecmp(option, "-mode"))
		{
			const char *modestring;

			modestring = param;

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

		if(p == NULL)
			return 1;

		while(*text == ' ')
			text++;
		if(*text == '\0')
			return 1;
	}

	return 1;
}

static void
show_channel(struct client *client_p, struct channel *chptr,
	     struct alis_query *query)
{
	if(query->show_mode && query->show_topicwho)
		sendto_server(":%s NOTICE %s :%-50s %-8s %3d :%s (%s)",
				MYNAME, client_p->name, chptr->name,
				chmode_to_string(chptr),
				dlink_list_length(&chptr->users),
				chptr->topic, chptr->topicwho);
	else if(query->show_mode)
		sendto_server(":%s NOTICE %s :%-50s %-8s %3d :%s",
				MYNAME, client_p->name, chptr->name,
				chmode_to_string(chptr),
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

/* s_alis()
 *   Handles the listing of channels for ALIS.
 *
 * inputs	- client requesting list, params
 * outputs	-
 */
static int
s_alis_list(struct client *client_p, char *text)
{
	struct channel *chptr;
	struct alis_query query;
	dlink_node *ptr;
	char *p;
	int maxmatch = ALIS_MAX_MATCH;

	memset(&query, 0, sizeof(struct alis_query));

        query.mask = text;

        if(EmptyString(query.mask))
        {
                service_error(alis_p, client_p, ALIS_ERR_PARAM);
                return 1;
        }

        if((p = strchr(query.mask, ' ')) != NULL)
        {
                *p++ = '\0';

                if(!EmptyString(p))
                {
                        if(!parse_alis(client_p, &query, p))
                                return 1;
                }
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
                                show_channel(client_p, chptr, &query);
                }

                sendto_server(":%s NOTICE %s :End of output.",
                                MYNAME, client_p->name);
                return 1;
        }

        DLINK_FOREACH(ptr, channel_list.head)
        {
                chptr = ptr->data;

                /* skip +p/+s channels */
                if(chptr->mode.mode & MODE_SECRET ||
                                chptr->mode.mode & MODE_PRIVATE)
                        continue;

                if(dlink_list_length(&chptr->users) < query.min ||
                                (query.max && dlink_list_length(&chptr->users) > query.max))
                        continue;

                if(query.mode)
                {
                        if(query.mode_dir == DIR_SET)
                        {
                                if(((chptr->mode.mode & query.mode) == 0) ||
                                                (query.mode_key && chptr->mode.key[0] == '\0') ||
                                                (query.mode_limit && !chptr->mode.limit))
                                        continue;
                        }
                        else if(query.mode_dir == DIR_UNSET)
                        {
                                if((chptr->mode.mode & query.mode) ||
                                                (query.mode_key && chptr->mode.key[0] != '\0') ||
                                                (query.mode_limit && chptr->mode.limit))
                                        continue;
                        }
                        else if(query.mode_dir == DIR_EQUAL)
                        {
                                if((chptr->mode.mode != query.mode) ||
                                                (query.mode_key && chptr->mode.key[0] == '\0') ||
                                                (query.mode_limit && !chptr->mode.limit))
                                        continue;
                        }
                }

                /* cant show a topicwho, when a channel has no topic. */
                if(chptr->topic[0] == '\0')
                        query.show_topicwho = 0;

                if(!match(query.mask, chptr->name))
                        continue;

                if(query.topic != NULL && !match(query.topic, chptr->topic))
                        continue;

                if(query.skip)
                {
                        query.skip--;
                        continue;
                }

                /* matches, so show it */
                show_channel(client_p, chptr, &query);

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

void
s_alis_stats(struct connection_entry *conn_p, char *parv[], int parc)
{
#if 0
        sendto_one(conn_p, "ALIS Service:");
        sendto_one(conn_p, "  Online as %s!%s@%s [%s]",
                   alis_p->name, alis_p->service->username,
                   alis_p->service->host, alis_p->info);

        sendto_one(conn_p, "  Command usage: HELP:%d EHELP:%d LIST:%d",
                   alis_stats.help, alis_stats.ehelp, alis_stats.list);
        sendto_one(conn_p, "  Missing parameters: %d Parse errors: %d",
                   alis_stats.error_param, alis_stats.error_parse);
        sendto_one(conn_p, "  Flood protection: Paced:%d Ignored: %d",
                   alis_stats.flood, alis_stats.flood_ignore);
#endif
}
