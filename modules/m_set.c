/************************************************************************
 *   IRC - Internet Relay Chat, src/m_version.c
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
#include "fdlist.h"
#include "s_bsd.h"
#include "s_serv.h"
#include "send.h"
#include "common.h"   /* for NO */
#include "channel.h"  /* for server_was_split */
#include "s_log.h"
#include "msg.h"

#include <stdlib.h>  /* atoi */

struct Message set_msgtab = {
  MSG_SET, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_error, mo_set}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_SET, &set_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_SET);
}

char *_version = "20001122";

/*
 * set_parser - find the correct int. to return 
 * so we can switch() it.
 * KEY:  0 - MAX
 *       1 - AUTOCONN
 *       2 - IDLETIME
 *       3 - FLUDNUM
 *       4 - FLUDTIME
 *       5 - FLUDBLOCK
 *       6 - DRONETIME
 *       7 - DRONECOUNT
 *       8 - SPAMNUM
 *       9 - SPAMTIME
 *	10 - LOG
 * - rjp
 */

#define TOKEN_MAX 0
#define TOKEN_AUTOCONN 1
#define TOKEN_IDLETIME 2
#define TOKEN_FLUDNUM 3
#define TOKEN_FLUDTIME 4
#define TOKEN_FLUDBLOCK 5
#define TOKEN_DRONETIME 6
#define TOKEN_DRONECOUNT 7
#define TOKEN_SPAMNUM 8
#define TOKEN_SPAMTIME 9
#define TOKEN_LOG 10
#define TOKEN_HIDE 11
#define TOKEN_BAD 12

char *set_token_table[] = {
  "MAX",
  "AUTOCONN",
  "IDLETIME",
  "FLUDNUM",
  "FLUDTIME",
  "FLUDBLOCK",
  "DRONETIME",
  "DRONECOUNT",
  "SPAMNUM",
  "SPAMTIME",
  "LOG",
  "HIDE",
  NULL
};

int set_parser(const char* parsethis)
{
  int i;

  for( i = 0; set_token_table[i]; i++ )
    {
      if(!irccmp(set_token_table[i], parsethis))
        return i;
    }
  return TOKEN_BAD;
}

/*
 * mo_set - SET command handler
 * set options while running
 */
