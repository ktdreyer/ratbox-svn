/************************************************************************
 *   IRC - Internet Relay Chat, src/s_stats.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  $Id$
 */

#include <sys/types.h>

#include <string.h>

#include "s_stats.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "send.h"
#include "memory.h"

/*
 * stats stuff
 */
static struct ServerStatistics  ircst;
struct ServerStatistics* ServerStats = &ircst;

void init_stats()
{
  memset(&ircst, 0, sizeof(ircst));
}

/*
 * tstats
 *
 * inputs	- client to report to
 * output	- NONE 
 * side effects	-
 */
void tstats(struct Client *source_p)
{
  struct Client*           target_p;
  struct ServerStatistics* sp;
  struct ServerStatistics  tmp;
  dlink_node *ptr;

  sp = &tmp;
  memcpy(sp, ServerStats, sizeof(struct ServerStatistics));

  for(ptr = serv_list.head; ptr; ptr = ptr->next)
    {
      target_p = ptr->data;

      sp->is_sbs += target_p->localClient->sendB;
      sp->is_sbr += target_p->localClient->receiveB;
      sp->is_sks += target_p->localClient->sendK;
      sp->is_skr += target_p->localClient->receiveK;
      sp->is_sti += CurrentTime - target_p->firsttime;
      sp->is_sv++;
      if (sp->is_sbs > 1023)
	{
	  sp->is_sks += (sp->is_sbs >> 10);
	  sp->is_sbs &= 0x3ff;
	}
      if (sp->is_sbr > 1023)
	{
	  sp->is_skr += (sp->is_sbr >> 10);
	  sp->is_sbr &= 0x3ff;
	}
    }

  for(ptr = lclient_list.head; ptr; ptr = ptr->next)
    {
      target_p = ptr->data;

      sp->is_cbs += target_p->localClient->sendB;
      sp->is_cbr += target_p->localClient->receiveB;
      sp->is_cks += target_p->localClient->sendK;
      sp->is_ckr += target_p->localClient->receiveK;
      sp->is_cti += CurrentTime - target_p->firsttime;
      sp->is_cl++;
      if (sp->is_cbs > 1023)
	{
	  sp->is_cks += (sp->is_cbs >> 10);
	  sp->is_cbs &= 0x3ff;
	}
      if (sp->is_cbr > 1023)
	{
	  sp->is_ckr += (sp->is_cbr >> 10);
	  sp->is_cbr &= 0x3ff;
	}
      
    }

  for(ptr = unknown_list.head; ptr; ptr = ptr->next)
    {
      sp->is_ni++;
    }

  sendto_one(source_p, ":%s %d %s :accepts %u refused %u",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_ac, sp->is_ref);
  sendto_one(source_p, ":%s %d %s :unknown commands %u prefixes %u",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_unco, sp->is_unpf);
  sendto_one(source_p, ":%s %d %s :nick collisions %u unknown closes %u",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_kill, sp->is_ni);
  sendto_one(source_p, ":%s %d %s :wrong direction %u empty %u",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_wrdi, sp->is_empt);
  sendto_one(source_p, ":%s %d %s :numerics seen %u mode fakes %u",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_num, sp->is_fake);
  sendto_one(source_p, ":%s %d %s :auth successes %u fails %u",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_asuc, sp->is_abad);
  sendto_one(source_p, ":%s %d %s :local connections %u udp packets %u",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_loc, sp->is_udp);
  sendto_one(source_p, ":%s %d %s :Client Server",
             me.name, RPL_STATSDEBUG, source_p->name);
  sendto_one(source_p, ":%s %d %s :connected %u %u",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_cl, sp->is_sv);
  sendto_one(source_p, ":%s %d %s :bytes sent %lu.%uK %lu.%uK",
             me.name, RPL_STATSDEBUG, source_p->name,
             sp->is_cks, sp->is_cbs, sp->is_sks, sp->is_sbs);
  sendto_one(source_p, ":%s %d %s :bytes recv %lu.%uK %lu.%uK",
             me.name, RPL_STATSDEBUG, source_p->name,
             sp->is_ckr, sp->is_cbr, sp->is_skr, sp->is_sbr);
  sendto_one(source_p, ":%s %d %s :time connected %lu %lu",
             me.name, RPL_STATSDEBUG, source_p->name, sp->is_cti, sp->is_sti);
}


