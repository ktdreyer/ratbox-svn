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

/* Structure used for the SET table itself */
struct SetStruct
{
  char  *name;
  int   (*handler)();
  int   _unused1;	/* unused at this time */
  int   _unused2;	/* unused at this time */
};


int quote_autoconn(struct Client *, int, char **);
int quote_floodcount(struct Client *, int, char **);
int quote_floodtime(struct Client *, int, char **);
int quote_idletime(struct Client *, int, char **);
int quote_log(struct Client *, int, char **);
int quote_max(struct Client *, int, char **);
int quote_spamnum(struct Client *, int, char **);
int quote_spamtime(struct Client *, int, char **);
int quote_shide(struct Client *, int, char **);
int quote_chide(struct Client *, int, char **);
int list_quote_commands(struct Client *);


static struct SetStruct set_cmd_table[] =
{
  /* name		function		unused	unused */
  /* --------------------------------------------------------- */
  { "AUTOCONN",		quote_autoconn,		0,	3 },
  { "FLOODCOUNT",	quote_floodcount,	0,	2 },
  { "FLOODTIME",	quote_floodtime,	0,	2 },
  { "IDLETIME",		quote_idletime,		0,	2 },
  { "LOG",		quote_log,		0,	2 },
  { "MAX",		quote_max,		0,	2 },
  { "SPAMNUM",		quote_spamnum,		0,	2 },
  { "SPAMTIME",		quote_spamtime,		0,	2 },
  { "SHIDE",		quote_shide,		0,	2 },
  { "CHIDE",		quote_chide,		0,	2 },
  /* --------------------------------------------------------- */
  { (char *) 0,		(int (*)()) 0,		0,	0 }
};


/*
 * list_quote_commands() sends the client all the available commands.
 * This function should PROBABLY be re-written, as it pretty much
 * "floods" the user with all the commands on each individual line.
 * This isn't all that great, but, at least it's more dynamic...
 */
int list_quote_commands(struct Client *sptr)
{
  int i;

  sendto_one(sptr, ":%s NOTICE %s :Available QUOTE SET commands:",
		me.name, sptr->name);

  for (i=0; set_cmd_table[i].handler; i++)
    {
      sendto_one(sptr, ":%s NOTICE %s :%s",
		 me.name, sptr->name, set_cmd_table[i].name);
    }
  return(0);
}



/* SET AUTOCONN */
int quote_autoconn( struct Client *sptr,
		    int parc, char *parv[])
{
  if (parc > 3)
  {
    int newval = atoi(parv[3]);

    if (!irccmp(parv[2],"ALL"))
    {
      sendto_realops("%s has changed AUTOCONN ALL to %i", parv[0], newval);

      GlobalSetOptions.autoconn = newval;
    }
    else /* if (!irccmp(parv[2],"ALL")) */
    {
      set_autoconn(sptr, parv[0], parv[2], newval);
    }
  }
  else /* if (parc > 3) */
  {
    sendto_one(sptr, ":%s NOTICE %s :AUTOCONN ALL is currently %i",
		me.name, parv[0], GlobalSetOptions.autoconn);
  }
  return(0);
}


/* SET FLOODCOUNT */
int quote_floodcount( struct Client *sptr,
		      int parc, char *parv[])
{
  if (parc > 2)
  {
    int newval = atoi(parv[2]);

    if (newval <= 0)
    {
      sendto_one(sptr, ":%s NOTICE %s :FLOODCOUNT must be > 0",
	me.name, parv[0]);

      return(0);
    }

    GlobalSetOptions.floodcount = newval;
    sendto_realops("%s has changed FLOODCOUNT to %i", parv[0], 
		   GlobalSetOptions.floodcount);
  }
  else /* if (parc > 2) */
  {
    sendto_one(sptr, ":%s NOTICE %s :FLOODCOUNT is currently %i",
	me.name, parv[0], GlobalSetOptions.floodcount);
  }
  return(0);
}


