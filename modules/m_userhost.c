/************************************************************************
 *   IRC - Internet Relay Chat, src/m_userhost.c
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
#include "s_serv.h"
#include "send.h"
#include "irc_string.h"
#include "msg.h"
#include <string.h>

static char buf[BUFSIZE];

struct Message userhost_msgtab = {
  MSG_USERHOST, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_userhost, m_ignore, m_userhost}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_USERHOST, &userhost_msgtab);
}

/*
 * m_userhost added by Darren Reed 13/8/91 to aid clients and reduce
 * the need for complicated requests like WHOIS. It returns user/host
 * information only (no spurious AWAY labels or channels).
 */
/* rewritten Diane Bruce 1999 */

int     m_userhost(struct Client *cptr,
                   struct Client *sptr,
                   int parc,
                   char *parv[])
{
  char  *p;            /* scratch end pointer */
  char  *cn;           /* current name */
  struct Client *acptr;
  char response[5][NICKLEN*2+USERLEN+HOSTLEN+30];
  int i;               /* loop counter */

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "USERHOST");
      return 0;
    }

  /* The idea is to build up the response string out of pieces
   * none of this strlen() nonsense.
   * 5 * (NICKLEN*2+USERLEN+HOSTLEN+30) is still << sizeof(buf)
   * and our ircsprintf() truncates it to fit anyway. There is
   * no danger of an overflow here. -Dianora
   */

  response[0][0] = response[1][0] = response[2][0] = 
    response[3][0] = response[4][0] = '\0';

  for(cn = strtoken(&p, parv[1], ","), i=0; (i < 5) && cn; 
      cn = strtoken(&p, (char *)NULL, ","), i++ )
    {
      if ((acptr = find_person(cn, NULL)))
	{
	  if (acptr == sptr) /* show real IP for USERHOST on yourself */
            ircsprintf(response[i], "%s%s=%c%s@%s",
		       acptr->name,
		       IsAnyOper(acptr) ? "*" : "",
		       (acptr->user->away) ? '-' : '+',
		       acptr->username,
		       acptr->sockhost);
          else
            ircsprintf(response[i], "%s%s=%c%s@%s",
		       acptr->name,
		       IsAnyOper(acptr) ? "*" : "",
		       (acptr->user->away) ? '-' : '+',
		       acptr->username,
		       acptr->host);

	}
    }

  ircsprintf(buf, "%s %s %s %s %s",
    response[0], response[1], response[2], response[3], response[4] );
  sendto_one(sptr, form_str(RPL_USERHOST), me.name, parv[0], buf);

  return 0;
}