int mo_set(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char *command;
  int cnum;

  if (!MyClient(sptr) || !IsAnyOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      command = parv[1];
      cnum = set_parser(command);
/* This strcasecmp crap is annoying.. a switch() would be better.. 
 * - rjp
 */
      switch(cnum)
        {
        case TOKEN_MAX:
          if (parc > 2)
            {
              int new_value = atoi(parv[2]);
              if (new_value > MASTER_MAX)
                {
                  sendto_one(sptr,
                             ":%s NOTICE %s :You cannot set MAXCLIENTS to > MASTER_MAX (%d)",
                             me.name, parv[0], MASTER_MAX);
                  return 0;
                }
              if (new_value < 32)
                {
                  sendto_one(sptr, ":%s NOTICE %s :You cannot set MAXCLIENTS to < 32 (%d:%d)",
                             me.name, parv[0], 
			     GlobalSetOptions.maxclients,
			     highest_fd);
                  return 0;
                }
              GlobalSetOptions.maxclients = new_value;
              sendto_realops("%s!%s@%s set new MAXCLIENTS to %d (%d current)",
                             parv[0], sptr->username, sptr->host, 
			     GlobalSetOptions.maxclients, Count.local);
              return 0;
            }
          sendto_one(sptr, ":%s NOTICE %s :Current Maxclients = %d (%d)",
                     me.name, parv[0], GlobalSetOptions.maxclients,
		     Count.local);
          return 0;
          break;

        case TOKEN_AUTOCONN:
          if(parc > 3)
            {
              int newval = atoi(parv[3]);

              if(!irccmp(parv[2],"ALL"))
                {
                  sendto_realops(
                                 "%s has changed AUTOCONN ALL to %i",
                                 parv[0], newval);
                  GlobalSetOptions.autoconn = newval;
                }
              else
                set_autoconn(sptr,parv[0],parv[2],newval);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :AUTOCONN ALL is currently %i",
                         me.name, parv[0], GlobalSetOptions.autoconn);
            }
          return 0;
          break;

          case TOKEN_IDLETIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);
                if(newval == 0)
                  {
                    sendto_realops("%s has disabled IDLE_CHECK",
                                   parv[0]);
                    GlobalSetOptions.idletime = 0;
                  }
                else
                  {
                    sendto_realops("%s has changed IDLETIME to %i",
                                   parv[0], newval);
                    GlobalSetOptions.idletime = (newval*60);
                  }
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :IDLETIME is currently %i",
                           me.name, parv[0], 
			   GlobalSetOptions.idletime/60);
              }
            return 0;
            break;

          case TOKEN_FLUDNUM:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval <= 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDNUM must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                GlobalSetOptions.fludnum = newval;
                sendto_realops("%s has changed FLUDNUM to %i",
                               parv[0], GlobalSetOptions.fludnum);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDNUM is currently %i",
                           me.name, parv[0], GlobalSetOptions.fludnum);
              }
            return 0;
            break;

          case TOKEN_FLUDTIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval <= 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDTIME must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                GlobalSetOptions.fludtime = newval;
                sendto_realops("%s has changed FLUDTIME to %i",
                               parv[0], GlobalSetOptions.fludtime);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDTIME is currently %i",
                           me.name, parv[0], GlobalSetOptions.fludtime);
              }
            return 0;       
            break;

          case TOKEN_FLUDBLOCK:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK must be >= 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                GlobalSetOptions.fludblock = newval;
                if(GlobalSetOptions.fludblock == 0)
                  {
                    sendto_realops("%s has disabled flud detection/protection",
                                   parv[0]);
                  }
                else
                  {
                    sendto_realops("%s has changed FLUDBLOCK to %i",
                                   parv[0],GlobalSetOptions.fludblock);
                  }
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK is currently %i",
                           me.name, parv[0], GlobalSetOptions.fludblock);
              }
            return 0;       
            break;

          case TOKEN_DRONETIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :DRONETIME must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                GlobalSetOptions.dronetime = newval;
                if(GlobalSetOptions.dronetime == 0)
                  sendto_realops("%s has disabled the ANTI_DRONE_FLOOD code",
                                 parv[0]);
                else
                  sendto_realops("%s has changed DRONETIME to %i",
                                 parv[0], GlobalSetOptions.dronetime);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :DRONETIME is currently %i",
                           me.name, parv[0], GlobalSetOptions.dronetime);
              }
            return 0;
            break;

        case TOKEN_DRONECOUNT:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT must be > 0",
                             me.name, parv[0]);
                  return 0;
                }       
              GlobalSetOptions.dronecount = newval;
              sendto_realops("%s has changed DRONECOUNT to %i",
                             parv[0], GlobalSetOptions.dronecount);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT is currently %i",
                         me.name, parv[0], GlobalSetOptions.dronecount);
            }
          return 0;
          break;

          case TOKEN_SPAMNUM:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :SPAMNUM must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }
                if(newval == 0)
                  {
                    sendto_realops("%s has disabled ANTI_SPAMBOT",
                                   parv[0]);
                    return 0;
                  }

                if(newval < MIN_SPAM_NUM)
                  GlobalSetOptions.spam_num = MIN_SPAM_NUM;
                else
                  GlobalSetOptions.spam_num = newval;
                sendto_realops("%s has changed SPAMNUM to %i",
                               parv[0], GlobalSetOptions.spam_num);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :SPAMNUM is currently %i",
                           me.name, parv[0], 
			   GlobalSetOptions.spam_num);
              }

            return 0;
            break;

        case TOKEN_SPAMTIME:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPAMTIME must be > 0",
                             me.name, parv[0]);
                  return 0;
                }
              if(newval < MIN_SPAM_TIME)
                GlobalSetOptions.spam_time = MIN_SPAM_TIME;
              else
                GlobalSetOptions.spam_time = newval;
              sendto_realops("%s has changed SPAMTIME to %i",
                             parv[0], GlobalSetOptions.spam_time);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is currently %i",
                         me.name, parv[0], GlobalSetOptions.spam_time);
            }
          return 0;
          break;

        case TOKEN_LOG:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);
              const char *log_level_as_string;

              if(newval < L_WARN)
                {
                  sendto_one(sptr, ":%s NOTICE %s :LOG must be > %d (L_WARN)",
                             me.name, parv[0], L_WARN);
                  return 0;
                }
              if(newval > L_DEBUG)
                newval = L_DEBUG;
              set_log_level(newval); 
              log_level_as_string = get_log_level_as_string(newval);
              sendto_realops("%s has changed LOG level to %i (%s)",
                             parv[0], newval, log_level_as_string);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :LOG level is currently %i (%s)",
                         me.name, parv[0], get_log_level(),
                         get_log_level_as_string(get_log_level()));
            }
          return 0;
          break;

        case TOKEN_HIDE:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval)
                GlobalSetOptions.hide_server = 1;
	      else
		GlobalSetOptions.hide_server = 0;

              sendto_realops("%s has changed HIDE to %i",
                             parv[0], GlobalSetOptions.hide_server);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :HIDE is currently %i",
                         me.name, parv[0], GlobalSetOptions.hide_server);

            }
          return 0;
          break;

        default:
        case TOKEN_BAD:
          break;
        }
    }

  sendto_one(sptr, ":%s NOTICE %s :Options: MAX AUTOCONN",
             me.name, parv[0]);
  sendto_one(sptr, ":%s NOTICE %s :Options: FLUDNUM, FLUDTIME, FLUDBLOCK",
             me.name, parv[0]);
  sendto_one(sptr, ":%s NOTICE %s :Options: DRONETIME, DRONECOUNT",
             me.name, parv[0]);
  sendto_one(sptr, ":%s NOTICE %s :Options: SPAMNUM, SPAMTIME",
             me.name, parv[0]);
  sendto_one(sptr, ":%s NOTICE %s :Options: IDLETIME",
             me.name, parv[0]);
  sendto_one(sptr, ":%s NOTICE %s :Options: LOG",
             me.name, parv[0]);
  return 0;
}

