/************************************************************************
 *   IRC - Internet Relay Chat, src/s_debug.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
#include "s_debug.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "dbuf.h"
#include "hash.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "res.h"
#include "s_conf.h"
#include "s_log.h"
#include "scache.h"
#include "send.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/resource.h>


/*
 * Option string.  Must be before #ifdef DEBUGMODE.
 */
const char serveropts[] = {
#ifdef  SENDQ_ALWAYS
  'A',
#endif
#ifdef  CHROOTDIR
  'c',
#endif
#ifdef  CMDLINE_CONFIG
  'C',
#endif
#ifdef        DO_ID
  'd',
#endif
#ifdef  DEBUGMODE
  'D',
#endif
#ifdef  LOCOP_REHASH
  'e',
#endif
#ifdef  OPER_REHASH
  'E',
#endif
#ifdef  HUB
  'H',
#endif
#ifdef  SHOW_INVISIBLE_LUSERS
  'i',
#endif
#ifndef NO_DEFAULT_INVISIBLE
  'I',
#endif
#ifdef  OPER_KILL
# ifdef  LOCAL_KILL_ONLY
  'k',
# else
  'K',
# endif
#endif
#ifdef  IDLE_FROM_MSG
  'M',
#endif
#ifdef  CRYPT_OPER_PASSWORD
  'p',
#endif
#ifdef  CRYPT_LINK_PASSWORD
  'P',
#endif
#ifdef  LOCOP_RESTART
  'r',
#endif
#ifdef  OPER_RESTART
  'R',
#endif
#ifdef  OPER_REMOTE
  't',
#endif
#ifdef  VALLOC
  'V',
#endif
#ifdef  USE_SYSLOG
  'Y',
#endif
  'Z',
  ' ',
  'T',
  'S',
#ifdef TS_CURRENT
  '0' + TS_CURRENT,
#endif
/* th+hybrid servers ONLY do TS */
/* th+hybrid servers ALWAYS do TS_WARNINGS */
  'o',
  'w',
  '\0'
};

void debug(int level, char *format, ...)
{
  static char debugbuf[1024];
  va_list args;
  int err = errno;

  if ((debuglevel >= 0) && (level <= debuglevel)) {
    va_start(args, format);

    vsprintf(debugbuf, format, args);
    va_end(args);

    log(L_DEBUG, debugbuf);
  }
  errno = err;
} /* debug() */

/*
 * This is part of the STATS replies. There is no offical numeric for this
 * since this isnt an official command, in much the same way as HASH isnt.
 * It is also possible that some systems wont support this call or have
 * different field names for "struct rusage".
 * -avalon
 */
void send_usage(struct Client *cptr, char *nick)
{
  struct rusage  rus;
  time_t         secs;
  time_t         rup;
#ifdef  hz
# define hzz hz
#else
# ifdef HZ
#  define hzz HZ
# else
  int   hzz = 1;
# endif
#endif

  if (getrusage(RUSAGE_SELF, &rus) == -1)
    {
      sendto_one(cptr,":%s NOTICE %s :Getruseage error: %s.",
                 me.name, nick, strerror(errno));
      return;
    }
  secs = rus.ru_utime.tv_sec + rus.ru_stime.tv_sec;
  if (0 == secs)
    secs = 1;

  rup = (CurrentTime - me.since) * hzz;
  if (0 == rup)
    rup = 1;


  sendto_one(cptr,
             ":%s %d %s :CPU Secs %d:%d User %d:%d System %d:%d",
             me.name, RPL_STATSDEBUG, nick, secs/60, secs%60,
             rus.ru_utime.tv_sec/60, rus.ru_utime.tv_sec%60,
             rus.ru_stime.tv_sec/60, rus.ru_stime.tv_sec%60);
  sendto_one(cptr, ":%s %d %s :RSS %d ShMem %d Data %d Stack %d",
             me.name, RPL_STATSDEBUG, nick, rus.ru_maxrss,
             rus.ru_ixrss / rup, rus.ru_idrss / rup,
             rus.ru_isrss / rup);
  sendto_one(cptr, ":%s %d %s :Swaps %d Reclaims %d Faults %d",
             me.name, RPL_STATSDEBUG, nick, rus.ru_nswap,
             rus.ru_minflt, rus.ru_majflt);
  sendto_one(cptr, ":%s %d %s :Block in %d out %d",
             me.name, RPL_STATSDEBUG, nick, rus.ru_inblock,
             rus.ru_oublock);
  sendto_one(cptr, ":%s %d %s :Msg Rcv %d Send %d",
             me.name, RPL_STATSDEBUG, nick, rus.ru_msgrcv, rus.ru_msgsnd);
  sendto_one(cptr, ":%s %d %s :Signals %d Context Vol. %d Invol %d",
             me.name, RPL_STATSDEBUG, nick, rus.ru_nsignals,
             rus.ru_nvcsw, rus.ru_nivcsw);

  /* The counters that were here have been removed, 
   * Bleep and I (Dianora) contend they weren't useful for
   * even debugging.
   */

  sendto_one(cptr, ":%s %d %s :DBUF used %d allocated %d",
             me.name, RPL_STATSDEBUG, nick, DBufUsedCount, DBufCount);
  return;
}

