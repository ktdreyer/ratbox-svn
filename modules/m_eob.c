/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_eob.c
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
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"

#include "msg.h"

void do_eob( struct Client *sptr );

struct Message eob_msgtab = {
  MSG_EOB, 0, 0, MFLG_SLOW | MFLG_UNREG, 0, 
  {m_unregistered, m_error, ms_eob, m_error}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_EOB, &eob_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_EOB);
}

char *_version = "20001202";

/*
 * ms_eob - EOB command handler
 *      parv[0] = sender prefix   
 *      parv[1] = servername   
 */
int ms_eob(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  if (parc > 2)
    sendto_realops_flags(FLAGS_ALL,"*** End of burst from %s (%s seconds)",
			 sptr->name, parv[2]);
  else
    sendto_realops_flags(FLAGS_ALL,"*** End of burst from %s",
			 sptr->name);
}
