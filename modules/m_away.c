/************************************************************************
 *   IRC - Internet Relay Chat, src/m_away.c
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
#include "send.h"
#include "msg.h"

#include <stdlib.h>

struct Message away_msgtab = {
  MSG_AWAY, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_away, m_away, m_away}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_AWAY, &away_msgtab);
}

/***********************************************************************
 * m_away() - Added 14 Dec 1988 by jto. 
 *            Not currently really working, I don't like this
 *            call at all...
 *
 *            ...trying to make it work. I don't like it either,
 *            but perhaps it's worth the load it causes to net.
 *            This requires flooding of the whole net like NICK,
 *            USER, MODE, etc messages...  --msa
 ***********************************************************************/

/*
** m_away
**      parv[0] = sender prefix
**      parv[1] = away message
*/
int     m_away(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  char  *away, *awy2 = parv[1];

  /* make sure the user exists */
  if (!(sptr->user))
    {
      sendto_realops_flags(FLAGS_DEBUG,
                           "Got AWAY from nil user, from %s (%s)\n",
			   cptr->name,sptr->name);
      return 0;
    }

  away = sptr->user->away;

  if (parc < 2 || !*awy2)
    {
      /* Marking as not away */

      if (away)
        {
	  /* some lamers scripts continually do a /away, hence making a lot of
	     unnecessary traffic. *sigh* so... as comstud has done, I've
	     commented out this sendto_serv_butone() call -Dianora */
	  /* we now send this only if they were away before --is */
	  sendto_serv_butone(cptr, ":%s AWAY", parv[0]);
	  
          MyFree(away);
          sptr->user->away = NULL;
        }
      if (MyConnect(sptr))
        sendto_one(sptr, form_str(RPL_UNAWAY),
                   me.name, parv[0]);
      return 0;
    }

  /* Marking as away */
  
  if (strlen(awy2) > (size_t) TOPICLEN)
    awy2[TOPICLEN] = '\0';

  /* some lamers scripts continually do a /away, hence making a lot of
   * unnecessary traffic. *sigh* so... as comstud has done, I've
   * commented out this sendto_serv_butone() call -Dianora
   */
  /* we now send this only if they weren't away already --is */
  if (!away)
    sendto_serv_butone(cptr, ":%s AWAY :%s", parv[0], awy2); 

  /* don't use realloc() -Dianora */

  if (away)
    MyFree(away);

  away = (char *)MyMalloc(strlen(awy2)+1);
  strcpy(away,awy2);

  sptr->user->away = away;

  if (MyConnect(sptr))
    sendto_one(sptr, form_str(RPL_NOWAWAY), me.name, parv[0]);
  return 0;
}

