/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_links.c
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
 *   $Id$
 */
#include "handlers.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "s_conf.h"
#include "msg.h"
#include "motd.h"
#include "parse.h"
#include "modules.h"
#include "hook.h"

#include <assert.h>
#include <string.h>

static void m_links(struct Client*, struct Client*, int, char**);
static void mo_links(struct Client*, struct Client*, int, char**);
static void ms_links(struct Client*, struct Client*, int, char**);

struct Message links_msgtab = {
  "LINKS", 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_links, ms_links, mo_links}
};

void
_modinit(void)
{
	hook_add_event("doing_links");
  mod_add_cmd(&links_msgtab);
}

void
_moddeinit(void)
{
	hook_del_event("doing_links");
  mod_del_cmd(&links_msgtab);
}

char *_version = "20001122";

/*
 * m_links - LINKS message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername mask
 * or
 *      parv[0] = sender prefix
 *      parv[1] = server to query 
 *      parv[2] = servername mask
 */

static void m_links(struct Client *client_p, struct Client *source_p,
                   int parc, char *parv[])
{

  if (!GlobalSetOptions.hide_server)
    {
     mo_links(client_p, source_p, parc, parv);
     return;
    }
  SendMessageFile(source_p, &ConfigFileEntry.linksfile);

    
/*
 * Print our own info so at least it looks like a normal links
 * then print out the file (which may or may not be empty)
 */
  
  sendto_one(source_p, form_str(RPL_LINKS),
                           me.name, parv[0], me.name, me.name,
                           0, me.info);
      
  sendto_one(source_p, form_str(RPL_ENDOFLINKS), me.name, parv[0], "*");
}

static void mo_links(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  char*    mask = "";
  struct Client* target_p;
  char           clean_mask[2 * HOSTLEN + 4];
  char*          p;
  struct hook_links_data hd;
  
  if (parc > 2)
    {
      if (hunt_server(client_p, source_p, ":%s LINKS %s :%s", 1, parc, parv)
          != HUNTED_ISME)
        return;
      mask = parv[2];
    }
  else if (parc == 2)
    mask = parv[1];

  assert(0 != mask);

  if (*mask)       /* only necessary if there is a mask */
    mask = collapse(clean_string(clean_mask, (const unsigned char*) mask, 2 * HOSTLEN));

  hd.client_p = client_p;
  hd.source_p = source_p;
  hd.mask = mask;
  hd.parc = parc;
  hd.parv = parv;
  
  hook_call_event("doing_links", &hd);
  
  for (target_p = GlobalClientList; target_p; target_p = target_p->next) 
    {
      if (!IsServer(target_p) && !IsMe(target_p))
        continue;
      if (*mask && !match(mask, target_p->name))
        continue;
    
      if(target_p->info[0])
        {
          if( (p = strchr(target_p->info,']')) )
            p += 2; /* skip the nasty [IP] part */
          else
            p = target_p->info;
        } 
      else
        p = "(Unknown Location)";

     /* We just send the reply, as if theyre here theres either no SHIDE,
      * or theyre an oper..  
      */
      sendto_one(source_p, form_str(RPL_LINKS),
		      me.name, parv[0], target_p->name, target_p->serv->up,
                      target_p->hopcount, p);
    }
  
  sendto_one(source_p, form_str(RPL_ENDOFLINKS), me.name, parv[0],
             EmptyString(mask) ? "*" : mask);
}

/*
 * ms_links - LINKS message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername mask
 * or
 *      parv[0] = sender prefix
 *      parv[1] = server to query 
 *      parv[2] = servername mask
 */
static void ms_links(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  if (hunt_server(client_p, source_p, ":%s LINKS %s :%s", 1, parc, parv)
      != HUNTED_ISME)
    return;

  if(IsOper(source_p))
    m_links(client_p,source_p,parc,parv);
}

