/************************************************************************
 *   IRC - Internet Relay Chat, src/m_sjoin.c
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
#include "channel.h"
#include "vchannel.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "send.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * ms_sjoin
 * parv[0] - sender
 * parv[1] - TS
 * parv[2] - channel
 * parv[3] - modes + n arguments (key and/or limit)
 * parv[4+n] - flags+nick list (all in one parameter)
 * 
 * process a SJOIN, taking the TS's into account to either ignore the
 * incoming modes or undo the existing ones or merge them, and JOIN
 * all the specified users while sending JOIN/MODEs to non-TS servers
 * and to clients
 *
 * This function is a festering pile of doggie doo-doo left in the
 * hot sun for 2 weeks, coated with flies. -db
 */

static  char    modebuf[MODEBUFLEN];
static  char    parabuf[MODEBUFLEN];
static  char    *mbuf;
static  int     pargs;

void set_final_mode(struct Mode *mode,struct Mode *oldmode);

int     ms_sjoin(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Channel *chptr;
  struct Channel *top_chptr;	/* ZZZ vchans */
  struct Client       *acptr;
  time_t        newts;
  time_t        oldts;
  time_t        tstosend;
  static        struct Mode mode, *oldmode;
  struct SLink  *l;
  int   args = 0, keep_our_modes = 1, keep_new_modes = 1;
  int   doesop = 0, what = 0, fl, people = 0, isnew;
  register      char *s, *s0;
  static        char sjbuf[BUFSIZE];
  char    *t = sjbuf;
  char    *p;

  if (IsClient(sptr) || parc < 5)
    return 0;
  if (!IsChannelName(parv[2]))
    return 0;

  if (!check_channel_name(parv[2]))
     { 
       return 0;
     }

  /* comstud server did this, SJOIN's for
   * local channels can't happen.
   */

  if(*parv[2] == '&')
    return 0;

  mbuf = modebuf;
  pargs = 0;
  newts = atol(parv[1]);
  memset(&mode, 0, sizeof(mode));

  s = parv[3];
  while (*s)
    switch(*(s++))
      {
      case 'i':
        mode.mode |= MODE_INVITEONLY;
        break;
      case 'n':
        mode.mode |= MODE_NOPRIVMSGS;
        break;
      case 'p':
        mode.mode |= MODE_PRIVATE;
        break;
      case 's':
        mode.mode |= MODE_SECRET;
        break;
      case 'm':
        mode.mode |= MODE_MODERATED;
        break;
      case 't':
        mode.mode |= MODE_TOPICLIMIT;
        break;
      case 'k':
         strncpy_irc(mode.key, parv[4 + args], KEYLEN);
        args++;
        if (parc < 5+args) return 0;
        break;
      case 'l':
        mode.limit = atoi(parv[4+args]);
        args++;
        if (parc < 5+args) return 0;
        break;
      }

  *parabuf = '\0';

  isnew = ChannelExists(parv[2]) ? 0 : 1;
  chptr = get_channel(sptr, parv[2], CREATE);

  /* ZZZ vchan cruft */
  /* vchans are encoded as "##mainchanname_timestamp" */

  top_chptr = NULL;

  if(parv[2][1] == '#')
    {
      char *p;

      /* possible sub vchan being sent along ? */
      if((p = strchr(parv[2],'_')))
	{
	  /* quite possibly now. To confirm I could
	   * check the encoded timestamp to see if it matches
	   * the given timestamp for this channel. Maybe do 
	   * that later. - db
	   */

	  *p = '\0';	/* fugly hack for now ... */

	  /* + 1 skip the extra '#' in the name */
	  if((top_chptr = hash_find_channel((parv[2] + 1), NULL)))
	    {
sendto_realops("ZZZ Found top_chptr for %s", (parv[2] + 1));

	      if (top_chptr->next_vchan)
		{
		  chptr->next_vchan = top_chptr->next_vchan;
		  top_chptr->next_vchan->prev_vchan = chptr;
		}

	      top_chptr->next_vchan = chptr;
	      chptr->prev_vchan = top_chptr;
	    }
	  else
	    {
sendto_realops("ZZZ Creating top_chptr for %s", (parv[2] + 1));

	      top_chptr = get_channel(sptr, (parv[2] + 1), CREATE);

	      top_chptr->next_vchan = chptr;
	      chptr->prev_vchan = top_chptr;
	    }

	  *p = '_';	/* fugly hack, restore '_' */
	}
    }

  /*
   * bogus ban removal code.
   * If I see that this SJOIN will mean I keep my ops, but lose
   * the ops from the joining server, I keep track of that in the channel
   * structure. I set keep_their_modes to NO
   * since the joining server will not be keeping their ops, I can
   * ignore any of the bans sent from that server. The moment
   * I see a chanop MODE being sent, I can set this flag back to YES.
   *
   * There is one degenerate case. Two servers connect bursting
   * at the same time. It might cause a problem, or it might not.
   * In the case that it becomes an issue, then a short list
   * of servers having their modes ignored would have to be linked
   * into the channel structure. This would be only an issue
   * on hubs.
   * Hopefully, it will be much of a problem.
   *
   * Bogus bans on the server losing its chanops is trivial. All
   * bans placed on the local server during its split, with bogus chanops
   * I can just remove.
   *
   * -Dianora
   */
  
  chptr->keep_their_modes = YES;

  /* locally created channels do not get created from SJOIN's
   * any SJOIN destroys the locally_created flag
   *
   * -Dianora
   */

  chptr->locally_created = NO;
  oldts = chptr->channelts;

  /* If the TS goes to 0 for whatever reason, flag it
   * ya, I know its an invasion of privacy for those channels that
   * want to keep TS 0 *shrug* sorry
   * -Dianora
   */

  if(!isnew && !newts && oldts)
    {
      if(IsVchan(chptr) && top_chptr)
	{
	  sendto_channel_butserv(chptr, &me,
	 ":%s NOTICE %s :*** Notice -- TS for %s changed from %lu to 0",
              me.name, top_chptr->chname, top_chptr->chname, oldts);
	}
      else
	{
	  sendto_channel_butserv(chptr, &me,
	 ":%s NOTICE %s :*** Notice -- TS for %s changed from %lu to 0",
              me.name, chptr->chname, chptr->chname, oldts);
	}
      sendto_realops("Server %s changing TS on %s from %lu to 0",
                     sptr->name,parv[2],oldts);
    }

  doesop = (parv[4+args][0] == '@' || parv[4+args][1] == '@');

  oldmode = &chptr->mode;

  if (isnew)
    chptr->channelts = tstosend = newts;
  else if (newts == 0 || oldts == 0)
    chptr->channelts = tstosend = 0;
  else if (!newts)
    chptr->channelts = tstosend = oldts;
  else if (newts == oldts)
    tstosend = oldts;
  else if (newts < oldts)
    {
      if (doesop)
        keep_our_modes = NO;

      clear_bans_exceptions_denies(sptr,chptr);

      if (chptr->opcount && !doesop)
          tstosend = oldts;
      else
        chptr->channelts = tstosend = newts;
    }
  else
    {
      chptr->keep_their_modes = NO;

      if (chptr->opcount)
        keep_new_modes = NO;
      if (doesop && !chptr->opcount)
        {
          chptr->channelts = tstosend = newts;
        }
      else
        tstosend = oldts;
    }

  if (!keep_new_modes)
    mode = *oldmode;
  else if (keep_our_modes)
    {
      mode.mode |= oldmode->mode;
      if (oldmode->limit > mode.limit)
        mode.limit = oldmode->limit;
      if (strcmp(mode.key, oldmode->key) < 0)
        strcpy(mode.key, oldmode->key);
    }

  set_final_mode(&mode,oldmode);
  chptr->mode = mode;

  if (!keep_our_modes)
    {
      what = 0;
      for (l = chptr->members; l && l->value.cptr; l = l->next)
        {
          if (l->flags & MODE_CHANOP)
            {
              if( chptr->opcount )
                chptr->opcount--;

              if (what != -1)
                {
                  *mbuf++ = '-';
                  what = -1;
                }
              *mbuf++ = 'o';
              strcat(parabuf, l->value.cptr->name);
              strcat(parabuf, " ");
              pargs++;
              if (pargs >= MAXMODEPARAMS)
                {
                  *mbuf = '\0';
		  if(IsVchan(chptr) && top_chptr)
		    {
		      sendto_channel_butserv(chptr, sptr,
					     ":%s MODE %s %s %s", parv[0],
					     top_chptr->chname,
					     modebuf, parabuf );
		    }
		  else
		    {
		      sendto_channel_butserv(chptr, sptr,
					     ":%s MODE %s %s %s", parv[0],
					     chptr->chname, modebuf, parabuf );
		    }
                  mbuf = modebuf;
                  *mbuf = parabuf[0] = '\0';
                  pargs = what = 0;
                }
              l->flags &= ~MODE_CHANOP;
            }
          if (l->flags & MODE_VOICE)
            {
              if (what != -1)
                {
                  *mbuf++ = '-';
                  what = -1;
                }
              *mbuf++ = 'v';
              strcat(parabuf, l->value.cptr->name);
              strcat(parabuf, " ");
              pargs++;
              if (pargs >= MAXMODEPARAMS)
                {
                  *mbuf = '\0';
		  if(IsVchan(chptr) && top_chptr)
		    {
		      sendto_channel_butserv(chptr, sptr,
					     ":%s MODE %s %s %s", parv[0],
					     top_chptr->chname,
					     modebuf, parabuf );
		    }
		  else
		    {
		      sendto_channel_butserv(chptr, sptr,
					     ":%s MODE %s %s %s", parv[0],
					     chptr->chname, modebuf, parabuf );
		    }
                  mbuf = modebuf;
                  *mbuf = parabuf[0] = '\0';
                  pargs = what = 0;
                }
              l->flags &= ~MODE_VOICE;
            }
        }
      if(IsVchan(chptr) && top_chptr)
	{
	  sendto_channel_butserv(chptr, &me,
	 ":%s NOTICE %s :*** Notice -- TS for %s changed from %lu to %lu",
            me.name, top_chptr->chname, top_chptr->chname, oldts, newts);
	}
      else
	{
	  sendto_channel_butserv(chptr, &me,
	 ":%s NOTICE %s :*** Notice -- TS for %s changed from %lu to %lu",
            me.name, chptr->chname, chptr->chname, oldts, newts);
	}
    }
  if (mbuf != modebuf)
    {
      *mbuf = '\0';
      if(IsVchan(chptr) && top_chptr)
	{
	  sendto_channel_butserv(chptr, sptr,
				 ":%s MODE %s %s %s", parv[0],
				 top_chptr->chname, modebuf, parabuf );
	}
      else
	{
	  sendto_channel_butserv(chptr, sptr,
				 ":%s MODE %s %s %s", parv[0],
				 chptr->chname, modebuf, parabuf );
	}
    }

  *modebuf = *parabuf = '\0';
  if (parv[3][0] != '0' && keep_new_modes)
    channel_modes(chptr, sptr, modebuf, parabuf);
  else
    {
      modebuf[0] = '0';
      modebuf[1] = '\0';
    }

  ircsprintf(t, ":%s SJOIN %lu %s %s %s :", parv[0], tstosend, parv[2],
          modebuf, parabuf);
  t += strlen(t);

  mbuf = modebuf;
  parabuf[0] = '\0';
  pargs = 0;
  *mbuf++ = '+';

  for (s = s0 = strtoken(&p, parv[args+4], " "); s;
       s = s0 = strtoken(&p, (char *)NULL, " "))
    {
      fl = 0;

      if (*s == '@' || s[1] == '@')
        fl |= MODE_CHANOP;
      if (*s == '+' || s[1] == '+')
        fl |= MODE_VOICE;

      if (!keep_new_modes)
       {
        if (fl & MODE_CHANOP)
          {
            fl = MODE_DEOPPED;
          }
        else
          {
            fl = 0;
          }
       }
      while (*s == '@' || *s == '+')
        s++;
      if (!(acptr = find_chasing(sptr, s, NULL)))
        continue;
      if (acptr->from != cptr)
        continue;
      people++;
      if (!IsMember(acptr, chptr))
        {
          add_user_to_channel(chptr, acptr, fl);
	  if(IsVchan(chptr) && top_chptr)
	    {
	      sendto_channel_butserv(chptr, acptr, ":%s JOIN :%s",
				     s, top_chptr->chname);
	    }
	  else
	    {
	      sendto_channel_butserv(chptr, acptr, ":%s JOIN :%s",
				     s, parv[2]);
	    }
        }
      if (keep_new_modes)
        strcpy(t, s0);
      else
        strcpy(t, s);
      t += strlen(t);
      *t++ = ' ';
      if (fl & MODE_CHANOP)
        {
          *mbuf++ = 'o';
          strcat(parabuf, s);
          strcat(parabuf, " ");
          pargs++;
          if (pargs >= MAXMODEPARAMS)
            {
              *mbuf = '\0';
	      if(IsVchan(chptr) && top_chptr)
		{
		  sendto_channel_butserv(chptr, sptr,
					 ":%s MODE %s %s %s", parv[0],
					 top_chptr->chname, modebuf, parabuf );
		}
	      else
		{
		  sendto_channel_butserv(chptr, sptr,
					 ":%s MODE %s %s %s", parv[0],
					 chptr->chname, modebuf, parabuf );
		}
              mbuf = modebuf;
              *mbuf++ = '+';
              parabuf[0] = '\0';
              pargs = 0;
            }
        }
      if (fl & MODE_VOICE)
        {
          *mbuf++ = 'v';
          strcat(parabuf, s);
          strcat(parabuf, " ");
          pargs++;
          if (pargs >= MAXMODEPARAMS)
            {
              *mbuf = '\0';
	      if(IsVchan(chptr) && top_chptr)
		{
		  sendto_channel_butserv(chptr, sptr,
					 ":%s MODE %s %s %s", parv[0],
					 top_chptr->chname, modebuf, parabuf );
		}
	      else
		{
		  sendto_channel_butserv(chptr, sptr,
					 ":%s MODE %s %s %s", parv[0],
					 chptr->chname, modebuf, parabuf );
		}
              mbuf = modebuf;
              *mbuf++ = '+';
              parabuf[0] = '\0';
              pargs = 0;
            }
        }
    }
  
  *mbuf = '\0';
  if (pargs)
    {
      if(IsVchan(chptr) && top_chptr)
	{
	  sendto_channel_butserv(chptr, sptr,
				 ":%s MODE %s %s %s", parv[0],
				 top_chptr->chname, modebuf, parabuf );
	}
      else
	{
	  sendto_channel_butserv(chptr, sptr,
				 ":%s MODE %s %s %s", parv[0],
				 chptr->chname, modebuf, parabuf );
	}
    }

  if (people)
    {
      if (t[-1] == ' ')
        t[-1] = '\0';
      else
        *t = '\0';
      sendto_match_servs(chptr, cptr, "%s", sjbuf);
    }
  return 0;
}