/* SET FLOODTIME */
int quote_floodtime( struct Client *sptr,
		     int parc, char *parv[])
{
  if (parc > 2)
    {
      int newval = atoi(parv[2]);

      if (newval < 0)
	{
	  sendto_one(sptr, ":%s NOTICE %s :FLOODTIME must be > 0",
		     me.name, parv[0]);

	  return(0);
	}
      GlobalSetOptions.floodtime = newval;

      if (GlobalSetOptions.floodtime == 0)
	{
	  sendto_realops("%s has disabled the ANTI_FLOOD code", parv[0]);
	}
      else
	{
	  sendto_realops("%s has changed FLOODTIME to %i", parv[0], 
			 GlobalSetOptions.floodtime);
	}
    }
  else /* if (parc > 2) */
    {
      sendto_one(sptr, ":%s NOTICE %s :FLOODTIME is currently %i",
		 me.name, parv[0], GlobalSetOptions.floodtime);
    }
  return(0);
}

/* SET IDLETIME */
int quote_idletime( struct Client *sptr,
		    int parc, char *parv[])
{
  if (parc > 2)
    {
      int newval = atoi(parv[2]);

      if (newval == 0)
	{
	  sendto_realops("%s has disabled IDLE_CHECK", parv[0]);
	  GlobalSetOptions.idletime = 0;
	}
      else /* if (newval == 0) */
	{
	  sendto_realops("%s has changed IDLETIME to %i", parv[0], newval);
	  GlobalSetOptions.idletime = (newval*60);
	}
    }
  else /* if (parc > 2) */
    {
      sendto_one(sptr, ":%s NOTICE %s :IDLETIME is currently %i",
		 me.name, parv[0], GlobalSetOptions.idletime/60);
    }
  return(0);
}


/* SET LOG */
int quote_log( struct Client *sptr,
	       int parc, char *parv[])
{
  if (parc > 2)
    {
      int newval = atoi(parv[2]);
      const char *log_level_as_string;

      if (newval < L_WARN)
	{
	  sendto_one(sptr, ":%s NOTICE %s :LOG must be > %d (L_WARN)",
		     me.name, parv[0], L_WARN);
	  return(0);
	}

      if (newval > L_DEBUG)
	{
	  newval = L_DEBUG;
	}

      set_log_level(newval);
      log_level_as_string = get_log_level_as_string(newval);
      sendto_realops("%s has changed LOG level to %i (%s)",
		     parv[0], newval, log_level_as_string);
    }
  else /* if (parc > 2) */
    {
      sendto_one(sptr, ":%s NOTICE %s :LOG level is currently %i (%s)",
		 me.name, parv[0], get_log_level(),
		 get_log_level_as_string(get_log_level()));
    }
  return(0);
}

/* SET MAX */
int quote_max( struct Client *sptr, 
	       int parc, char *parv[])
{
  if (parc > 2)
    {
      int new_value = atoi(parv[2]);

      if (new_value > MASTER_MAX)
	{
	  sendto_one(sptr,
	     ":%s NOTICE %s :You cannot set MAXCLIENTS to > MASTER_MAX (%d)",
	     me.name, parv[0], MASTER_MAX);

	  return(0);
	}

    if (new_value < 32)
      {
	sendto_one(sptr,
		   ":%s NOTICE %s :You cannot set MAXCLIENTS to < 32 (%d:%d)",
		   me.name, parv[0], GlobalSetOptions.maxclients, highest_fd);

	return(0);
      }

    GlobalSetOptions.maxclients = new_value;

    sendto_realops("%s!%s@%s set new MAXCLIENTS to %d (%d current)",
		   parv[0], sptr->username, sptr->host,
		   GlobalSetOptions.maxclients, Count.local);

    return(0);
    }
  sendto_one(sptr, ":%s NOTICE %s :Current Maxclients = %d (%d)",
	     me.name, parv[0],
	     GlobalSetOptions.maxclients, Count.local);

  return(0);
}

