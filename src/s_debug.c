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

#include <sys/types.h> 
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/file.h>

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include "tools.h"
#include "s_debug.h"
#include "channel.h"
#include "blalloc.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"
#include "fileio.h"
#include "hash.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "res.h"
#include "s_conf.h"
#include "s_log.h"
#include "scache.h"
#include "send.h"
#include "whowas.h"
#include "memory.h"

extern  void    count_ip_hash(int *,u_long *);    /* defined in s_conf.c */

/*
 * Option string.  Must be before #ifdef DEBUGMODE.
 */
const char serveropts[] = {
#ifdef  CMDLINE_CONFIG
  'C',
#endif
#ifdef  DEBUGMODE
  'D',
#endif
  'i',  /* SHOW_INVISIBLE_LUSERS (to opers) - always on */
  'I',  /* NO_DEFAULT_INVISIBLE - always on */
  'M',  /* IDLE_FROM_MSG - only /msg re-sets idle time
         * always on */
#ifdef  CRYPT_OPER_PASSWORD
  'p',
#endif
#ifdef  USE_SYSLOG
  'Y',
#endif
  ' ',
  'T',
  'S',
#ifdef TS_CURRENT
  '0' + TS_CURRENT,
#endif
/* ONLY do TS */
/* ALWAYS do TS_WARNINGS */
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

    log(L_DEBUG, "%s", debugbuf);
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
void send_usage(struct Client *source_p)
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

#ifdef VMS
  sendto_one(source_p, ":%s NOTICE %s :getrusage not supported on this system");
  return;
#else
  if (getrusage(RUSAGE_SELF, &rus) == -1)
    {
      sendto_one(source_p,":%s NOTICE %s :Getruseage error: %s.",
                 me.name, source_p->name, strerror(errno));
      return;
    }
  secs = rus.ru_utime.tv_sec + rus.ru_stime.tv_sec;
  if (0 == secs)
    secs = 1;

  rup = (CurrentTime - me.since) * hzz;
  if (0 == rup)
    rup = 1;


  sendto_one(source_p,
             ":%s %d %s :CPU Secs %d:%d User %d:%d System %d:%d",
             me.name, RPL_STATSDEBUG, source_p->name, (int)(secs/60), (int)(secs%60),
             (int)(rus.ru_utime.tv_sec/60), (int)(rus.ru_utime.tv_sec%60),
             (int)(rus.ru_stime.tv_sec/60), (int)(rus.ru_stime.tv_sec%60));
  sendto_one(source_p, ":%s %d %s :RSS %d ShMem %d Data %d Stack %d",
             me.name, RPL_STATSDEBUG, source_p->name, (int)rus.ru_maxrss,
             (int)(rus.ru_ixrss / rup), (int)(rus.ru_idrss / rup),
             (int)(rus.ru_isrss / rup));
  sendto_one(source_p, ":%s %d %s :Swaps %d Reclaims %d Faults %d",
             me.name, RPL_STATSDEBUG, source_p->name, (int)rus.ru_nswap,
             (int)rus.ru_minflt, (int)rus.ru_majflt);
  sendto_one(source_p, ":%s %d %s :Block in %d out %d",
             me.name, RPL_STATSDEBUG, source_p->name, (int)rus.ru_inblock,
             (int)rus.ru_oublock);
  sendto_one(source_p, ":%s %d %s :Msg Rcv %d Send %d",
             me.name, RPL_STATSDEBUG, source_p->name, (int)rus.ru_msgrcv,
             (int)rus.ru_msgsnd);
  sendto_one(source_p, ":%s %d %s :Signals %d Context Vol. %d Invol %d",
             me.name, RPL_STATSDEBUG, source_p->name, (int)rus.ru_nsignals,
             (int)rus.ru_nvcsw, (int)rus.ru_nivcsw);
#endif /* VMS */
}

