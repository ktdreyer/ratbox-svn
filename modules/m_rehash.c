/************************************************************************
 *   IRC - Internet Relay Chat, src/m_rehash.c
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
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "m_gline.h"
#include "numeric.h"
#include "res.h"
#include "s_conf.h"
#include "s_log.h"
#include "send.h"
#include "msg.h"

struct Message rehash_msgtab = {
  MSG_REHASH, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_ignore, mo_rehash}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_REHASH, &rehash_msgtab);
}

char *_version = "20001122";

/*
 * mo_rehash - REHASH message handler
 *
 */
int mo_rehash(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int found = NO;

  if ( !IsOperRehash(sptr) )
    {
      sendto_one(sptr,":%s NOTICE %s :You have no H flag", me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      if (irccmp(parv[1],"DNS") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "DNS");
#ifdef CUSTOM_ERR
          sendto_realops("%s is rehashing DNS while whistling innocently",
#else
          sendto_realops("%s is rehashing DNS",
#endif
                 parv[0]);
          restart_resolver();   /* re-read /etc/resolv.conf AGAIN?
                                   and close/re-open res socket */
          found = YES;
        }
      else if(irccmp(parv[1],"TKLINES") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "temp klines");
          flush_temp_klines();
#ifdef CUSTOM_ERR
          sendto_realops("%s is clearing temp klines while whistling innocently",
#else
          sendto_realops("%s is clearing temp klines",
#endif
                 parv[0]);
          found = YES;
        }
      else if(irccmp(parv[1],"GLINES") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "g-lines");
          flush_glines();
#ifdef CUSTOM_ERR
          sendto_realops("%s is clearing G-lines while whistling innocently",
#else
          sendto_realops("%s is clearing G-lines",
#endif
                 parv[0]);
          found = YES;
        }
      else if(irccmp(parv[1],"GC") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "garbage collecting");
          block_garbage_collect();
#ifdef CUSTOM_ERR
          sendto_realops("%s is garbage collecting while whistling innocently",
#else
          sendto_realops("%s is garbage collecting",
#endif
                 parv[0]);
          found = YES;
        }
      else if(irccmp(parv[1],"MOTD") == 0)
        {
          sendto_realops("%s is forcing re-reading of MOTD file",parv[0]);
          ReadMessageFile( &ConfigFileEntry.motd );
          found = YES;
        }
      else if(irccmp(parv[1],"OMOTD") == 0)
        {
          sendto_realops("%s is forcing re-reading of OPER MOTD file",parv[0]);
          ReadMessageFile( &ConfigFileEntry.opermotd );
          found = YES;
        }
      else if(irccmp(parv[1],"HELP") == 0)
        {
          sendto_realops("%s is forcing re-reading of oper help file",parv[0]);
          ReadMessageFile( &ConfigFileEntry.helpfile );
          found = YES;
        }

      if(found)
        {
          log(L_NOTICE, "REHASH %s From %s\n", parv[1], 
              get_client_name(sptr, HIDE_IP));
          return 0;
        }
      else
        {
	if (ConfigFileEntry.glines)
          sendto_one(sptr,":%s NOTICE %s : rehash one of :DNS TKLINES GLINES GC MOTD OMOTD" ,me.name,sptr->name);
	else
	  sendto_one(sptr,":%s NOTICE %s : rehash one of :DNS TKLINES GC MOTD OMOTD" ,me.name,sptr->name);
          return(0);
        }
    }
  else
    {
      sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0],
                 ConfigFileEntry.configfile);
#ifdef CUSTOM_ERR
      sendto_realops("%s is rehashing server config file while whistling innocently",
#else
      sendto_realops("%s is rehashing server config file",
#endif
                 parv[0]);
      log(L_NOTICE, "REHASH From %s\n", get_client_name(sptr, SHOW_IP));
      return rehash(cptr, sptr, 0);
    }
  return 0; /* shouldn't ever get here */
}

