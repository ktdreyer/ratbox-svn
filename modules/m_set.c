/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_set.c Copyright (C) 1990
 *   Jarkko Oikarinen and University of Oulu, Computing Center
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
 *   $Id$ */

/* rewritten by jdc */

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
#include "parse.h"
#include "modules.h"

#include <stdlib.h>  /* atoi */

static void mo_set(struct Client*, struct Client*, int, char**);

struct Message set_msgtab = {
  "SET", 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_error, mo_set}
};

  void
_modinit(void)
{
  mod_add_cmd(&set_msgtab);
}

  void
_moddeinit(void)
{
  mod_del_cmd(&set_msgtab);
}

char *_version = "20001122";

/* Structure used for the SET table itself */
struct SetStruct
{
  char  *name;
  void  (*handler)();
  int   wants_char; /* 1 if it expects (char *, [int]) */
  int   wants_int;  /* 1 if it expects ([char *], int) */

  /* eg:  0, 1 == only an int arg
   * eg:  1, 1 == char and int args */
};


static void quote_autoconn(struct Client *, char *, int);
static void quote_autoconnall(struct Client *, int);
static void quote_floodcount(struct Client *, int);
static void quote_idletime(struct Client *, int);
static void quote_log(struct Client *, int);
static void quote_max(struct Client *, int);
static void quote_msglocale(struct Client *, char *);
static void quote_spamnum(struct Client *, int);
static void quote_spamtime(struct Client *, int);
static void quote_shide(struct Client *, int);
static void list_quote_commands(struct Client *);


/* 
 * If this ever needs to be expanded to more than one arg of each
 * type, want_char/want_int could be the count of the arguments,
 * instead of just a boolean flag...
 *
 * -davidt
 */

static struct SetStruct set_cmd_table[] =
{
  /* name		function        string arg  int arg */
  /* -------------------------------------------------------- */
  { "AUTOCONN",		quote_autoconn,		1,	1 },
  { "AUTOCONNALL",	quote_autoconnall,	0,	1 },
  { "FLOODCOUNT",	quote_floodcount,	0,	1 },
  { "IDLETIME",		quote_idletime,		0,	1 },
  { "LOG",		quote_log,		0,	1 },
  { "MAX",		quote_max,		0,	1 },
  { "MSGLOCALE",	quote_msglocale,	1,	0 },
  { "SPAMNUM",		quote_spamnum,		0,	1 },
  { "SPAMTIME",		quote_spamtime,		0,	1 },
  { "SHIDE",		quote_shide,		0,	1 },
  /* -------------------------------------------------------- */
  { (char *) 0,		(void (*)()) 0,		0,	0 }
};


/*
 * list_quote_commands() sends the client all the available commands.
 * Four to a line for now.
 */
static void list_quote_commands(struct Client *server_p)
{
  int i;
  int j=0;
  char *names[4];

  sendto_one(server_p, ":%s NOTICE %s :Available QUOTE SET commands:",
             me.name, server_p->name);

  names[0] = names[1] = names[2] = names[3] = "";

  for (i=0; set_cmd_table[i].handler; i++)
  {
    names[j++] = set_cmd_table[i].name;

    if(j > 3)
    {
      sendto_one(server_p, ":%s NOTICE %s :%s %s %s %s",
                 me.name, server_p->name,
                 names[0], names[1], 
                 names[2],names[3]);
      j = 0;
      names[0] = names[1] = names[2] = names[3] = "";
    }

  }
  if(j)
    sendto_one(server_p, ":%s NOTICE %s :%s %s %s %s",
               me.name, server_p->name,
               names[0], names[1], 
               names[2],names[3]);
}

/* SET AUTOCONN */
static void quote_autoconn( struct Client *server_p, char *arg, int newval)
{
  set_autoconn(server_p, server_p->name, arg, newval);
}

/* SET AUTOCONNALL */
static void quote_autoconnall( struct Client *server_p, int newval)
{
  if(newval >= 0)
  {
    sendto_realops_flags(FLAGS_ALL,"%s has changed AUTOCONNALL to %i",
                         server_p->name, newval);

    GlobalSetOptions.autoconn = newval;
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :AUTOCONNALL is currently %i",
               me.name, server_p->name, GlobalSetOptions.autoconn);
  }
}


