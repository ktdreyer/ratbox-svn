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

struct Message stats_msgtab = {
  "STATS", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_stats, ms_stats, m_stats}
};

#ifndef STATIC_MODULES
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

char *_version = "20010128";
#endif

const char* Lformat = ":%s %d %s %s %u %u %u %u %u :%u %u %s";

static char *parse_stats_args(int, char **, int *, int *);

static void stats_L(struct Client *, char *, int, int, char);
static void stats_L_list(struct Client *s, char *, int, int,
                         dlink_list *, char);
static void stats_spy(struct Client *, char *);
static void stats_L_spy(struct Client *, char *, char *);

/* One is a char, one is a string, thats why theyre seperate.. -- fl_ */
struct StatsStructLetter
{
  char name;
  void (*handler)();
  int need_oper;
  int need_admin;
  char *description;
};

struct StatsStructWord
{
  char *name;
  void (*handler)();
  int need_oper;
  int need_admin;
};

static void stats_adns_servers(struct Client *);
static void stats_connect(struct Client *);
static void stats_deny(struct Client *);
static void stats_exempt(struct Client *);
static void stats_events(struct Client *);
static void stats_fd(struct Client *);
static void stats_glines(struct Client *);
static void stats_hubleaf(struct Client *);
static void stats_auth(struct Client *);
static void stats_tklines(struct Client *);
static void stats_klines(struct Client *);
static void stats_messages(struct Client *);
static void stats_oper(struct Client *);
static void stats_operedup(struct Client *);
static void stats_ports(struct Client *);
static void stats_quarantine(struct Client *);
static void stats_usage(struct Client *);
static void stats_scache(struct Client *);
static void stats_tstats(struct Client *);
static void stats_uptime(struct Client *);
static void stats_shared(struct Client *);
static void stats_servers(struct Client *);
static void stats_gecos(struct Client *);
static void stats_class(struct Client *);
static void stats_memory(struct Client *);
static void stats_servlinks(struct Client *);
static void stats_ltrace(struct Client *, int, char**);

/* This table is for stats LETTERS, and will only allow single chars..
 * The format is (letter) (function to call) (oper only?) (admin only?)
 * operonly/adminonly in here will override anything in the config..
 * so make stats commands wisely! -- fl_ */
static struct StatsStructLetter stats_let_table[] =
{
  /* name function	need_oper need_admin	description */
  { 'A',  stats_adns_servers,	1,	1,	"ADNS DNS servers in use", },
  { 'a',  stats_adns_servers,	1,	1,	"ADNS DNS servers in use", },
  { 'C',  stats_connect,	1,	0,	"Show C/N.lines", },
  { 'c',  stats_connect,	1,	0,	"Show C/N lines", },
  { 'D',  stats_deny,		1,	0,	"Show D lines", },
  { 'd',  stats_deny,		1,	0,	"Show D lines", },
  { 'e',  stats_exempt,		1,	0,	"Show exempt lines", },
  { 'E',  stats_events,		1,	1,	"Show Events", },
  { 'f',  stats_fd,		1,	1,	"Show used file descriptors", },
  { 'F',  stats_fd,		1,	1,	"Show used file descriptors", },
  { 'g',  stats_glines,		1,	0,	"Show G lines", },
  { 'G',  stats_glines,		1,	0,	"Show G lines", },
  { 'h',  stats_hubleaf,	1,	0,	"Show H/L lines", },
  { 'H',  stats_hubleaf,	1,	0,	"Show H/L lines", },
  { 'i',  stats_auth,		1,	0,	"Show I lines", },
  { 'I',  stats_auth,		1,	0,	"Show I lines", },
  { 'k',  stats_tklines,	1,	0,	"Show temporary K lines", },
  { 'K',  stats_klines,		1,	0,	"Show K lines", },
  { 'l',  stats_ltrace,		0,	0,	"Show info about <nick>", },
  { 'L',  stats_ltrace,		0,	0,	"Show info about <nick>", },
  { 'm',  stats_messages,	1,	0,	"Show command usage stats", },
  { 'M',  stats_messages,	1,	0,	"Show command usage stats", },
  { 'o',  stats_oper,		0,	0,	"Show O lines", },
  { 'O',  stats_oper,		0,	0,	"Show O lines", },
  { 'p',  stats_operedup,	0,	0,	"Show 'active' opers", },
  { 'P',  stats_ports,		1,	0,	"Show listening ports", },
  { 'q',  stats_quarantine,	1,	0,	"Show quarantines (Q lines)", },
  { 'Q',  stats_quarantine,	1,	0,	"Show quarantines (Q lines)", },
  { 'R',  stats_usage,		1,	0,	"Show daemon resource usage", },
  { 'r',  stats_usage,		1,	0,	"Show daemon resource usage", },
  { 's',  stats_scache,		1,	1,	"Show server cache", },
  { 'S',  stats_scache,		1,	1,	"Show server cache", },
  { 't',  stats_tstats,		1,	0,	"Show generic server stats", },
  { 'T',  stats_tstats,		1,	0,	"Show generic server stats", },
  { 'u',  stats_uptime,		0,	0,	"Show server uptime", },
  { 'U',  stats_shared,		1,	0,	"Show shared blocks (U lines)", },
  { 'v',  stats_servers,	1,	0,	"Show connected servers + idle times", },
  { 'V',  stats_servers,	1,	0,	"Show connected servers + idle times", },
  { 'x',  stats_gecos,		1,	0,	"Show gecos bans (X lines)", },
  { 'X',  stats_gecos,		1,	0,	"Show gecos bans (X lines)", },
  { 'y',  stats_class,		1,	0,	"Show classes (Y lines)", },
  { 'Y',  stats_class,		1,	0,	"Show classes (Y lines)", },
  { 'z',  stats_memory,		1,	0,	"Show daemon memory stats", },
  { 'Z',  stats_memory,		1,	0,	"Show daemon memory stats", },
  { '?',  stats_servlinks,	1,	0,	"Show connected servers + SendQ counts", },
  { (char) 0, (void (*)()) 0,	0,	0,	(char *) 0, }
};


