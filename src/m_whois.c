/************************************************************************
 *   IRC - Internet Relay Chat, src/m_who.c
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

#include "common.h"   /* bleah */
#include "handlers.h"
#include "client.h"
#include "common.h"   /* bleah */
#include "channel.h"
#include "vchannel.h"
#include "hash.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "list.h"
#include "irc_string.h"
#include "s_conf.h"

#include <string.h>

/*
 * m_functions execute protocol messages on this server:
 *
 *      cptr    is always NON-NULL, pointing to a *LOCAL* client
 *              structure (with an open socket connected!). This
 *              identifies the physical socket where the message
 *              originated (or which caused the m_function to be
 *              executed--some m_functions may call others...).
 *
 *      sptr    is the source of the message, defined by the
 *              prefix part of the message if present. If not
 *              or prefix not found, then sptr==cptr.
 *
 *              (!IsServer(cptr)) => (cptr == sptr), because
 *              prefixes are taken *only* from servers...
 *
 *              (IsServer(cptr))
 *                      (sptr == cptr) => the message didn't
 *                      have the prefix.
 *
 *                      (sptr != cptr && IsServer(sptr) means
 *                      the prefix specified servername. (?)
 *
 *                      (sptr != cptr && !IsServer(sptr) means
 *                      that message originated from a remote
 *                      user (not local).
 *
 *              combining
 *
 *              (!IsServer(sptr)) means that, sptr can safely
 *              taken as defining the target structure of the
 *              message in this server.
 *
 *      *Always* true (if 'parse' and others are working correct):
 *
 *      1)      sptr->from == cptr  (note: cptr->from == cptr)
 *
 *      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
 *              *cannot* be a local connection, unless it's
 *              actually cptr!). [MyConnect(x) should probably
 *              be defined as (x == x->from) --msa ]
 *
 *      parc    number of variable parameter strings (if zero,
 *              parv is allowed to be NULL)
 *
 *      parv    a NULL terminated list of parameter pointers,
 *
 *                      parv[0], sender (prefix string), if not present
 *                              this points to an empty string.
 *                      parv[1]...parv[parc-1]
 *                              pointers to additional parameters
 *                      parv[parc] == NULL, *always*
 *
 *              note:   it is guaranteed that parv[0]..parv[parc-1] are all
 *                      non-NULL pointers.
 */

static int single_whois(struct Client *sptr, struct Client *acptr, int wilds);
static int global_whois(struct Client *sptr, char *nick, int wilds);

/*
** m_whois
**      parv[0] = sender prefix
**      parv[1] = nickname masklist
*/
int     m_whois(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  static time_t last_used=0L;
  struct Client *acptr;
  char  *nick;
  char  *p = NULL;
  int   found=NO;
  int   wilds;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NONICKNAMEGIVEN),
                 me.name, parv[0]);
      return 0;
    }

  if(parc > 2)
    {
      if (hunt_server(cptr,sptr,":%s WHOIS %s :%s", 1,parc,parv) !=
          HUNTED_ISME)
        return 0;
      parv[1] = parv[2];
    }

  if(!IsAnyOper(sptr) && !MyConnect(sptr)) /* pace non local requests */
    {
      if((last_used + ConfigFileEntry.whois_wait) > CurrentTime)
        {
          /* Unfortunately, returning anything to a non local
           * request =might= increase sendq to be usable in a split hack
           * Sorry gang ;-( - Dianora
           */
          return 0;
        }
      else
        {
          last_used = CurrentTime;
        }
    }

  nick = parv[1];
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';

  (void)collapse(nick);
  wilds = (strchr(nick, '?') || strchr(nick, '*'));

  /*
  ** We're no longer allowing remote users to generate
  ** requests with wildcards.
  */
#ifdef NO_WHOIS_WILDCARDS
  if (!MyConnect(sptr) && wilds)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
		 me.name, parv[0], nick);
      return 0;
    }
#endif

  /* If the nick doesn't have any wild cards in it,
   * then just pick it up from the hash table
   * - Dianora 
   */

  if(!wilds)
    {
      if( (acptr = hash_find_client(nick,(struct Client *)NULL)) )
	{
	  if(IsPerson(acptr))
	    {
	      (void)single_whois(sptr,acptr,wilds);
	      found = YES;
	    }
	}
    }
  else
    {
      /* Oh-oh wilds is true so have to do it the hard expensive way */
      found = global_whois(sptr, nick, wilds);
    }

  if(found)
    sendto_one(sptr, form_str(RPL_ENDOFWHOIS), me.name, parv[0], parv[1]);
  else
    sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name, parv[0], nick);

  return 0;
}

/*
 * global_whois()
 *
 * Inputs	- sptr client to report to
 *		- acptr client to report on
 *		- wilds whether wildchar char or not
 * Output	- if found return 1
 * Side Effects	- do a single whois on given client
 * 		  writing results to sptr
 */

static int global_whois(struct Client *sptr, char *nick, int wilds)
{
  struct Client *acptr;
  int found = NO;

  for (acptr = GlobalClientList; (acptr = next_client(acptr, nick));
       acptr = acptr->next)
    {
      if (IsServer(acptr))
	continue;
      /*
       * I'm always last :-) and acptr->next == NULL!!
       */
      if (IsMe(acptr))
	break;
      /*
       * 'Rules' established for sending a WHOIS reply:
       *
       *
       * - if wildcards are being used dont send a reply if
       *   the querier isnt any common channels and the
       *   client in question is invisible and wildcards are
       *   in use (allow exact matches only);
       *
       * - only send replies about common or public channels
       *   the target user(s) are on;
       */

      if(!IsRegistered(acptr))
	continue;

      if(single_whois(sptr, acptr, wilds))
	found = 1;
    }

  return (found);
}