/* SET FLOODCOUNT */
static void quote_floodcount( struct Client *server_p, int newval)
{
  if(newval >= 0)
  {
    GlobalSetOptions.floodcount = newval;
    sendto_realops_flags(FLAGS_ALL,
                         "%s has changed FLOODCOUNT to %i", server_p->name,
                         GlobalSetOptions.floodcount);
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :FLOODCOUNT is currently %i",
               me.name, server_p->name, GlobalSetOptions.floodcount);
  }
}

/* SET IDLETIME */
static void quote_idletime( struct Client *server_p, int newval )
{
  if(newval >= 0)
  {
    if (newval == 0)
    {
      sendto_realops_flags(FLAGS_ALL,
                           "%s has disabled idletime checking",
                           server_p->name);
      GlobalSetOptions.idletime = 0;
    }
    else
    {
      sendto_realops_flags(FLAGS_ALL,
                           "%s has changed IDLETIME to %i",
                           server_p->name, newval);
      GlobalSetOptions.idletime = (newval*60);
    }
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :IDLETIME is currently %i",
               me.name, server_p->name, GlobalSetOptions.idletime/60);
  }
}

/* SET LOG */
static void quote_log( struct Client *server_p, int newval )
{
  const char *log_level_as_string;

  if (newval >= 0)
  {
    if (newval < L_WARN)
    {
      sendto_one(server_p, ":%s NOTICE %s :LOG must be > %d (L_WARN)",
                 me.name, server_p->name, L_WARN);
      return;
    }

    if (newval > L_DEBUG)
    {
      newval = L_DEBUG;
    }

    set_log_level(newval);
    log_level_as_string = get_log_level_as_string(newval);
    sendto_realops_flags(FLAGS_ALL,"%s has changed LOG level to %i (%s)",
                         server_p->name, newval, log_level_as_string);
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :LOG level is currently %i (%s)",
               me.name, server_p->name, get_log_level(),
               get_log_level_as_string(get_log_level()));
  }
}

/* SET MAX */
static void quote_max( struct Client *server_p, int newval )
{
  if (newval > 0)
  {
    if (newval > MASTER_MAX)
    {
      sendto_one(server_p,
	":%s NOTICE %s :You cannot set MAXCLIENTS to > MASTER_MAX (%d)",
	me.name, server_p->name, MASTER_MAX);
      return;
    }

    if (newval < 32)
    {
      sendto_one(server_p,
	":%s NOTICE %s :You cannot set MAXCLIENTS to < 32 (%d:%d)",
	me.name, server_p->name, GlobalSetOptions.maxclients, highest_fd);
      return;
    }

    GlobalSetOptions.maxclients = newval;

    sendto_realops_flags(FLAGS_ALL,
	"%s!%s@%s set new MAXCLIENTS to %d (%d current)",
	server_p->name, server_p->username, server_p->host,
	GlobalSetOptions.maxclients, Count.local);

    return;
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :Current Maxclients = %d (%d)",
	me.name, server_p->name,
	GlobalSetOptions.maxclients, Count.local);
  }
}

/* SET MSGLOCALE */
static void quote_msglocale( struct Client *server_p, char *locale )
{
#ifdef USE_GETTEXT
  if(locale)
  {
    char langenv[BUFSIZE];
    ircsprintf(langenv,"LANGUAGE=%s",locale);
    putenv(langenv);

    sendto_one(server_p, ":%s NOTICE %s :Set MSGLOCALE to '%s'",
	me.name, server_p->name,
	getenv("LANGUAGE") ? getenv("LANGUAGE") : "<unset>");
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :MSGLOCALE is currently '%s'",
	me.name, server_p->name,
	(getenv("LANGUAGE")) ? getenv("LANGUAGE") : "<unset>");
  }
#else
  sendto_one(server_p, ":%s NOTICE %s :No gettext() support available.",
	me.name, server_p->name);
#endif
}

/* SET SPAMNUM */
static void quote_spamnum( struct Client *server_p, int newval )
{
  if (newval > 0)
  {
    if (newval == 0)
    {
      sendto_realops_flags(FLAGS_ALL,
                           "%s has disabled ANTI_SPAMBOT", server_p->name);
      GlobalSetOptions.spam_num = newval;
      return;
    }
    if (newval < MIN_SPAM_NUM)
    {
      GlobalSetOptions.spam_num = MIN_SPAM_NUM;
    }
    else /* if (newval < MIN_SPAM_NUM) */
    {
      GlobalSetOptions.spam_num = newval;
    }
    sendto_realops_flags(FLAGS_ALL,"%s has changed SPAMNUM to %i",
		server_p->name, GlobalSetOptions.spam_num);
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :SPAMNUM is currently %i",
		me.name,
		server_p->name, GlobalSetOptions.spam_num);
  }
}