/* This table is for stats words, such as /stats auth..
 * Format is same as above.  This table is checked case insensitively,
 * so "auth" and "AUTH" are the same, and if we have full words.. there
 * should be no stats the same.. -- fl_ */
#if 0
static struct StatsStructWord stats_cmd_table[] =
{
  /* name		function	*/
  { "AUTH",		stats_auth,	},
  { (char *) 0, 	(void (*)()) 0, }
};
#endif

/*
 * m_stats by fl_
 *      parv[0] = sender prefix
 *      parv[1] = stat letter/command
 *      parv[2] = (if present) server/mask in stats L
 * 
 * This will search the tables for the appropriate stats letter/command,
 * if found execute it.  One function, for opers and users, although it 
 * could possibly be split up..
 */

static void m_stats(struct Client *client_p, struct Client *source_p,
                   int parc, char *parv[])
{
  int i;
  static time_t last_used = 0;
  char statchar;

  /* Check the user is actually allowed to do /stats, and isnt flooding */
  if(!IsOper(source_p) && (last_used + ConfigFileEntry.pace_wait) > CurrentTime)
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

  /* Is the stats meant for us? */
  if (IsOper(source_p) || !GlobalSetOptions.hide_server)
    {
      if (hunt_server(client_p,source_p,":%s STATS %s :%s",2,parc,parv) != HUNTED_ISME)
        return;
    }

  /* There are two different tables, one for letters, one for strings,
   * we need to know which we have to search, so check the length -- fl_ */
  if(strlen(parv[1]) > 1)
  {
 /* I actually need to add them before I enable this ;) */
#if 0
      for (i=0; stats_cmd_table[i].handler; i++)
      {
        if (!irccmp(stats_cmd_table[i].name, parv[1]))
      }
#endif
    return;
  }
  else
  {
    statchar=parv[1][0];
    for (i=0; stats_let_table[i].handler; i++)
      {
        if (stats_let_table[i].name == statchar)
          {
            /* The stats table says whether its oper only or not, so check --fl_ */
            if(stats_let_table[i].need_oper && !IsOper(source_p))
              {
                sendto_one(source_p, form_str(ERR_NOPRIVILEGES),me.name,source_p->name);
                break;
              }

            /* The stats table says whether its admin only or not, so check --fl_ */
            if(stats_let_table[i].need_admin && !IsAdmin(source_p))
              {
                sendto_one(source_p, form_str(ERR_NOPRIVILEGES),me.name,source_p->name);
                break;
              }
            
            /* Theyre allowed to do the stats, so execute it */

            /* Blah, stats L needs the parameters, none of the others do.. */
            /* XXXX - this should be fixed :P */
            if(statchar == 'L' || statchar == 'l')
              stats_let_table[i].handler(source_p, parc, parv);
            else
              stats_let_table[i].handler(source_p);
          }
       }
   }

  /* Send the end of stats notice, and the stats_spy */
  sendto_one(source_p, form_str(RPL_ENDOFSTATS), me.name, parv[0], parv[1]);
  stats_spy(source_p, parv[1]);
}