/* SET SPAMNUM */
int quote_spamnum( struct Client *sptr,
		   int parc, char *parv[])
{
  if (parc > 2)
    {
      int newval = atoi(parv[2]);

      if (newval < 0)
	{
	  sendto_one(sptr, ":%s NOTICE %s :SPAMNUM must be > 0",
		     me.name, parv[0]);
	  return(0);
	}
      if (newval == 0)
	{
	  sendto_realops("%s has disabled ANTI_SPAMBOT", parv[0]);
	  GlobalSetOptions.spam_num = newval;
	  return(0);
	}
      if (newval < MIN_SPAM_NUM)
	{
	  GlobalSetOptions.spam_num = MIN_SPAM_NUM;
	}
      else /* if (newval < MIN_SPAM_NUM) */
	{
	  GlobalSetOptions.spam_num = newval;
	}
      sendto_realops("%s has changed SPAMNUM to %i",
		     parv[0], GlobalSetOptions.spam_num);
    }
  else /* if (parc > 2) */
    {
      sendto_one(sptr, ":%s NOTICE %s :SPAMNUM is currently %i",
		 me.name,
		 parv[0], GlobalSetOptions.spam_num);
    }
  return(0);
}

/* SET SPAMTIME */
int quote_spamtime( struct Client *sptr,
		    int parc, char *parv[])
{
  if (parc > 2)
    {
      int newval = atoi(parv[2]);

      if (newval <= 0)
	{
	  sendto_one(sptr, ":%s NOTICE %s :SPAMTIME must be > 0",
		     me.name, parv[0]);

	  return(0);
	}
      if (newval < MIN_SPAM_TIME)
	{
	  GlobalSetOptions.spam_time = MIN_SPAM_TIME;
	}
      else /* if (newval < MIN_SPAM_TIME) */
	{
	  GlobalSetOptions.spam_time = newval;
	}
      sendto_realops("%s has changed SPAMTIME to %i",
		     parv[0], GlobalSetOptions.spam_time);
    }
  else /* if (parc > 2) */
    {
      sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is currently %i",
		 me.name, parv[0], GlobalSetOptions.spam_time);
    }
  return(0);
}

int quote_chide( struct Client *sptr,
		     int parc, char *parv[])
{
  if(parc > 2)
    {
      int newval = atoi(parv[2]);

      if(newval)
	GlobalSetOptions.hide_chanops = 1;
      else
	GlobalSetOptions.hide_chanops = 0;

      sendto_realops("%s has changed CHIDE to %i",
		     parv[0], GlobalSetOptions.hide_chanops);
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :CHIDE is currently %i",
		 me.name, parv[0], GlobalSetOptions.hide_chanops);

    }
  return 0;
}

int quote_shide( struct Client *sptr,
		     int parc, char *parv[])
{
  if(parc > 2)
    {
      int newval = atoi(parv[2]);

      if(newval)
	GlobalSetOptions.hide_server = 1;
      else
	GlobalSetOptions.hide_server = 0;

      sendto_realops("%s has changed SHIDE to %i",
		     parv[0], GlobalSetOptions.hide_server);
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :SHIDE is currently %i",
		 me.name, parv[0], GlobalSetOptions.hide_server);
    }
  return 0;
}

/*
 * m_set - SET command handler
 * set options while running
 */
int mo_set(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  if (parc > 1)
    {
      int i;
      int result;

      /*
       * Go through all the commands in set_cmd_table, until one is
       * matched.  I realize strcmp() is more intensive than a numeric
       * lookup, but at least it's better than a big-ass switch/case
       * statement.
       */
      for (i=0; set_cmd_table[i].handler; i++)
	{
	  if (!strcasecmp(set_cmd_table[i].name, parv[1]))
	    {
	      /*
	       * Command found; now execute the code
	       */
	      result = set_cmd_table[i].handler( sptr, parc, parv);
	      return(result);
	    }
	}
      /*
       * Code here will be executed when a /QUOTE SET command is not
       * found within set_cmd_table.
       */
      sendto_one(sptr, ":%s NOTICE %s :Variable not found.", me.name, parv[0]);
      return(0);
    }
  list_quote_commands(sptr);
  return(0);
}