/* SET SPAMTIME */
static void quote_spamtime( struct Client *server_p, int newval )
{
  if (newval > 0)
  {
    if (newval < MIN_SPAM_TIME)
    {
      GlobalSetOptions.spam_time = MIN_SPAM_TIME;
    }
    else /* if (newval < MIN_SPAM_TIME) */
    {
      GlobalSetOptions.spam_time = newval;
    }
    sendto_realops_flags(FLAGS_ALL,"%s has changed SPAMTIME to %i",
		server_p->name, GlobalSetOptions.spam_time);
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :SPAMTIME is currently %i",
		me.name,
		server_p->name, GlobalSetOptions.spam_time);
  }
}

static void quote_shide( struct Client *server_p, int newval )
{
  if(newval >= 0)
  {
    if(newval)
      GlobalSetOptions.hide_server = 1;
    else
      GlobalSetOptions.hide_server = 0;

    sendto_realops_flags(FLAGS_ALL,"%s has changed SHIDE to %i",
                         server_p->name, GlobalSetOptions.hide_server);
  }
  else
  {
    sendto_one(server_p, ":%s NOTICE %s :SHIDE is currently %i",
               me.name, server_p->name, GlobalSetOptions.hide_server);
  }
}

/*
 * mo_set - SET command handler
 * set options while running
 */
static void mo_set(struct Client *client_p, struct Client *server_p,
                  int parc, char *parv[])
{
  int newval;
  int i, n;
  char *arg=NULL;
  char *intarg=NULL;

  if (parc > 1)
  {
    /*
     * Go through all the commands in set_cmd_table, until one is
     * matched.  I realize strcmp() is more intensive than a numeric
     * lookup, but at least it's better than a big-ass switch/case
     * statement.
     */
    for (i=0; set_cmd_table[i].handler; i++)
    {
      if (!irccmp(set_cmd_table[i].name, parv[1]))
      {
        /*
         * Command found; now execute the code
         */
        n = 2;

        if(set_cmd_table[i].wants_char)
        {
          arg = parv[n++];
        }

        if(set_cmd_table[i].wants_int)
        {
          intarg = parv[n++];
        }

        if( (n - 1) > parc )
        {
          if(parc > 2)
            sendto_one(server_p,
                       ":%s NOTICE %s :SET %s expects (\"%s%s\") args",
                       me.name, server_p->name, set_cmd_table[i].name,
                       (set_cmd_table[i].wants_char ? "string, " : ""),
                       (set_cmd_table[i].wants_char ? "int" : "")
                      );
        }

        if(parc <= 2)
        {
          arg = NULL;
          intarg = NULL;
        }

        if(set_cmd_table[i].wants_int && (parc > 2))
        {
          if(intarg)
          {
            if(!irccmp(intarg, "yes") || !irccmp(intarg, "on"))
              newval = 1;
            else if (!irccmp(intarg, "no") || !irccmp(intarg, "off"))
              newval = 0;
            else
              newval = atoi(intarg);
          }
          else
          {
            newval = -1;
          }

          if(newval < 0)
          {
            sendto_one(server_p,
                       ":%s NOTICE %s :Value less than 0 illegal for %s",
                       me.name, server_p->name,
                       set_cmd_table[i].name);

            return;
          }
        }
        else
          newval = -1;

        if(set_cmd_table[i].wants_char)
        {
          if(set_cmd_table[i].wants_int)
            set_cmd_table[i].handler( server_p, arg, newval );
          else
            set_cmd_table[i].handler( server_p, arg );
          return;
        }
        else
        {
          if(set_cmd_table[i].wants_int)
            set_cmd_table[i].handler( server_p, newval );
          else
            /* Just in case someone actually wants a
             * set function that takes no args.. *shrug* */
            set_cmd_table[i].handler( server_p );
          return;
        }
      }
    }

    /*
     * Code here will be executed when a /QUOTE SET command is not
     * found within set_cmd_table.
     */
    sendto_one(server_p, ":%s NOTICE %s :Variable not found.", me.name, parv[0]);
    return;
  }

  list_quote_commands(server_p);
}