/*
 * single_whois()
 *
 * Inputs	- sptr client to report to
 *		- acptr client to report on
 *		- wilds whether wildchar char or not
 * Output	- if found return 1
 * Side Effects	- do a single whois on given client
 * 		  writing results to sptr
 */

static int single_whois(struct Client *sptr,struct Client *acptr,int wilds)
{
  char buf[BUFSIZE];
  char *chname;
  static struct User UnknownUser =
  {
    NULL,       /* next */
    NULL,       /* channel */
    NULL,       /* invited */
    NULL,       /* away */
    0,          /* last */
    1,          /* refcount */
    0,          /* joined */
    "<Unknown>" /* server */
  };
  struct SLink  *lp;
  char *name;
  struct User   *user;
  struct Client *a2cptr;
  struct Channel *chptr;
  struct Channel *bchan;
  int   len;
  int   mlen;
  int found_mode;
  int invis;
  int member;
  int showperson;

  user = acptr->user ? acptr->user : &UnknownUser;
  name = (!*acptr->name) ? "?" : acptr->name;
  invis = IsInvisible(acptr);
  member = (user->channel) ? 1 : 0;
  showperson = (wilds && !invis && !member) || !wilds;

  for (lp = user->channel; lp; lp = lp->next)
    {
      chptr = lp->value.chptr;
      member = IsMember(sptr, chptr);
      if (invis && !member)
	continue;
      if (member || (!invis && PubChannel(chptr)))
	{
	  showperson = 1;
	  break;
	}
      if (!invis && HiddenChannel(chptr) &&
	  !SecretChannel(chptr))
	showperson = 1;
    }

  if (!showperson)
    return 0;
          
  a2cptr = find_server(user->server);
          
  sendto_one(sptr, form_str(RPL_WHOISUSER), me.name,
	     sptr->name, name,
	     acptr->username, acptr->host, acptr->info);
  mlen = strlen(me.name) + strlen(sptr->name) + 6 +
    strlen(name);
  for (len = 0, *buf = '\0', lp = user->channel; lp;
       lp = lp->next)
    {
      chptr = lp->value.chptr;
      chname = chptr->chname;

      if (IsVchan(chptr))
	{
	  bchan = find_bchan (chptr);
	  if (bchan != NULL)
	    chname = bchan->chname;
	}

      if (ShowChannel(sptr, chptr))
	{
	  if (len + strlen(chname)
	      > (size_t) BUFSIZE - 4 - mlen)
	    {
	      sendto_one(sptr,
			 ":%s %d %s %s :%s",
			 me.name,
			 RPL_WHOISCHANNELS,
			 sptr->name, name, buf);
	      *buf = '\0';
	      len = 0;
	    }
	  found_mode = user_channel_mode(chptr, acptr);
	  if (found_mode & CHFL_CHANOP)
	    *(buf + len++) = '@';
	  else if (found_mode & CHFL_VOICE)
	    *(buf + len++) = '+';
	  if (len)
	    *(buf + len) = '\0';
	  (void)strcpy(buf + len, chname);
	  len += strlen(chptr->chname);
	  (void)strcat(buf + len, " ");
	  len++;
	}
    }
  if (buf[0] != '\0')
    sendto_one(sptr, form_str(RPL_WHOISCHANNELS),
	       me.name, sptr->name, name, buf);
          
  sendto_one(sptr, form_str(RPL_WHOISSERVER),
	     me.name, sptr->name, name, user->server,
	     a2cptr?a2cptr->info:"*Not On This Net*");

  if (user->away)
    sendto_one(sptr, form_str(RPL_AWAY), me.name,
	       sptr->name, name, user->away);

  if (IsAnyOper(acptr))
    {
      sendto_one(sptr, form_str(RPL_WHOISOPERATOR),
		 me.name, sptr->name, name);

      if (IsAdmin(acptr))
	sendto_one(sptr, form_str(RPL_WHOISADMIN),
		   me.name, sptr->name, name);
    }

  if (ConfigFileEntry.whois_notice && 
      (MyOper(acptr)) && ((acptr)->umodes & FLAGS_SPY) &&
      (MyConnect(sptr)) && (IsPerson(sptr)) && (acptr != sptr))
    sendto_one(acptr,
	       ":%s NOTICE %s :*** Notice -- %s (%s@%s) is doing a /whois on you.",
	       me.name, acptr->name, sptr->name, sptr->username,
	       sptr->host);
  
  
  if (acptr->user && MyConnect(acptr))
    sendto_one(sptr, form_str(RPL_WHOISIDLE),
	       me.name, sptr->name, name,
	       CurrentTime - user->last,
	       acptr->firsttime);
  
  return 1;
}

/*
** ms_whois
**      parv[0] = sender prefix
**      parv[1] = nickname masklist
*/
int     ms_whois(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  /* If its running as a hub, and linked with lazy links
   * then allow leaf to use normal client m_names()
   * other wise, ignore it.
   */

  if( ConfigFileEntry.hub )
    {
      if(!IsCapable(cptr->from,CAP_LL))
	return 0;
    }

  return( m_whois(cptr,sptr,parc,parv) );
}