static void stats_adns_servers(struct Client *client_p)
{
  report_adns_servers(client_p);
}

static void stats_connect(struct Client *client_p)
{
  report_configured_links(client_p, CONF_SERVER);
}

static void stats_deny(struct Client *client_p)
{
  report_dlines(client_p);
}

static void stats_exempt(struct Client *client_p)
{
  report_exemptlines(client_p);
}

static void stats_events(struct Client *client_p)
{
  show_events(client_p);
}

static void stats_fd(struct Client *client_p)
{
  fd_dump(client_p);
}

static void stats_glines(struct Client *client_p)
{
  if(ConfigFileEntry.glines)
    report_glines(client_p);
  else
    sendto_one(client_p, ":%s NOTICE %s :This server does not support G-Lines",
               me.name, client_p->name); 
}

static void stats_hubleaf(struct Client *client_p)
{
  report_configured_links(client_p, CONF_HUB|CONF_LEAF);
}

static void stats_auth(struct Client *client_p)
{
  report_Ilines(client_p);
}

static void stats_tklines(struct Client *client_p)
{
  report_Klines(client_p, -1);
}

static void stats_klines(struct Client *client_p)
{
  report_Klines(client_p, 0);
}

static void stats_messages(struct Client *client_p)
{
  report_messages(client_p);
}

static void stats_oper(struct Client *client_p)
{
  if (!IsOper(client_p) && ConfigFileEntry.o_lines_oper_only)
    sendto_one(client_p, form_str(ERR_NOPRIVILEGES),me.name,client_p->name);
  else
    report_configured_links(client_p, CONF_OPERATOR);
}

static void stats_operedup(struct Client *client_p)
{
  show_opers(client_p);
}

static void stats_ports(struct Client *client_p)
{
  show_ports(client_p);
}

static void stats_quarantine(struct Client *client_p)
{
  report_qlines(client_p);
}

static void stats_usage(struct Client *client_p)
{
  send_usage(client_p);
}

static void stats_scache(struct Client *client_p)
{
  list_scache(client_p);
}

static void stats_tstats(struct Client *client_p)
{
  tstats(client_p);
}

static void stats_uptime(struct Client *client_p)
{
  time_t now;

   now = CurrentTime - me.since;
   sendto_one(client_p, form_str(RPL_STATSUPTIME), me.name, client_p->name,
              now/86400, (now/3600)%24, (now/60)%60, now%60);
   if(!GlobalSetOptions.hide_server || IsOper(client_p))
      sendto_one(client_p, form_str(RPL_STATSCONN), me.name, client_p->name,
                 MaxConnectionCount, MaxClientCount, Count.totalrestartcount);
}

static void stats_shared(struct Client *client_p)
{
  report_specials(client_p, CONF_ULINE, RPL_STATSULINE);
}

static void stats_servers(struct Client *client_p)
{
  show_servers(client_p);
}

static void stats_gecos(struct Client *client_p)
{
  report_specials(client_p, CONF_XLINE, RPL_STATSXLINE);
}

static void stats_class(struct Client *client_p)
{
  report_classes(client_p);
}

static void stats_memory(struct Client *client_p)
{
  count_memory(client_p);
}

static void stats_servlinks(struct Client *client_p)
{
  serv_info(client_p);
}

static void stats_ltrace(struct Client *client_p, int parc, char *parv[])
{
  int             doall = 0;
  int             wilds = 0;
  char            *name=NULL;
  char            *target=NULL;

  name = parse_stats_args(parc,parv,&doall,&wilds);

  if (parc > 3)
    target = parv[3];

  stats_L(client_p,name,doall,wilds,'L');
  stats_L_spy(client_p, name, parv[1]);

  return;
}

/*
 * ms_stats - STATS message handler
 *      parv[0] = sender prefix
 *      parv[1] = statistics selector (defaults to Message frequency)
 *      parv[2] = server name (current server defaulted, if omitted)
 */

static void ms_stats(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  if (hunt_server(client_p,source_p,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return;

  m_stats(client_p,source_p,parc,parv);
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
static void stats_spy(struct Client *source_p,char *statchar)
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
static void stats_L_spy(struct Client *source_p, char *statchar, char *name)
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