void count_memory(struct Client *source_p)
{
  struct Client *target_p;
  struct Channel *chptr;
  struct ConfItem *aconf;
  struct Class *cltmp;
  dlink_node *dlink;

  int lc = 0;           /* local clients */
  int ch = 0;           /* channels */
  int lcc = 0;          /* local client conf links */
  int rc = 0;           /* remote clients */
  int us = 0;           /* user structs */
  int chu = 0;          /* channel users */
  int chi = 0;          /* channel invites */
  int chb = 0;          /* channel bans */
  int wwu = 0;          /* whowas users */
  int cl = 0;           /* classes */
  int co = 0;           /* conf lines */

  int usi = 0;          /* users invited */
  int usc = 0;          /* users in channels */
  int aw = 0;           /* aways set */
  int number_ips_stored;        /* number of ip addresses hashed */
  int number_servers_cached; /* number of servers cached by scache */

  u_long chm = 0;       /* memory used by channels */
  u_long chbm = 0;      /* memory used by channel bans */
  u_long lcm = 0;       /* memory used by local clients */
  u_long rcm = 0;       /* memory used by remote clients */
  u_long awm = 0;       /* memory used by aways */
  u_long wwm = 0;       /* whowas array memory used */
  u_long com = 0;       /* memory used by conf lines */
  u_long rm = 0;        /* res memory used */
  u_long mem_servers_cached; /* memory used by scache */
  u_long mem_ips_stored; /* memory used by ip address hash */

  size_t dbuf_allocated          = 0;
#if 0
  size_t dbuf_used               = 0;
  size_t dbuf_alloc_count        = 0;
  size_t dbuf_used_count         = 0;
#endif

  size_t client_hash_table_size = 0;
  size_t channel_hash_table_size = 0;
  u_long totcl = 0;
  u_long totch = 0;
  u_long totww = 0;

  u_long local_client_memory_used = 0;
  u_long local_client_memory_allocated = 0;

  u_long remote_client_memory_used = 0;
  u_long remote_client_memory_allocated = 0;

  u_long user_memory_used = 0;
  u_long user_memory_allocated = 0;

  u_long links_memory_used = 0;
  u_long links_memory_allocated = 0;

  u_long tot = 0;

  count_whowas_memory(&wwu, &wwm);      /* no more away memory to count */

  for (target_p = GlobalClientList; target_p; target_p = target_p->next)
    {
      if (MyConnect(target_p))
        {
          lc++;
          for (dlink = target_p->localClient->confs.head;
	       dlink; dlink = dlink->next)
            lcc++;
        }
      else
        rc++;
      if (target_p->user)
        {
          us++;
          for (dlink = target_p->user->invited.head; dlink;
               dlink = dlink->next)
            usi++;
          for (dlink = target_p->user->channel.head; dlink;
               dlink = dlink->next)
            usc++;
          if (target_p->user->away)
            {
              aw++;
              awm += (strlen(target_p->user->away)+1);
            }
        }
    }

/* XXX */
#if 0
  lcm = lc * CLIENT_LOCAL_SIZE;
  rcm = rc * CLIENT_REMOTE_SIZE;
#endif

  for (chptr = GlobalChannelList; chptr; chptr = chptr->nextch)
    {
      ch++;
      chm += (strlen(chptr->chname) + sizeof(struct Channel));

      for (dlink = chptr->peons.head; dlink; dlink = dlink->next)
        chu++;
      for (dlink = chptr->chanops.head; dlink; dlink = dlink->next)
        chu++;
      for (dlink = chptr->voiced.head; dlink; dlink = dlink->next)
        chu++;
      for (dlink = chptr->halfops.head; dlink; dlink = dlink->next)
        chu++;

      for (dlink = chptr->invites.head; dlink; dlink = dlink->next)
        chi++;

      /* XXX invex deny except */
      for (dlink = chptr->banlist.head; dlink; dlink = dlink->next)
        {
          chb++;

	  /* XXX */
#if 0
          chbm += (strlen(dlink->data)+1+sizeof(dlink_node));
          if (dlink->value.banptr->banstr)
            chbm += strlen(dlink->value.banptr->banstr);
          if (dlink->value.banptr->who)
            chbm += strlen(dlink->value.banptr->who);
#endif
        }
    }

  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    {
      co++;
      com += aconf->host ? strlen(aconf->host)+1 : 0;
      com += aconf->passwd ? strlen(aconf->passwd)+1 : 0;
      com += aconf->name ? strlen(aconf->name)+1 : 0;
      com += sizeof(struct ConfItem);
    }

  for (cltmp = ClassList; cltmp; cltmp = cltmp->next)
    cl++;

  /*
   * need to set dbuf_count here because we use a dbuf when we send
   * the results. since sending the results results in a dbuf being used,
   * the count would be wrong if we just used the globals
   */

#if 0
  count_dbuf_memory(&dbuf_allocated, &dbuf_used);
  dbuf_alloc_count = INITIAL_DBUFS + DBufAllocCount;
  dbuf_used_count  = DBufUsedCount;
#endif

  sendto_one(source_p, ":%s %d %s :Client Local %u(%d) Remote %u(%d)",
             me.name, RPL_STATSDEBUG, source_p->name, lc, (int)lcm, rc, (int)rcm);
  sendto_one(source_p, ":%s %d %s :Users %u(%u) Invites %u(%u)",
             me.name, RPL_STATSDEBUG, source_p->name,
	     us, us*sizeof(struct User), usi,
             usi * sizeof(dlink_node));
  sendto_one(source_p, ":%s %d %s :User channels %u(%u) Aways %u(%d)",
             me.name, RPL_STATSDEBUG, source_p->name, usc, usc*sizeof(dlink_node),
             aw, (int)awm);
  sendto_one(source_p, ":%s %d %s :Attached confs %u(%u)",
             me.name, RPL_STATSDEBUG, source_p->name, lcc, lcc*sizeof(dlink_node));

  totcl = lcm + rcm + us*sizeof(struct User) + usc*sizeof(dlink_node) + awm;
  totcl += lcc*sizeof(dlink_node) + usi*sizeof(dlink_node);

  sendto_one(source_p, ":%s %d %s :Conflines %u(%d)",
             me.name, RPL_STATSDEBUG, source_p->name, co, (int)com);

  sendto_one(source_p, ":%s %d %s :Classes %u(%u)",
             me.name, RPL_STATSDEBUG, source_p->name, cl, cl*sizeof(struct Class));

  sendto_one(source_p, ":%s %d %s :Channels %u(%d) Bans %u(%d)",
             me.name, RPL_STATSDEBUG, source_p->name, ch, (int)chm, chb, (int)chbm);
  sendto_one(source_p, ":%s %d %s :Channel members %u(%u) invite %u(%u)",
             me.name, RPL_STATSDEBUG, source_p->name, chu, chu*sizeof(dlink_node),
             chi, chi*sizeof(dlink_node));

  totch = chm + chbm + chu*sizeof(dlink_node) + chi*sizeof(dlink_node);

  sendto_one(source_p, ":%s %d %s :Whowas users %u(%u)",
             me.name, RPL_STATSDEBUG, source_p->name,
	     wwu, wwu*sizeof(struct User));

  sendto_one(source_p, ":%s %d %s :Whowas array %u(%d)",
             me.name, RPL_STATSDEBUG, source_p->name, NICKNAMEHISTORYLENGTH, (int)wwm);

  totww = wwu * sizeof(struct User) + wwm;

  client_hash_table_size  = hash_get_client_table_size();
  channel_hash_table_size = hash_get_channel_table_size();

  sendto_one(source_p, ":%s %d %s :Hash: client %u(%u) chan %u(%u)",
             me.name, RPL_STATSDEBUG, source_p->name,
             U_MAX, client_hash_table_size,
             CH_MAX, channel_hash_table_size);

#if 0
  sendto_one(source_p, ":%s %d %s :Dbuf blocks allocated %d(%d), used %d(%d) max allocated by malloc() %d",
             me.name, RPL_STATSDEBUG, source_p->name,
	     dbuf_alloc_count, dbuf_allocated,
             dbuf_used_count, dbuf_used, DBufMaxAllocated );
#endif

#if 0
  rm = cres_mem(source_p);
#endif
  count_scache(&number_servers_cached,&mem_servers_cached);

  sendto_one(source_p, ":%s %d %s :scache %u(%d)",
             me.name, RPL_STATSDEBUG, source_p->name,
             number_servers_cached,
             (int)mem_servers_cached);

  count_ip_hash(&number_ips_stored,&mem_ips_stored);
  sendto_one(source_p, ":%s %d %s :iphash %u(%d)",
             me.name, RPL_STATSDEBUG, source_p->name,
             number_ips_stored,
             (int)mem_ips_stored);

  tot = totww + totch + totcl + com + cl*sizeof(struct Class) +
    dbuf_allocated + rm;
  tot += client_hash_table_size;
  tot += channel_hash_table_size;

  tot += mem_servers_cached;
  sendto_one(source_p, ":%s %d %s :Total: ww %d ch %d cl %d co %d db %u",
             me.name, RPL_STATSDEBUG, source_p->name, (int)totww, (int)totch,
             (int)totcl, (int)com, dbuf_allocated);


  count_local_client_memory((int *)&local_client_memory_used,
                            (int *)&local_client_memory_allocated);
  tot += local_client_memory_allocated;
  sendto_one(source_p, ":%s %d %s :Local client Memory in use: %d Local "
                   "client Memory allocated: %d",
             me.name, RPL_STATSDEBUG, source_p->name,
             (int)local_client_memory_used, (int)local_client_memory_allocated);


  count_remote_client_memory( (int *)&remote_client_memory_used,
                              (int *)&remote_client_memory_allocated);
  tot += remote_client_memory_allocated;
  sendto_one(source_p, ":%s %d %s :Remote client Memory in use: %d Remote "
                   "client Memory allocated: %d",
             me.name, RPL_STATSDEBUG, source_p->name,
             (int)remote_client_memory_used, (int)remote_client_memory_allocated);


  count_user_memory( (int *)&user_memory_used,
                    (int *)&user_memory_allocated);
  tot += user_memory_allocated;
  sendto_one(source_p, ":%s %d %s :struct User Memory in use: %d struct User "
                   "Memory allocated: %d",
             me.name, RPL_STATSDEBUG, source_p->name,
             (int)user_memory_used,
             (int)user_memory_allocated);


  count_links_memory( (int *)&links_memory_used,
		      (int *)&links_memory_allocated);
  sendto_one(source_p, ":%s %d %s :Links Memory in use: %d Links Memory "
                   "allocated: %d",
             me.name, RPL_STATSDEBUG, source_p->name,
             (int)links_memory_used,
             (int)links_memory_allocated);

  sendto_one(source_p, 
             ":%s %d %s :TOTAL: %d Available:  Current max RSS: %u",
             me.name, RPL_STATSDEBUG, source_p->name, (int)tot, get_maxrss());

}
