/*
 *  ircd-ratbox: A slightly useful ircd.
 *  class.c: Controls connection classes.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "config.h"

#include "tools.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "send.h"
#include "irc_string.h"
#include "memory.h"
#include "patricia.h"

#define BAD_CONF_CLASS          -1
#define BAD_PING                -2
#define BAD_CLIENT_CLASS        -3

dlink_list class_list;
struct Class *default_class;

struct Class *
make_class(void)
{
	struct Class *tmp;

	tmp = (struct Class *) MyMalloc(sizeof(struct Class));

	ConFreq(tmp) = DEFAULT_CONNECTFREQUENCY;
	PingFreq(tmp) = DEFAULT_PINGFREQUENCY;
	MaxUsers(tmp) = 1;
	MaxSendq(tmp) = DEFAULT_SENDQ;
	MaxSendqEob(tmp) = DEFAULT_SENDQ;

#ifdef IPV6
	tmp->ip_limits = New_Patricia(128);
#else
	tmp->ip_limits = New_Patricia(32);
#endif
	return tmp;
}

void
free_class(struct Class *tmp)
{
	Destroy_Patricia(tmp->ip_limits, NULL);
	MyFree(tmp->class_name);
	MyFree((char *) tmp);

}

/*
 * get_conf_ping
 *
 * inputs	- pointer to struct ConfItem
 * output	- ping frequency
 * side effects - NONE
 */
static int
get_conf_ping(struct ConfItem *aconf)
{
	if((aconf) && ClassPtr(aconf))
		return (ConfPingFreq(aconf));

	return (BAD_PING);
}

/*
 * get_client_class
 *
 * inputs	- pointer to client struct
 * output	- pointer to name of class
 * side effects - NONE
 */
const char *
get_client_class(struct Client *target_p)
{
	struct ConfItem *aconf;
	const char *retc = "unknown";

	if((target_p != NULL) && !IsMe(target_p))
	{
		aconf = target_p->localClient->att_conf;

		if((aconf == NULL) || (aconf->className == NULL))
			retc = "default";
		else
			retc = aconf->className;
	}

	return (retc);
}

/*
 * get_client_ping
 *
 * inputs	- pointer to client struct
 * output	- ping frequency
 * side effects - NONE
 */
int
get_client_ping(struct Client *target_p)
{
	int ping = 0;
	struct ConfItem *aconf;

	aconf = target_p->localClient->att_conf;

	if(aconf != NULL)
	{
		if(aconf->status & (CONF_CLIENT | CONF_SERVER))
			ping = get_conf_ping(aconf);
	}
	else
	{
		ping = DEFAULT_PINGFREQUENCY;
	}

	if(ping <= 0)
		ping = DEFAULT_PINGFREQUENCY;

	return (ping);
}

/*
 * get_con_freq
 *
 * inputs	- pointer to class struct
 * output	- connection frequency
 * side effects - NONE
 */
int
get_con_freq(struct Class *clptr)
{
	if(clptr)
		return (ConFreq(clptr));
	return (DEFAULT_CONNECTFREQUENCY);
}

/* add_class()
 *
 * input	- class to add
 * output	-
 * side effects - class is added to class_list if new, else old class
 *                is updated with new values.
 */
void
add_class(struct Class *classptr)
{
	struct Class *tmpptr;

	tmpptr = find_class(classptr->class_name);

	if(tmpptr == default_class)
	{
		dlinkAddAlloc(classptr, &class_list);
		CurrUsers(classptr) = 0;
	}
	else
	{
		MaxUsers(tmpptr) = MaxUsers(classptr);
		MaxLocal(tmpptr) = MaxLocal(classptr);
		MaxGlobal(tmpptr) = MaxGlobal(classptr);
		MaxIdent(tmpptr) = MaxIdent(classptr);
		PingFreq(tmpptr) = PingFreq(classptr);
		MaxSendq(tmpptr) = MaxSendq(classptr);
		MaxSendqEob(tmpptr) = MaxSendqEob(classptr);
		ConFreq(tmpptr) = ConFreq(classptr);
		CidrBitlen(tmpptr) = CidrBitlen(classptr);
		CidrAmount(tmpptr) = CidrAmount(classptr);

		free_class(classptr);
	}
}


/*
 * find_class
 *
 * inputs	- string name of class
 * output	- corresponding class pointer
 * side effects	- NONE
 */
struct Class *
find_class(const char *classname)
{
	struct Class *cltmp;
	dlink_node *ptr;

	if(classname == NULL)
		return default_class;

	DLINK_FOREACH(ptr, class_list.head)
	{
		cltmp = ptr->data;

		if(!strcmp(ClassName(cltmp), classname))
			return cltmp;
	}

	return default_class;
}

/*
 * check_class
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	- 
 */
void
check_class()
{
	struct Class *cltmp;
	dlink_node *ptr;
	dlink_node *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, class_list.head)
	{
		cltmp = ptr->data;

		if(MaxUsers(cltmp) < 0)
		{
			dlinkDestroy(ptr, &class_list);
			if(CurrUsers(cltmp) <= 0)
				free_class(cltmp);
		}
	}
}

/*
 * initclass
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	- 
 */
void
initclass()
{
	default_class = make_class();
	DupString(ClassName(default_class), "default");
}

/*
 * report_classes
 *
 * inputs	- pointer to client to report to
 * output	- NONE
 * side effects	- class report is done to this client
 */
void
report_classes(struct Client *source_p)
{
	struct Class *cltmp;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, class_list.head)
	{
		cltmp = ptr->data;

		sendto_one_numeric(source_p, RPL_STATSYLINE, 
				   form_str(RPL_STATSYLINE),
				   'Y', ClassName(cltmp),
				   PingFreq(cltmp), ConFreq(cltmp),
				   MaxUsers(cltmp), MaxSendq(cltmp),
				   MaxLocal(cltmp), MaxIdent(cltmp),
				   MaxGlobal(cltmp), MaxIdent(cltmp));
	}

	/* also output the default class */
	sendto_one_numeric(source_p, RPL_STATSYLINE, form_str(RPL_STATSYLINE),
			   'Y', ClassName(default_class),
			   PingFreq(default_class), ConFreq(default_class),
			   MaxUsers(default_class), MaxSendq(default_class),
			   MaxLocal(default_class), MaxIdent(default_class),
			   MaxGlobal(default_class), MaxIdent(default_class));
}

/*
 * get_sendq
 *
 * inputs	- pointer to client
 * output	- sendq for this client as found from its class
 * side effects	- NONE
 */
long
get_sendq(struct Client *client_p)
{
	struct ConfItem *aconf;

	if((client_p != NULL) && !IsMe(client_p))
	{
		aconf = client_p->localClient->att_conf;

		if(aconf != NULL)
		{
			if(aconf->status & (CONF_CLIENT | CONF_SERVER))
			{
				/* never true for clients */
				if(HasSentEob(client_p))
					return ConfMaxSendqEob(aconf);

				return ConfMaxSendq(aconf);
			}
		}
	}

	return DEFAULT_SENDQ;
}
