/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_rehash.c
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
#include "channel.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "s_gline.h"
#include "numeric.h"
#include "res.h"
#include "s_conf.h"
#include "s_log.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

static void mo_rehash(struct Client*, struct Client*, int, char**);

struct Message rehash_msgtab = {
  "REHASH", 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_rehash}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&rehash_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&rehash_msgtab);
}

char *_version = "20001122";
#endif
/*
 * mo_rehash - REHASH message handler
 *
 */
static void mo_rehash(struct Client *client_p, struct Client *source_p,
                     int parc, char *parv[])
{
  int found = NO;

  if ( !IsOperRehash(source_p) )
    {
      sendto_one(source_p,":%s NOTICE %s :You need rehash = yes;", me.name, parv[0]);
      return;
    }

  if (parc > 1)
    {
      if (irccmp(parv[1],"CHANNELS") == 0)
        {
          sendto_realops_flags(FLAGS_ALL, L_ALL,
                       "%s is forcing cleanup of channels",parv[0]);
          found = YES;
        }
      else if(irccmp(parv[1],"DNS") == 0)
        {
          sendto_one(source_p, form_str(RPL_REHASHING), me.name, parv[0], "DNS");
          sendto_realops_flags(FLAGS_ALL, L_ALL,"%s is rehashing DNS",
                               get_oper_name(source_p));
          restart_resolver();   /* re-read /etc/resolv.conf AGAIN?
                                   and close/re-open res socket */
          found = YES;
        }
      else if(irccmp(parv[1],"MOTD") == 0)
        {
          sendto_realops_flags(FLAGS_ALL, L_ALL,
		       "%s is forcing re-reading of MOTD file",
		       get_oper_name(source_p));
          ReadMessageFile( &ConfigFileEntry.motd );
          found = YES;
        }
      else if(irccmp(parv[1],"OMOTD") == 0)
        {
          sendto_realops_flags(FLAGS_ALL, L_ALL,
		       "%s is forcing re-reading of OPER MOTD file",
		       get_oper_name(source_p));
          ReadMessageFile( &ConfigFileEntry.opermotd );
          found = YES;
        }
      else if(irccmp(parv[1],"HELP") == 0)
        {
          sendto_realops_flags(FLAGS_ALL, L_ALL,
		       "%s is forcing re-reading of oper and user help files",
                       parv[0]);
          ReadMessageFile( &ConfigFileEntry.helpfile );
          ReadMessageFile( &ConfigFileEntry.uhelpfile );
          found = YES;
        }
      if(found)
        {
          ilog(L_NOTICE, "REHASH %s From %s\n", parv[1], 
	       get_client_name(source_p, HIDE_IP));
          return;
        }
      else
        {
          sendto_one(source_p,":%s NOTICE %s :rehash one of :CHANNELS DNS HELP MOTD OMOTD" ,me.name,source_p->name);
          return;
        }
    }
  else
    {
      sendto_one(source_p, form_str(RPL_REHASHING), me.name, parv[0],
                 ConfigFileEntry.configfile);
      sendto_realops_flags(FLAGS_ALL, L_ALL,
			   "%s is rehashing server config file",
			   get_oper_name(source_p));
      ilog(L_NOTICE, "REHASH From %s[%s]", get_oper_name(source_p),
           source_p->localClient->sockhost);
      rehash(client_p, source_p, 0);
      return;
    }
}

