/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_svinfo.c
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
#include "common.h"     /* TRUE bleah */
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "s_log.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

#include <assert.h>
#include <time.h>
#include <stdlib.h>

static void ms_svinfo(struct Client*, struct Client*, int, char**);

struct Message svinfo_msgtab = {
  "SVINFO", 0, 4, 0, MFLG_SLOW, 0,
  {m_unregistered, m_ignore, ms_svinfo, m_ignore}
};

void
_modinit(void)
{
  mod_add_cmd(&svinfo_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&svinfo_msgtab);
}

char *_version = "20001122";

/*
 * ms_svinfo - SVINFO message handler
 *      parv[0] = sender prefix
 *      parv[1] = TS_CURRENT for the server
 *      parv[2] = TS_MIN for the server
 *      parv[3] = server is standalone or connected to non-TS only
 *      parv[4] = server's idea of UTC time
 */
static void ms_svinfo(struct Client *client_p, struct Client *server_p,
                     int parc, char *parv[])
{
  time_t deltat;
  time_t theirtime;

  if (MyConnect(server_p) && IsUnknown(server_p))
  {
    exit_client(server_p, server_p, server_p, "Need SERVER before SVINFO");
    return;
  }

  if (!IsServer(server_p) || !MyConnect(server_p) || parc < 5)
    return;

  if (TS_CURRENT < atoi(parv[2]) || atoi(parv[1]) < TS_MIN)
    {
      /*
       * a server with the wrong TS version connected; since we're
       * TS_ONLY we can't fall back to the non-TS protocol so
       * we drop the link  -orabidoo
       */
      sendto_realops_flags(FLAGS_ALL,
	         "Link %s dropped, wrong TS protocol version (%s,%s)",
                 get_client_name(server_p, SHOW_IP), parv[1], parv[2]);
      exit_client(server_p, server_p, server_p, "Incompatible TS version");
      return;
    }

  /*
   * since we're here, might as well set CurrentTime while we're at it
   */
  CurrentTime = time(0);
  theirtime = atol(parv[4]);
  deltat = abs(theirtime - CurrentTime);

  if (deltat > ConfigFileEntry.ts_max_delta)
    {
      sendto_realops_flags(FLAGS_ALL,
       "Link %s dropped, excessive TS delta (my TS=%lu, their TS=%lu, delta=%lu)",
                 get_client_name(server_p, SHOW_IP),
                 CurrentTime, theirtime,deltat);

      log(L_NOTICE,
       "Link %s dropped, excessive TS delta (my TS=%lu, their TS=%lu, delta=%lu)",
                 get_client_name(server_p, SHOW_IP),
                 CurrentTime, theirtime,deltat);
      exit_client(server_p, server_p, server_p, "Excessive TS delta");
      return;
    }

  if (deltat > ConfigFileEntry.ts_warn_delta)
    { 
      sendto_realops_flags(FLAGS_ALL,
                 "Link %s notable TS delta (my TS=%lu, their TS=%lu, delta=%lu)",
			   get_client_name(server_p, MASK_IP),
			   CurrentTime, theirtime, deltat);
    }
}

