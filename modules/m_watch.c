/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_watch.c: implements a watch command
 * 
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1991 Darren Reed
 *  Copyright (C) 2000-2003 TR-IRCD Development
 *  Copyright (C) 2004 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 *  $Id$
 */
                        
#include "stdinc.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "watch.h"
#include "client.h"
#include "irc_string.h"
#include "numeric.h"
#include "s_serv.h"
#include "s_user.h"
#include "send.h"
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"


static char buf[BUFSIZE];

static int m_watch(struct Client *, struct Client *, int, const char **);

struct Message watch_msgtab = {
	"WATCH", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, {m_watch, 1}, mg_ignore, mg_ignore, mg_ignore, {m_watch, 1}}
};

mapi_clist_av1 watch_clist[] = { &watch_msgtab, NULL };

DECLARE_MODULE_AV1(watch, NULL, NULL, watch_clist, NULL, NULL, "$Revision$");

/*
 * RPL_NOWON   - Online at the moment (Succesfully added to WATCH-list)
 * RPL_NOWOFF  - Offline at the moement (Succesfully added to WATCH-list)
 * RPL_WATCHOFF   - Succesfully removed from WATCH-list.
 * ERR_TOOMANYWATCH - Take a guess :>  Too many WATCH entries.
 */
static void
show_watch(struct Client * client_p, char *name, int rpl1, int rpl2)
{
	struct Client *aclient_p;

	if((aclient_p = find_person(name)))
		sendto_one(client_p, form_str(rpl1), me.name, client_p->name, aclient_p->name, aclient_p->username,
				   aclient_p->host, MyClient(aclient_p) ? aclient_p->localClient->lasttime : 0);
	else
		sendto_one(client_p, form_str(rpl2), me.name, client_p->name, name, "*", "*", 0);
}

/*
 * m_watch
 */
int
m_watch(struct Client * client_p, struct Client * source_p, int parc, const char **parv)
{
	struct Client *aclient_p;
	static time_t last_used = 0;
	char *s, *p, *user;
	char def[2] = "l";
	char *tmp;
	int x;



	if(parc < 2)
		s = def;
	else
		s = LOCAL_COPY(parv[1]);


	if((last_used + ConfigFileEntry.pace_wait_simple) > CurrentTime)
	{

                sendto_one(source_p, form_str(RPL_LOAD2HI),
                           me.name, source_p->name, "WATCH");
		sendto_one(source_p, form_str(RPL_ENDOFWATCHLIST),
                	   me.name, source_p->name, "");
		return 0;			
	} else
		last_used = CurrentTime;
	

	switch(*s)
	{
		case 'L':
		case 'l':
		{
			/*
			 * Well that was fun, NOT.  Now they want a list of everybody in
			 * their WATCH list AND if they are online or offline? Sheesh,
			 * greedy arn't we?
			 */
			struct Watch *awptr;
			dlink_node *lp;
			DLINK_FOREACH(lp, source_p->localClient->watchlist.head)
			{
				awptr = lp->data;
				if((aclient_p = find_person(awptr->watchnick)))
					sendto_one(source_p, form_str(RPL_NOWON), me.name, client_p->name, 
						   aclient_p->name,
						   aclient_p->username, aclient_p->host,
						   aclient_p->tsinfo);
				/*
				 * But actually, only show them offline if its a capital
				 * 'L' (full list wanted).
				 */
				else if(IsUpper(*s))
					sendto_one(source_p, form_str(RPL_NOWOFF), me.name, client_p->name,
							   awptr->watchnick, "*",
							   "*", awptr->lasttime);
			}
			sendto_one(source_p, form_str(RPL_ENDOFWATCHLIST), me.name, client_p->name, *s);
			return 0;
		} 
		case 'C':
		case 'c':
			hash_del_watch_list(source_p);
			return 0;

		case 'S':
		case 's':		
		{
			/*
			 * Now comes the fun stuff, "S" or "s" returns a status report of
			 * their WATCH list.  I imagine this could be CPU intensive if its
			 * done alot, perhaps an auto-lag on this?
			 */
			dlink_node *lp;
			struct Watch *awptr;
			int count = 0;
		
			/*
			 * Send a list of how many users they have on their WATCH list
			 * and how many WATCH lists they are on.
			 */
			awptr = hash_get_watch(source_p->name);
			if(awptr) 
				count = dlink_list_length(&awptr->watched_by);
		
			sendto_one(source_p, form_str(RPL_WATCHSTAT), me.name, client_p->name,
					   dlink_list_length(&source_p->localClient->watchlist), 
					   count);
			/*
			 * Send a list of everybody in their WATCH list. Be careful
			 * not to buffer overflow.
			 */	
			if(dlink_list_length(&source_p->localClient->watchlist) == 0)
			{
				sendto_one(source_p, form_str(RPL_ENDOFWATCHLIST), me.name, client_p->name, *s);
				return 0;
			}

			*buf = '\0';
			count = strlen(parv[0]) + strlen(me.name) + 10;
		
			DLINK_FOREACH(lp, source_p->localClient->watchlist.head)
			{
				awptr = lp->data;
				if(count + strlen(awptr->watchnick) + 1 > BUFSIZE - 2)
				{
					sendto_one(source_p, form_str(RPL_WATCHLIST), me.name, client_p->name, buf);
					*buf = '\0';
					count = strlen(parv[0]) + strlen(me.name) + 10;
				}
				strcat(buf, awptr->watchnick);
				strcat(buf, " ");
				count += (strlen(awptr->watchnick) + 1);
			}
			sendto_one(source_p, form_str(RPL_WATCHLIST), me.name, client_p->name, buf);
			sendto_one(source_p, form_str(RPL_ENDOFWATCHLIST), me.name, client_p->name, *s);
			return 0;
		}
		case '+':
		case '-':
		{
			for(x = 1; x < parc; x++)
			{	 
				tmp = LOCAL_COPY(parv[x]);
				for(p = NULL, s = strtoken(&p, tmp, ", "); s; s = strtoken(&p, NULL, ", "))
				{
					if((user = (char *) strchr(s, '!')))
						*user++ = '\0';	/* Not used */
					
					if(*s == '+')
					{
						if(*(s + 1))
						{
							if(dlink_list_length(&source_p->localClient->watchlist) >= ConfigFileEntry.max_watch)
							{
								sendto_one(source_p, form_str(ERR_TOOMANYWATCH), me.name, client_p->name, s + 1, ConfigFileEntry.max_watch);
								continue;
							}
							add_to_watch_hash_table(s + 1, source_p);
						}
						show_watch(source_p, s + 1, RPL_NOWON, RPL_NOWOFF);
						continue;
					} 
					else if(*s == '-')
					{
						del_from_watch_hash_table(s + 1, source_p);
						show_watch(source_p, s + 1, RPL_WATCHOFF, RPL_WATCHOFF);
						continue;
					} else
						continue;
				}
			}			
		}
	}
	return 0;
}
