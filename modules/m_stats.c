/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_stats.c
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
 *  $Id$
 */
#include "tools.h"	 /* dlink_node/dlink_list */
#include "handlers.h"    /* m_pass prototype */
#include "class.h"       /* report_classes */
#include "client.h"      /* Client */
#include "common.h"      /* TRUE/FALSE */
#include "dline_conf.h"  /* report_dlines */
#include "irc_string.h"  /* strncpy_irc */
#include "ircd.h"        /* me */
#include "listener.h"    /* show_ports */
#include "ircd_handler.h"
#include "msg.h"         /* Message */
#include "mtrie_conf.h"  /* report_mtrie_conf_links */
#include "s_gline.h"     /* report_glines */
#include "numeric.h"     /* ERR_xxx */
#include "scache.h"      /* list_scache */
#include "send.h"        /* sendto_one */
#include "fdlist.h"      /* PF and friends */
#include "s_bsd.h"       /* highest_fd */
#include "s_conf.h"      /* ConfItem, report_configured_links */
#include "s_debug.h"     /* send_usage */
#include "s_misc.h"      /* serv_info */
#include "s_serv.h"      /* hunt_server, show_servers */
#include "s_stats.h"     /* tstats */
#include "s_user.h"      /* show_opers */
#include "event.h"	 /* events */
#include "msg.h"
#include "linebuf.h"

#include <string.h>

struct Message stats_msgtab = {
  MSG_STATS, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_stats, ms_stats, mo_stats}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_STATS, &stats_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_STATS);
}

char *_version = "20001122";

/*
 * m_stats - STATS message handler
 *      parv[0] = sender prefix
 *      parv[1] = statistics selector (defaults to Message frequency)
 *      parv[2] = server name (current server defaulted, if omitted)
 *
 *      Currently supported are:
 *              M = Message frequency (the old stat behaviour)
 *              L = Local Link statistics
 *              C = Report C and N configuration lines
 *
 *
 * m_stats/stats_conf
 *    Report N/C-configuration lines from this server. This could
 *    report other configuration lines too, but converting the
 *    status back to "char" is a bit akward--not worth the code
 *    it needs...
 *
 *    Note:   The info is reported in the order the server uses
 *            it--not reversed as in ircd.conf!
 */

const char* Lformat = ":%s %d %s %s %u %u %u %u %u :%u %u %s";

char *parse_stats_args(int parc,char *parv[],int *doall,int *wilds);

void stats_L(struct Client *sptr,char *name,int doall, int wilds);
void stats_L_list(struct Client *sptr,char *name, int doall, int wilds,
		  dlink_list *list);
void stats_spy(struct Client *sptr,char stat);
void stats_L_spy(struct Client *sptr, char stat, char *name);
void stats_p_spy(struct Client *sptr);
void do_normal_stats(struct Client *sptr, char *name, char *target,
			    char stat, int doall, int wilds);
void do_non_priv_stats(struct Client *sptr, char *name, char *target,
			      char stat, int doall, int wilds);
void do_priv_stats(struct Client *sptr, char *name, char *target,
			  char stat, int doall, int wilds);