void count_memory(struct Client* cptr, char* nick)
{
  struct Client*   acptr;
  struct SLink*    link;
  struct Channel*  chptr;
  struct ConfItem* aconf;
  struct Class*    cltmp;

  int away_count              = 0;      /* aways set */
  int channel_ban_count       = 0;      /* channel bans */
  int channel_count           = 0;      /* channels */
  int channel_invite_count    = 0;      /* channel invites */
  int channel_user_count      = 0;      /* channel users */
  int class_count             = 0;      /* classes */
  int conf_count              = 0;      /* conf lines */
  int invite_count            = 0;      /* users invited */
  int listener_count          = 0;      /* listeners */
  int local_client_conf_count = 0;      /* local client conf links */
  int local_client_count      = 0;      /* local clients */
  int ip_hash_count           = 0;      /* number of ip addresses hashed */
  int scache_count            = 0;      /* number of servers cached by scache */
  int remote_client_count     = 0;      /* remote clients */
  int server_count            = 0;      /* server structs */
  int slink_count             = 0;      /* slinks */
  int user_channel_count      = 0;      /* users in channels */
  int user_count              = 0;      /* user structs */

  size_t away_mem             = 0;      /* memory used by aways */
  size_t channel_ban_mem      = 0;      /* memory used by channel bans */
  size_t channel_mem          = 0;      /* memory used by channels */
  size_t conf_mem             = 0;      /* memory used by conf lines */
  size_t dbuf_mem             = 0;      /* memory used by dbufs */
  size_t listener_mem         = 0;      /* memory used by listeners */
  size_t local_client_mem     = 0;      /* memory used by local clients */
  size_t ip_hash_mem          = 0;      /* memory used by ip address hash */
  size_t scache_mem           = 0;      /* memory used by scache */
  size_t mtrie_conf_mem       = 0;      /* memory used by mtrie */
  size_t parser_mem           = 0;      /* memory used by the parser */
  size_t remote_client_mem    = 0;      /* memory used by remote clients */
  size_t resolver_mem         = 0;      /* memory used by resolver */
  size_t server_mem           = 0;      /* memory used by servers */
  size_t slink_mem            = 0;      /* memory used by slinks */
  size_t user_mem             = 0;      /* memory used by users */

  size_t client_mem_total     = 0;
  size_t channel_mem_total    = 0;

  size_t local_client_allocated  = 0;
  size_t local_client_used       = 0;
  size_t remote_client_allocated = 0;
  size_t remote_client_used      = 0;
  size_t slink_allocated         = 0;
  size_t slink_used              = 0;
  size_t user_allocated          = 0;
  size_t user_used               = 0;

#ifdef FLUD
  size_t flud_allocated          = 0;
  size_t flud_used               = 0;
#endif

  size_t mem_total = 0;

  for (acptr = GlobalClientList; acptr; acptr = acptr->next)
    {
      if (MyConnect(acptr))
        {
          ++local_client_count;
          for (link = acptr->confs; link; link = link->next)
            ++local_client_conf_count;
        }
      else if (!IsMe(cptr))
        ++remote_client_count;
      if (acptr->user)
        {
          ++user_count;
          for (link = acptr->user->invited; link; link = link->next)
            ++invite_count;
          for (link = acptr->user->channel; link; link = link->next)
            ++user_channel_count;
          if (acptr->user->away)
            {
              ++away_count;
              away_mem += (strlen(acptr->user->away) + 1);
            }
        }
    }
  local_client_mem  = local_client_count  * CLIENT_LOCAL_SIZE;
  remote_client_mem = remote_client_count * CLIENT_REMOTE_SIZE;
  user_mem          = user_count * sizeof(struct User);

  for (chptr = channel; chptr; chptr = chptr->nextch)
    {
      ++channel_count;
      channel_mem += (strlen(chptr->chname) + 1 + sizeof(struct Channel));
      for (link = chptr->members; link; link = link->next)
        ++channel_user_count;
      for (link = chptr->invites; link; link = link->next)
        ++channel_invite_count;
      for (link = chptr->banlist; link; link = link->next)
        {
          ++channel_ban_count;
          channel_ban_mem += (strlen(link->value.cp) + 1 + sizeof(struct SLink));
#ifdef BAN_INFO
          if (link->value.banptr->banstr)
            channel_ban_mem += strlen(link->value.banptr->banstr) + 1;
          if (link->value.banptr->who)
            channel_ban_mem += strlen(link->value.banptr->who) + 1;
#endif /* BAN_INFO */
        }
    }

  slink_count = local_client_conf_count + invite_count + user_channel_count +
                channel_user_count + channel_invite_count + channel_ban_count;   
  slink_mem = slink_count * sizeof(struct SLink);

  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    {
      ++conf_count;
      conf_mem += aconf->host ? strlen(aconf->host) + 1 : 0;
      conf_mem += aconf->passwd ? strlen(aconf->passwd) + 1 : 0;
      conf_mem += aconf->name ? strlen(aconf->name) + 1 : 0;
      conf_mem += sizeof(struct ConfItem);
    }

  for (cltmp = ClassList; cltmp; cltmp = cltmp->next)
    ++class_count;

  sendto_one(cptr, ":%s %d %s :Client Local %d(%d) Remote %d(%d)",
             me.name, RPL_STATSDEBUG, nick, local_client_count, 
             local_client_mem, remote_client_count, remote_client_mem);
  sendto_one(cptr, ":%s %d %s :Users %d(%d) Invites %d(%d)",
             me.name, RPL_STATSDEBUG, nick, user_count, user_mem,
             invite_count, invite_count * sizeof(struct SLink));
  sendto_one(cptr, ":%s %d %s :User channels %d(%d) Aways %d(%d)",
             me.name, RPL_STATSDEBUG, nick, user_channel_count, 
             user_channel_count * sizeof(struct SLink),
             away_count, away_mem);
  sendto_one(cptr, ":%s %d %s :Attached confs %d(%d)",
             me.name, RPL_STATSDEBUG, nick, local_client_conf_count, 
             local_client_conf_count * sizeof(struct SLink));

  client_mem_total = local_client_mem + remote_client_mem + user_mem +
                     user_channel_count * sizeof(struct SLink) + away_mem;
  client_mem_total += local_client_conf_count * sizeof(struct SLink) + 
                      invite_count * sizeof(struct SLink);

  sendto_one(cptr, ":%s %d %s :Conflines %d(%d)",
             me.name, RPL_STATSDEBUG, nick, conf_count, conf_mem);

  sendto_one(cptr, ":%s %d %s :Classes %d(%d)",
             me.name, RPL_STATSDEBUG, nick, class_count, 
             class_count * sizeof(struct Class));

  sendto_one(cptr, ":%s %d %s :Channels %d(%d) Bans %d(%d)",
             me.name, RPL_STATSDEBUG, nick, channel_count, channel_mem, 
             channel_ban_count, channel_ban_mem);
  sendto_one(cptr, ":%s %d %s :Channel members %d(%d) invite %d(%d)",
             me.name, RPL_STATSDEBUG, nick, channel_user_count, 
             channel_user_count * sizeof(struct SLink),
             channel_invite_count, channel_invite_count * sizeof(struct SLink));

  channel_mem_total = channel_mem + channel_ban_mem + 
                      channel_user_count * sizeof(struct SLink) + 
                      channel_invite_count * sizeof(struct SLink);

  sendto_one(cptr, ":%s %d %s :Hash: client %d chan %d",
             me.name, RPL_STATSDEBUG, nick, U_MAX, CH_MAX);

  dbuf_mem = DBufCount * sizeof(dbufbuf);
  sendto_one(cptr, ":%s %d %s :DBuf allocated %d(%d), used %d(%d)",
             me.name, RPL_STATSDEBUG, nick, DBufCount, dbuf_mem,
             DBufUsedCount, DBufUsedCount * sizeof(dbufbuf));

  resolver_mem = cres_mem(cptr);

  count_scache(&scache_count, &scache_mem);

  sendto_one(cptr, ":%s %d %s :scache %d(%d)",
             me.name, RPL_STATSDEBUG, nick,
             scache_count, scache_mem);

  count_ip_hash(&ip_hash_count, &ip_hash_mem);
  sendto_one(cptr, ":%s %d %s :iphash %d(%d)",
             me.name, RPL_STATSDEBUG, nick,
             ip_hash_count, ip_hash_mem);

  mem_total = channel_mem_total + client_mem_total + 
              conf_mem + class_count * sizeof(struct Class) + 
              dbuf_mem + resolver_mem + ip_hash_mem + scache_mem;

  sendto_one(cptr, ":%s %d %s :Total: channel %d client %d conf %d dbuf %d",
             me.name, RPL_STATSDEBUG, nick, channel_mem_total, 
             client_mem_total, conf_mem, dbuf_mem);


  count_local_client_memory(&local_client_allocated, 
                            &local_client_used);
  mem_total += local_client_allocated - local_client_mem;

  sendto_one(cptr, ":%s %d %s :Local client memory allocated: %d used: %d",
             me.name, RPL_STATSDEBUG, nick,
             local_client_allocated, local_client_used);


  count_remote_client_memory(&remote_client_allocated, 
                             &remote_client_used);
  mem_total += remote_client_allocated - remote_client_mem;

  sendto_one(cptr, ":%s %d %s :Remote client memory allocated: %d used: %d",
             me.name, RPL_STATSDEBUG, nick,
             remote_client_allocated, remote_client_used);


  count_user_memory(&user_allocated, &user_used);
  mem_total += user_allocated - user_mem;

  sendto_one(cptr, ":%s %d %s :User memory allocated: %d used: %d",
             me.name, RPL_STATSDEBUG, nick,
             user_allocated, user_used);


  count_links_memory(&slink_allocated, &slink_used);
  mem_total += slink_allocated - slink_mem;

  sendto_one(cptr, ":%s %d %s :Links memory allocated: %d used: %d",
             me.name, RPL_STATSDEBUG, nick,
             slink_allocated, slink_used);

#ifdef FLUD
  count_flud_memory(&flud_allocated, &flud_used);
  mem_total += flud_allocated;

  sendto_one(cptr, ":%s %d %s :FLUD memory allocated: %d used: %d",
             me.name, RPL_STATSDEBUG, nick,
             flud_allocated, flud_used);

#endif
  sendto_one(cptr, 
             ":%s %d %s :TOTAL: %d Available:  Current max RSS: %u",
             me.name, RPL_STATSDEBUG, nick, mem_total, get_maxrss());

}
