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
#include "hostmask.h"  /* report_mtrie_conf_links */
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
#include "linebuf.h"
#include "parse.h"
#include "modules.h"
#include "hook.h"

#include <string.h>

static void m_stats(struct Client*, struct Client*, int, char**);
static void ms_stats(struct Client*, struct Client*, int, char**);
static void mo_stats(struct Client*, struct Client*, int, char**);

struct Message stats_msgtab = {
  "STATS", 0, 1, 0, MFLG_SLOW, 0,
  {m_unregistered, m_stats, ms_stats, mo_stats}
};

void
_modinit(void)
{
  hook_add_event("doing_stats");
  mod_add_cmd(&stats_msgtab);
}

void
_moddeinit(void)
{
  hook_del_event("doing_stats");
  mod_del_cmd(&stats_msgtab);
}

char *_version = "20010127";

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

static char *parse_stats_args(int, char **, int *, int *);

static void stats_L(struct Client *, char *, int, int, char);
static void stats_L_list(struct Client *s, char *, int, int,
                         dlink_list *, char);
static void stats_spy(struct Client *, char);
static void stats_L_spy(struct Client *, char, char *);
static void do_normal_stats(struct Client *, char *, char *, char, int, int);
static void do_non_priv_stats(struct Client *, char *, char *, char, int, int);
static void do_priv_stats(struct Client *, char *, char *, char, int, int);