int m_stats(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char            stat = parc > 1 ? parv[1][0] : '\0';
  int             doall = 0;
  int             wilds = 0;
  char            *name=NULL;
  char            *target=NULL;
  static time_t   last_used = 0;

  if((last_used + ConfigFileEntry.pace_wait) > CurrentTime)
    {
      /* safe enough to give this on a local connect only */
      if(MyClient(sptr))
	sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }
  else
    {
      last_used = CurrentTime;
    }

  if (hunt_server(cptr,sptr,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return 0;

  name = parse_stats_args(parc,parv,&doall,&wilds);

  if (parc > 3)
    target = parv[3];

  do_normal_stats(sptr, name, target, stat, doall, wilds);
  do_non_priv_stats(sptr, name, target, stat, doall, wilds);
  sendto_one(sptr, form_str(RPL_ENDOFSTATS), me.name, parv[0], stat);

  return 0;
}

/*
 * mo_stats - STATS message handler
 *      parv[0] = sender prefix
 *      parv[1] = statistics selector (defaults to Message frequency)
 *      parv[2] = server name (current server defaulted, if omitted)
 *
 *      Currently supported are:
 *              M = Message frequency (the old stat behaviour)
 *              L = Local Link statistics
 *              C = Report C and N configuration lines
 *
 *
 * m_stats/stats_conf
 *    Report N/C-configuration lines from this server. This could
 *    report other configuration lines too, but converting the
 *    status back to "char" is a bit akward--not worth the code
 *    it needs...
 *
 *    Note:   The info is reported in the order the server uses
 *            it--not reversed as in ircd.conf!
 */


int mo_stats(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char            stat = parc > 1 ? parv[1][0] : '\0';
  int             doall = 0;
  int             wilds = 0;
  char            *name=NULL;
  char            *target=NULL;

  if (hunt_server(cptr,sptr,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return 0;

  name = parse_stats_args(parc,parv,&doall,&wilds);

  if (parc > 3)
    target = parv[3];
  
  do_normal_stats(sptr, name, target, stat, doall, wilds);
  do_priv_stats(sptr, name, target, stat, doall, wilds);
  sendto_one(sptr, form_str(RPL_ENDOFSTATS), me.name, parv[0], stat);

  return 0;
}


/*
 * ms_stats - STATS message handler
 *      parv[0] = sender prefix
 *      parv[1] = statistics selector (defaults to Message frequency)
 *      parv[2] = server name (current server defaulted, if omitted)
 *
 *      Currently supported are:
 *              M = Message frequency (the old stat behaviour)
 *              L = Local Link statistics
 *              C = Report C and N configuration lines
 *
 *
 * m_stats/stats_conf
 *    Report N/C-configuration lines from this server. This could
 *    report other configuration lines too, but converting the
 *    status back to "char" is a bit akward--not worth the code
 *    it needs...
 *
 *    Note:   The info is reported in the order the server uses
 *            it--not reversed as in ircd.conf!
 */

int ms_stats(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  if (hunt_server(cptr,sptr,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return 0;

  if (IsAnyOper(sptr))
    mo_stats(cptr,sptr,parc,parv);
  else
    m_stats(cptr,sptr,parc,parv);

  return 0;
}

/*
 * do_normal_stats
 *
 * inputs	- source pointer to client
 *		- name for stats L
 *		- target pointer
 *		- stat char
 *		- doall
 *		- wilds or not
 * output	- NONE
 * side effects - stats that either opers or non opers can see
 */
void do_normal_stats(struct Client *sptr,
			    char *name, char *target,
			    char stat, int doall, int wilds)
{
  switch (stat)
    {
    case 'L' : case 'l' :
      stats_L(sptr,name,doall,wilds);
      stats_L_spy(sptr,stat,name);
      break;

    case 'u' :
      {
        time_t now;
        
        now = CurrentTime - me.since;
        sendto_one(sptr, form_str(RPL_STATSUPTIME), me.name, sptr->name,
                   now/86400, (now/3600)%24, (now/60)%60, now%60);
        sendto_one(sptr, form_str(RPL_STATSCONN), me.name, sptr->name,
                   MaxConnectionCount, MaxClientCount);
	stats_spy(sptr,stat);
        break;
      }
    default :
      break;
    }
}

/*
 * do_non_priv_stats
 *
 * inputs	- source pointer to client
 *		- name for stats L
 *		- target pointer
 *		- stat char
 *		- doall
 *		- wilds or not
 * output	- NONE
 * side effects - only stats that are allowed for non-opers etc. are done here
 */
void do_non_priv_stats(struct Client *sptr, char *name, char *target,
			      char stat, int doall, int wilds)
{
  switch (stat)
    {
    case 'E' : case 'e' :
    case 'F' : case 'f' :
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, sptr->name);
      stats_spy(sptr,stat);
      break;

/* ZZZ opers only flag */
    case 'H' : case 'h' :
      report_configured_links(sptr, CONF_HUB|CONF_LEAF);
      stats_spy(sptr,stat);
      break;

    case 'I' : case 'i' :
#ifdef I_LINES_OPER_ONLY
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, sptr->name);
#else
      report_mtrie_conf_links(sptr, CONF_CLIENT);
#endif
      stats_spy(sptr,stat);
      break;

    case 'K' :
      if(target != (char *)NULL)
        report_matching_host_klines(sptr,target);
      else
	report_matching_host_klines(sptr,sptr->host);
      stats_spy(sptr,stat);
      break;

    case 'o' : case 'O' :
      report_configured_links(sptr, CONF_OPS);
      stats_spy(sptr,stat);
      break;

    case 'P' :
      show_ports(sptr);
      stats_spy(sptr,stat);
      break;

    case 'p' :
      if (ConfigFileEntry.stats_p_notice)
	{
	  stats_p_spy(sptr);
	}
      else
	{
	  show_opers(sptr);
	  stats_spy(sptr,stat);
	}
      break;

    case 'v' : case 'V' :
      show_servers(sptr);
      stats_spy(sptr,stat);
      break;

    case '?':
      serv_info(sptr);
      stats_spy(sptr,stat);
      break;


    case 'D' : case 'd' :
    case 'S' : case 's' :
    case 'T' : case 't' :
    case 'Z' : case 'z' :
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, sptr->name);
      stats_spy(sptr,stat);
      break;
    }
}

/*
 * do_priv_stats
 *
 * inputs	- source pointer to client
 *		- name for stats L
 *		- target pointer
 *		- stat char
 * output	- NONE
 * side effects - only stats that are allowed for opers etc. are done here
 */
void do_priv_stats(struct Client *sptr, char *name, char *target,
			    char stat, int doall, int wilds)
{
  struct Message* mptr;

  switch (stat)
    {
    case 'C' : case 'c' :
      report_configured_links(sptr, CONF_CONNECT_SERVER|CONF_NOCONNECT_SERVER);
      stats_spy(sptr,stat);
      break;
 
    case 'D': case 'd':
      report_dlines(sptr);
      stats_spy(sptr,stat);
      break;

    case 'E' : case 'e' :
      show_events(sptr);
      stats_spy(sptr,stat);
      break;

    case 'F' : case 'f' :
      fd_dump(sptr);
      stats_spy(sptr,stat);
      break;

    case 'G': case 'g' :
      if (ConfigFileEntry.glines)
	{
	  report_glines(sptr);
	  stats_spy(sptr,stat);
	}
      else
        sendto_one(sptr,":%s NOTICE %s :This server does not support G lines",
                   me.name, sptr->name);
      break;

    case 'H' : case 'h' :
      report_configured_links(sptr, CONF_HUB|CONF_LEAF);
      stats_spy(sptr,stat);
      break;

    case 'I' : case 'i' :
      report_mtrie_conf_links(sptr, CONF_CLIENT);
      stats_spy(sptr,stat);
      break;

    case 'K' :
      report_mtrie_conf_links(sptr, CONF_KILL);
      stats_spy(sptr,stat);
      break;

    case 'k' :
      report_temp_klines(sptr);
      stats_spy(sptr,stat);
      break;

    case 'M' : case 'm' :
      for (mptr = msgtab; mptr->cmd; mptr++)
          sendto_one(sptr, form_str(RPL_STATSCOMMANDS),
                     me.name, sptr->name, mptr->cmd,
                     mptr->count, mptr->bytes);
      stats_spy(sptr,stat);
      break;

    case 'o' : case 'O' :
      report_configured_links(sptr, CONF_OPS);
      stats_spy(sptr,stat);
      break;

    case 'P' :
      show_ports(sptr);
      stats_spy(sptr,stat);
      break;

    case 'Q' : case 'q' :
      report_qlines(sptr);
      stats_spy(sptr,stat);
      break;

    case 'R' : case 'r' :
      send_usage(sptr);
      stats_spy(sptr,stat);
      break;

    case 'S' : case 's':
      list_scache(sptr);
      stats_spy(sptr,stat);
      break;

    case 'T' : case 't' :
      tstats(sptr);
      stats_spy(sptr,stat);
      break;

    case 'U' :
      report_specials(sptr,CONF_ULINE,RPL_STATSULINE);
      stats_spy(sptr,stat);
      break;

    case 'p' :
      if (ConfigFileEntry.stats_p_notice)
	{
	  stats_p_spy(sptr);
	}
      else
	{
	  show_opers(sptr);
	  stats_spy(sptr,stat);
	}
      break;

    case 'v' : case 'V' :
      show_servers(sptr);
      stats_spy(sptr,stat);
      break;

    case 'X' :
      report_specials(sptr,CONF_XLINE,RPL_STATSXLINE);
      stats_spy(sptr,stat);
      break;

    case 'Y' : case 'y' :
      report_classes(sptr);
      stats_spy(sptr,stat);
      break;

    case 'Z' : case 'z' :
      count_memory(sptr);
      stats_spy(sptr,stat);
      break;

    case '?':
      serv_info(sptr);
      stats_spy(sptr,stat);
      break;

    }
}

/*
 * stats_L
 *
 * inputs	- pointer to client to report to
 *		- doall flag
 *		- wild card or not
 * output	- NONE
 * side effects	-
 */
void stats_L(struct Client *sptr,char *name,int doall, int wilds)
{
  struct Client *acptr;

  stats_L_list(sptr, name, doall, wilds, &unknown_list);
  stats_L_list(sptr, name, doall, wilds, &lclient_list);
  stats_L_list(sptr, name, doall, wilds, &oper_list);
  stats_L_list(sptr, name, doall, wilds, &serv_list);
}

void stats_L_list(struct Client *sptr,char *name, int doall, int wilds,
		  dlink_list *list)
{
  dlink_node *ptr;
  struct Client *acptr;

  /*
   * send info about connections which match, or all if the
   * mask matches me.name.  Only restrictions are on those who
   * are invisible not being visible to 'foreigners' who use
   * a wild card based search to list it.
   */
  for(ptr = list->head;ptr;ptr = ptr->next)
    {
      acptr = ptr->data;

      if (IsPerson(acptr) &&
	  !IsAnyOper(acptr) && !IsAnyOper(sptr) &&
	  (acptr != sptr))
	continue;
      if (IsInvisible(acptr) && (doall || wilds) &&
	  !(MyConnect(sptr) && IsGlobalOper(sptr)) &&
	  !IsAnyOper(acptr) && (acptr != sptr))
	continue;
      if (!doall && wilds && !match(name, acptr->name))
	continue;
      if (!(doall || wilds) && irccmp(name, acptr->name))
	continue;

      /* I've added a sanity test to the "CurrentTime - acptr->since"
       * occasionally, acptr->since is larger than CurrentTime.
       * The code in parse.c "randomly" increases the "since",
       * which means acptr->since is larger then CurrentTime at times,
       * this gives us very high odd number.. 
       * So, I am going to return 0 for ->since if this happens.
       * - Dianora
       */
      /* trust opers not on this server */
      /* if(IsAnyOper(sptr)) */

      /* Don't trust opers not on this server */
      if(MyClient(sptr) && IsAnyOper(sptr))
	{
	  sendto_one(sptr, Lformat, me.name,
                     RPL_STATSLINKINFO, sptr->name,
                     (IsUpper(stat)) ?
                     get_client_name(acptr, TRUE) :
                     get_client_name(acptr, FALSE),
                     (int)linebuf_len(&acptr->localClient->buf_sendq),
                     (int)acptr->localClient->sendM,
		     (int)acptr->localClient->sendK,
                     (int)acptr->localClient->receiveM,
		     (int)acptr->localClient->receiveK,
                     CurrentTime - acptr->firsttime,
                     (CurrentTime > acptr->since) ? (CurrentTime - acptr->since):0,
                     IsServer(acptr) ? show_capabilities(acptr) : "-");
	}
      else
	{
	  if(IsIPHidden(acptr) || IsServer(acptr))
	    sendto_one(sptr, Lformat, me.name,
		       RPL_STATSLINKINFO, sptr->name,
		       get_client_name(acptr, HIDEME),
		       (int)linebuf_len(&acptr->localClient->buf_sendq),
		       (int)acptr->localClient->sendM,
		       (int)acptr->localClient->sendK,
		       (int)acptr->localClient->receiveM,
		       (int)acptr->localClient->receiveK,
		       CurrentTime - acptr->firsttime,
		       (CurrentTime > acptr->since) ? (CurrentTime - acptr->since):0,
		       IsServer(acptr) ? show_capabilities(acptr) : "-");
	  else
	    sendto_one(sptr, Lformat, me.name,
		       RPL_STATSLINKINFO, sptr->name,
		       (IsUpper(stat)) ?
		       get_client_name(acptr, TRUE) :
		       get_client_name(acptr, FALSE),
		       (int)linebuf_len(&acptr->localClient->buf_sendq),
		       (int)acptr->localClient->sendM,
		       (int)acptr->localClient->sendK,
		       (int)acptr->localClient->receiveM,
		       (int)acptr->localClient->receiveK,
		       CurrentTime - acptr->firsttime,
		       (CurrentTime > acptr->since) ? (CurrentTime - acptr->since):0,
		       IsServer(acptr) ? show_capabilities(acptr) : "-");
	}
    }
}

/*
 * stats_spy
 *
 * inputs	- pointer to client doing the /stats
 *		- char letter they are doing /stats on
 * output	- none
 * side effects -
 * This little helper function reports to opers if configured.
 * personally, I don't see why opers need to see stats requests
 * at all. They are just "noise" to an oper, and users can't do
 * any damage with stats requests now anyway. So, why show them?
 * -Dianora
 *
 * so they can see stats p requests .. should probably add an
 * option so only stats p is shown..  --is
 *
 * done --is
 */
void stats_spy(struct Client *sptr,char stat)
{
  if (ConfigFileEntry.stats_notice)
    {
      sendto_realops_flags(FLAGS_SPY,
			   "STATS %c requested by %s (%s@%s) [%s]",
			   stat,
			   sptr->name,
			   sptr->username,
			   sptr->host,
			   sptr->user->server );
    }
}

/* 
 * stats_L_spy
 * 
 * inputs	- pointer to sptr, client doing stats L
 *		- stat that they are doing 'L' 'l'
 * 		- any name argument they have given
 * output	- NONE
 * side effects	- a notice is sent to opers, IF spy mode is configured
 * 		  in the conf file.
 */
void stats_L_spy(struct Client *sptr, char stat, char *name)
{
  if (ConfigFileEntry.stats_notice)
    {
      if(name != NULL)
	sendto_realops_flags(FLAGS_SPY,
			     "STATS %c requested by %s (%s@%s) [%s] on %s",
			     stat,
			     sptr->name,
			     sptr->username,
			     sptr->host,
			     sptr->user->server,
			     name );
      else
	sendto_realops_flags(FLAGS_SPY,
			     "STATS %c requested by %s (%s@%s) [%s]",
			     stat,
			     sptr->name,
			     sptr->username,

			     sptr->host,
			     sptr->user->server);
	
    }
}

/* 
 * stats_p_spy
 * 
 * inputs	- pointer to sptr, client doing stats p
 * output	- NONE
 * side effects	- a notice is sent to opers, IF spy mode is configured
 * 		  in the conf file.
 */
void stats_p_spy(struct Client *sptr)
{
  sendto_realops_flags(FLAGS_SPY,
		       "STATS p requested by %s (%s@%s) [%s]",
		       sptr->name, sptr->username, sptr->host,
		       sptr->user->server);
}

/*
 * parse_stats_args
 *
 * inputs	- arg count
 *		- args
 *		- doall flag
 *		- wild card or not
 * output	- pointer to name to use
 * side effects	-
 * common parse routine for m_stats args
 * 
 */
char *parse_stats_args(int parc,char *parv[],int *doall,int *wilds)
{
  char *name;

  if(parc > 2)
    {
      name = parv[2];
      if (!irccmp(name, me.name))
        *doall = 2;
      else if (match(name, me.name))
        *doall = 1;
      if (strchr(name, '*') || strchr(name, '?'))
        *wilds = 1;
    }
  else
    name = me.name;

  return(name);
}