/* ZZZ inline this eventually */

void
set_final_mode(struct Mode *mode,struct Mode *oldmode)
{
  int what = 0;
  char numeric[16];
  char *s;

  if((MODE_PRIVATE    & mode->mode) && !(MODE_PRIVATE    & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'p';
    }
  if((MODE_SECRET     & mode->mode) && !(MODE_SECRET     & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 's';
    }
  if((MODE_MODERATED  & mode->mode) && !(MODE_MODERATED  & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'm';
    }
  if((MODE_NOPRIVMSGS & mode->mode) && !(MODE_NOPRIVMSGS & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'n';
    }
  if((MODE_TOPICLIMIT & mode->mode) && !(MODE_TOPICLIMIT & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 't';
    }
  if((MODE_INVITEONLY & mode->mode) && !(MODE_INVITEONLY & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;             /* This one is actually redundant now */
        }
      *mbuf++ = 'i';
    }

  if((MODE_PRIVATE    & oldmode->mode) && !(MODE_PRIVATE    & mode->mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'p';
    }
  if((MODE_SECRET     & oldmode->mode) && !(MODE_SECRET     & mode->mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 's';
    }
  if((MODE_MODERATED  & oldmode->mode) && !(MODE_MODERATED  & mode->mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'm';
    }
  if((MODE_NOPRIVMSGS & oldmode->mode) && !(MODE_NOPRIVMSGS & mode->mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'n';
    }
  if((MODE_TOPICLIMIT & oldmode->mode) && !(MODE_TOPICLIMIT & mode->mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 't';
    }
  if((MODE_INVITEONLY & oldmode->mode) && !(MODE_INVITEONLY & mode->mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'i';
    }

  if (oldmode->limit && !mode->limit)
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'l';
    }
  if (oldmode->key[0] && !mode->key[0])
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'k';
      strcat(parabuf, oldmode->key);
      strcat(parabuf, " ");
      pargs++;
    }
  if (mode->limit && oldmode->limit != mode->limit)
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'l';
      ircsprintf(numeric, "%d", mode->limit);
      if ((s = strchr(numeric, ' ')))
        *s = '\0';
      strcat(parabuf, numeric);
      strcat(parabuf, " ");
      pargs++;
    }
  if (mode->key[0] && strcmp(oldmode->key, mode->key))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'k';
      strcat(parabuf, mode->key);
      strcat(parabuf, " ");
      pargs++;
    }
}
  