static void m_stats(struct Client *client_p, struct Client *source_p,
                   int parc, char *parv[])
{
  char            statchar = parc > 1 ? parv[1][0] : '\0';
  int             doall = 0;
  int             wilds = 0;
  char            *name=NULL;
  char            *target=NULL;
  static time_t   last_used = 0;

  if((last_used + ConfigFileEntry.pace_wait) > CurrentTime)
    {
      /* safe enough to give this on a local connect only */
      if(MyClient(source_p))
	sendto_one(source_p,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return;
    }
  else
    {
      last_used = CurrentTime;
    }

  if (hunt_server(client_p,source_p,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return;

  name = parse_stats_args(parc,parv,&doall,&wilds);

  if (parc > 3)
    target = parv[3];

  do_normal_stats(source_p, name, target, statchar, doall, wilds);
  do_non_priv_stats(source_p, name, target, statchar, doall, wilds);
  sendto_one(source_p, form_str(RPL_ENDOFSTATS), me.name, parv[0], statchar);
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

static void mo_stats(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  char            statchar = parc > 1 ? parv[1][0] : '\0';
  int             doall = 0;
  int             wilds = 0;
  char            *name=NULL;
  char            *target=NULL;

  if (hunt_server(client_p,source_p,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return;

  name = parse_stats_args(parc,parv,&doall,&wilds);

  if (parc > 3)
    target = parv[3];
  
  do_normal_stats(source_p, name, target, statchar, doall, wilds);
  do_priv_stats(source_p, name, target, statchar, doall, wilds);
  sendto_one(source_p, form_str(RPL_ENDOFSTATS), me.name, parv[0], statchar);
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

static void ms_stats(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  if (hunt_server(client_p,source_p,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return;

  if (IsOper(source_p))
    mo_stats(client_p,source_p,parc,parv);
  else
    m_stats(client_p,source_p,parc,parv);
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
static void do_normal_stats(struct Client *source_p,
			    char *name, char *target,
			    char statchar, int doall, int wilds)
{
  switch (statchar)
    {
    case 'L' : case 'l' :
      stats_L(source_p,name,doall,wilds,statchar);
      stats_L_spy(source_p,statchar,name);
      break;

    case 'u' :
      {
        time_t now;
        
        now = CurrentTime - me.since;
        sendto_one(source_p, form_str(RPL_STATSUPTIME), me.name, source_p->name,
                   now/86400, (now/3600)%24, (now/60)%60, now%60);
        if(!GlobalSetOptions.hide_server || IsOper(source_p))
          sendto_one(source_p, form_str(RPL_STATSCONN), me.name, source_p->name,
                     MaxConnectionCount, MaxClientCount, Count.totalrestartcount);
	stats_spy(source_p,statchar);
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
static void do_non_priv_stats(struct Client *source_p, char *name, char *target,
			      char statchar, int doall, int wilds)
{
  switch (statchar)
    {
    case 'K' :
      stats_spy(source_p,statchar);
      break;

    case 'o' : case 'O' :
      if (ConfigFileEntry.o_lines_oper_only)
	      sendto_one(source_p, form_str(ERR_NOPRIVILEGES),me.name,source_p->name);
      else
	  report_configured_links(source_p, CONF_OPERATOR);
      stats_spy(source_p,statchar);
      break;

    case 'p' :
      /* showing users the oper list cant hurt.. its better
       * than the alternatives of noticing the opers which could
       * get really annoying --fl
       */
      stats_spy(source_p,statchar);
      show_opers(source_p);
      break;

    case 'U' :
    case 'Q' : case 'q' :
    case 'C' : case 'c' :
    case 'H' : case 'h' :
    case 'E' : case 'e' :
    case 'F' : case 'f' :
    case 'I' : case 'i' :
    case 'M' : case 'm' :
    case 'R' : case 'r' :
    case 'X' : case 'x' :
    case 'Y' : case 'y' :
    case 'V' : case 'v' :
    case 'P' : case '?' :
    case 'G' : case 'g' :
    case 'D' : case 'd' :
    case 'S' : case 's' :
    case 'T' : case 't' :
    case 'Z' : case 'z' :
      sendto_one(source_p, form_str(ERR_NOPRIVILEGES), me.name, source_p->name);
      stats_spy(source_p,statchar);
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
static void do_priv_stats(struct Client *source_p, char *name, char *target,
			    char statchar, int doall, int wilds)
{ 
 switch (statchar)
    {
    case 'A' : case 'a' :
      report_adns_servers(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'C' : case 'c' :
      report_configured_links(source_p, CONF_SERVER);
      stats_spy(source_p,statchar);
      break;
 
    case 'D': case 'd':
      report_dlines(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'E' : case 'e' :
      show_events(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'F' : case 'f' :
      fd_dump(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'G': case 'g' :
      if (ConfigFileEntry.glines)
	{
	  report_glines(source_p);
	  stats_spy(source_p,statchar);
	}
      else
        sendto_one(source_p,":%s NOTICE %s :This server does not support G lines",
                   me.name, source_p->name);
      break;

    case 'H' : case 'h' :
      report_configured_links(source_p, CONF_HUB|CONF_LEAF);
      stats_spy(source_p,statchar);
      break;

    case 'I' : case 'i' :
      report_hostmask_conf_links(source_p, CONF_CLIENT);
      stats_spy(source_p,statchar);
      break;

    case 'K' :
      report_hostmask_conf_links(source_p, CONF_KILL);
      stats_spy(source_p,statchar);
      break;

    case 'k' :
      report_temp_klines(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'M' : case 'm' :
      report_messages(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'o' : case 'O' :
      report_configured_links(source_p, CONF_OPERATOR);
      stats_spy(source_p,statchar);
      break;

    case 'P' :
      show_ports(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'Q' : case 'q' :
      report_qlines(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'R' : case 'r' :
      send_usage(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'S' : case 's':
      list_scache(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'T' : case 't' :
      tstats(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'U' :
      report_specials(source_p,CONF_ULINE,RPL_STATSULINE);
      stats_spy(source_p,statchar);
      break;

    case 'p' :
      stats_spy(source_p,statchar);
      show_opers(source_p);
      break;

    case 'v' : case 'V' :
      show_servers(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'X' : case 'x' :
      report_specials(source_p,CONF_XLINE,RPL_STATSXLINE);
      stats_spy(source_p,statchar);
      break;

    case 'Y' : case 'y' :
      report_classes(source_p);
      stats_spy(source_p,statchar);
      break;

    case 'Z' : case 'z' :
      count_memory(source_p);
      stats_spy(source_p,statchar);
      break;

    case '?':
      serv_info(source_p);
      stats_spy(source_p,statchar);
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
static void stats_L(struct Client *source_p,char *name,int doall,
                    int wilds,char statchar)
{
  stats_L_list(source_p, name, doall, wilds, &unknown_list, statchar);
  stats_L_list(source_p, name, doall, wilds, &lclient_list, statchar);
  stats_L_list(source_p, name, doall, wilds, &serv_list, statchar);
}

static void stats_L_list(struct Client *source_p,char *name, int doall, int wilds,
                         dlink_list *list,char statchar)
{
  dlink_node *ptr;
  struct Client *target_p;

  /*
   * send info about connections which match, or all if the
   * mask matches me.name.  Only restrictions are on those who
   * are invisible not being visible to 'foreigners' who use
   * a wild card based search to list it.
   */
  for(ptr = list->head;ptr;ptr = ptr->next)
    {
      target_p = ptr->data;

      if (IsInvisible(target_p) && (doall || wilds) &&
	  !(MyConnect(source_p) && IsOper(source_p)) &&
	  !IsOper(target_p) && (target_p != source_p))
	continue;
      if (!doall && wilds && !match(name, target_p->name))
	continue;
      if (!(doall || wilds) && irccmp(name, target_p->name))
	continue;

      if(MyClient(source_p) && IsOper(source_p))
	{
	  sendto_one(source_p, Lformat, me.name,
                     RPL_STATSLINKINFO, source_p->name,
                     (IsUpper(statchar)) ?
                     get_client_name(target_p, SHOW_IP) :
                     get_client_name(target_p, HIDE_IP),
                     (int)linebuf_len(&target_p->localClient->buf_sendq),
                     (int)target_p->localClient->sendM,
		     (int)target_p->localClient->sendK,
                     (int)target_p->localClient->receiveM,
		     (int)target_p->localClient->receiveK,
                     CurrentTime - target_p->firsttime,
                     (CurrentTime > target_p->since) ? (CurrentTime - target_p->since):0,
                     IsServer(target_p) ? show_capabilities(target_p) : "-");
	}
      else
	{
	  if(IsIPHidden(target_p) || IsServer(target_p))
	    sendto_one(source_p, Lformat, me.name,
		       RPL_STATSLINKINFO, source_p->name,
		       get_client_name(target_p, MASK_IP),
		       (int)linebuf_len(&target_p->localClient->buf_sendq),
		       (int)target_p->localClient->sendM,
		       (int)target_p->localClient->sendK,
		       (int)target_p->localClient->receiveM,
		       (int)target_p->localClient->receiveK,
		       CurrentTime - target_p->firsttime,
		       (CurrentTime > target_p->since) ? (CurrentTime - target_p->since):0,
		       IsServer(target_p) ? show_capabilities(target_p) : "-");
	  else
	    sendto_one(source_p, Lformat, me.name,
		       RPL_STATSLINKINFO, source_p->name,
		       (IsUpper(statchar)) ?
		       get_client_name(target_p, SHOW_IP) :
		       get_client_name(target_p, HIDE_IP),
		       (int)linebuf_len(&target_p->localClient->buf_sendq),
		       (int)target_p->localClient->sendM,
		       (int)target_p->localClient->sendK,
		       (int)target_p->localClient->receiveM,
		       (int)target_p->localClient->receiveK,
		       CurrentTime - target_p->firsttime,
		       (CurrentTime > target_p->since) ? (CurrentTime - target_p->since):0,
		       IsServer(target_p) ? show_capabilities(target_p) : "-");
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
static void stats_spy(struct Client *source_p,char statchar)
{
  struct hook_stats_data data;

  data.source_p = source_p;
  data.statchar = statchar;
  data.name = NULL;

  hook_call_event("doing_stats", &data);
}

/* 
 * stats_L_spy
 * 
 * inputs	- pointer to source_p, client doing stats L
 *		- stat that they are doing 'L' 'l'
 * 		- any name argument they have given
 * output	- NONE
 * side effects	- a notice is sent to opers, IF spy mode is configured
 * 		  in the conf file.
 */
static void stats_L_spy(struct Client *source_p, char statchar, char *name)
{
  struct hook_stats_data data;

  data.source_p = source_p;
  data.statchar = statchar;
  data.name = name;

  hook_call_event("doing_stats", &data);
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
static char *parse_stats_args(int parc,char *parv[],int *doall,int *wilds)
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
