/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_set.c
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
  int   expects_two_args; /* 1 if it expects char *arg,int,
			 * 0 if it expects only int
			 */
};


int quote_autoconn(struct Client *, char *, int);
int quote_autoconnall(struct Client *, int);
int quote_floodcount(struct Client *, int);
int quote_floodtime(struct Client *, int);
int quote_idletime(struct Client *, int);
int quote_log(struct Client *, int);
int quote_max(struct Client *, int);
int quote_spamnum(struct Client *, int);
int quote_spamtime(struct Client *, int);
int quote_shide(struct Client *, int);
int quote_chide(struct Client *, int);
int list_quote_commands(struct Client *);


static struct SetStruct set_cmd_table[] =
{
  /* name		function	expects two args */
  /* --------------------------------------------------- */
  { "AUTOCONN",		quote_autoconn,		1 },
  { "AUTOCONNALL",	quote_autoconnall,	0 },
  { "FLOODCOUNT",	quote_floodcount,	0 },
  { "IDLETIME",		quote_idletime,		0 },
  { "LOG",		quote_log,		0 },
  { "MAX",		quote_max,		0 },
  { "SPAMNUM",		quote_spamnum,		0 },
  { "SPAMTIME",		quote_spamtime,		0 },
  { "SHIDE",		quote_shide,		0 },
  { "CHIDE",		quote_chide,		0 },
  /* ---------------------------------------------------- */
  { (char *) 0,		(int (*)()) 0,		0 }
};


/*
 * list_quote_commands() sends the client all the available commands.
 * Four to a line for now.
 */
int list_quote_commands(struct Client *sptr)
{
  int i;
  int j=0;
  char *names[4];

  sendto_one(sptr, ":%s NOTICE %s :Available QUOTE SET commands:",
		me.name, sptr->name);

  names[0] = names[1] = names[2] = names[3] = "";

  for (i=0; set_cmd_table[i].handler; i++)
    {
      names[j++] = set_cmd_table[i].name;

      if(j > 3)
	{
	  sendto_one(sptr, ":%s NOTICE %s :%s %s %s %s",
		     me.name, sptr->name,
		     names[0], names[1], 
		     names[2],names[3]);
	  j = 0;
	  names[0] = names[1] = names[2] = names[3] = "";
	}

    }
  if(j)
    sendto_one(sptr, ":%s NOTICE %s :%s %s %s %s",
	       me.name, sptr->name,
	       names[0], names[1], 
	       names[2],names[3]);

  return(0);
}

/* SET AUTOCONN */
int quote_autoconn( struct Client *sptr, char *arg, int newval)
{
  set_autoconn(sptr, sptr->name, arg, newval);
  return(0);
}

/* SET AUTOCONNALL */
int quote_autoconnall( struct Client *sptr, int newval)
{
  if(newval >= 0)
    {
      sendto_realops("%s has changed AUTOCONNALL to %i",
		     sptr->name, newval);

      GlobalSetOptions.autoconn = newval;
    }
  else 
    {
      sendto_one(sptr, ":%s NOTICE %s :AUTOCONNALL is currently %i",
		 me.name, sptr->name, GlobalSetOptions.autoconn);
    }
  return(0);
}


/* SET FLOODCOUNT */
int quote_floodcount( struct Client *sptr, int newval)
{
  if(newval >= 0)
    {
      GlobalSetOptions.floodcount = newval;
      sendto_realops("%s has changed FLOODCOUNT to %i", sptr->name,
		     GlobalSetOptions.floodcount);
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :FLOODCOUNT is currently %i",
		 me.name, sptr->name, GlobalSetOptions.floodcount);
    }
  return(0);
}

/* SET IDLETIME */
int quote_idletime( struct Client *sptr, int newval )
{
  if(newval >= 0)
    {
      if (newval == 0)
	{
	  sendto_realops("%s has disabled idletime checking", sptr->name);
	  GlobalSetOptions.idletime = 0;
	}
      else 
	{
	  sendto_realops("%s has changed IDLETIME to %i", sptr->name, newval);
	  GlobalSetOptions.idletime = (newval*60);
	}
    }
  else 
    {
      sendto_one(sptr, ":%s NOTICE %s :IDLETIME is currently %i",
		 me.name, sptr->name, GlobalSetOptions.idletime/60);
    }
  return(0);
}


/* SET LOG */
int quote_log( struct Client *sptr, int newval )
{
  const char *log_level_as_string;

  if (newval >= 0)
    {
      if (newval < L_WARN)
	{
	  sendto_one(sptr, ":%s NOTICE %s :LOG must be > %d (L_WARN)",
		     me.name, sptr->name, L_WARN);
	  return(0);
	}

      if (newval > L_DEBUG)
	{
	  newval = L_DEBUG;
	}

      set_log_level(newval);
      log_level_as_string = get_log_level_as_string(newval);
      sendto_realops("%s has changed LOG level to %i (%s)",
		     sptr->name, newval, log_level_as_string);
    }
  else 
    {
      sendto_one(sptr, ":%s NOTICE %s :LOG level is currently %i (%s)",
		 me.name, sptr->name, get_log_level(),
		 get_log_level_as_string(get_log_level()));
    }
  return(0);
}

/* SET MAX */
int quote_max( struct Client *sptr, int newval )
{
  if (newval > 0)
    {
      if (newval > MASTER_MAX)
	{
	  sendto_one(sptr,
	     ":%s NOTICE %s :You cannot set MAXCLIENTS to > MASTER_MAX (%d)",
	     me.name, sptr->name, MASTER_MAX);
	  return(0);
	}

      if (newval < 32)
	{
	  sendto_one(sptr,
		     ":%s NOTICE %s :You cannot set MAXCLIENTS to < 32 (%d:%d)",
		     me.name, sptr->name, GlobalSetOptions.maxclients, highest_fd);

	  return(0);
	}

      GlobalSetOptions.maxclients = newval;

      sendto_realops("%s!%s@%s set new MAXCLIENTS to %d (%d current)",
		     sptr->name, sptr->username, sptr->host,
		     GlobalSetOptions.maxclients, Count.local);

      return(0);
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :Current Maxclients = %d (%d)",
		 me.name, sptr->name,
		 GlobalSetOptions.maxclients, Count.local);
    }

  return(0);
}

/* SET SPAMNUM */
int quote_spamnum( struct Client *sptr, int newval )
{
  if (newval > 0)
    {
      if (newval == 0)
	{
	  sendto_realops("%s has disabled ANTI_SPAMBOT", sptr->name);
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
		     sptr->name, GlobalSetOptions.spam_num);
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :SPAMNUM is currently %i",
		 me.name,
		 sptr->name, GlobalSetOptions.spam_num);
    }
  return(0);
}

/* SET SPAMTIME */
int quote_spamtime( struct Client *sptr, int newval )
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
      sendto_realops("%s has changed SPAMTIME to %i",
		     sptr->name, GlobalSetOptions.spam_time);
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is currently %i",
		 me.name, sptr->name, GlobalSetOptions.spam_time);
    }
  return(0);
}

int quote_chide( struct Client *sptr, int newval )
{
  if(newval >= 0)
    {
      if(newval)
	GlobalSetOptions.hide_chanops = 1;
      else
	GlobalSetOptions.hide_chanops = 0;

      sendto_realops("%s has changed CHIDE to %i",
		     sptr->name, GlobalSetOptions.hide_chanops);
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :CHIDE is currently %i",
		 me.name, sptr->name, GlobalSetOptions.hide_chanops);

    }
  return 0;
}

int quote_shide( struct Client *sptr, int newval )
{
  if(newval >= 0)
    {
      if(newval)
	GlobalSetOptions.hide_server = 1;
      else
	GlobalSetOptions.hide_server = 0;

      sendto_realops("%s has changed SHIDE to %i",
		     sptr->name, GlobalSetOptions.hide_server);
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :SHIDE is currently %i",
		 me.name, sptr->name, GlobalSetOptions.hide_server);
    }
  return 0;
}

/*
 * m_set - SET command handler
 * set options while running
 */
int mo_set(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int newval;
  int i;
  char *arg;
  char *intarg;

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
	  if (!strcasecmp(set_cmd_table[i].name, parv[1]))
	    {
	      /*
	       * Command found; now execute the code
	       */
	      if (parc > 3)
		{
		  arg = parv[2];
		  intarg = parv[3];
		}
	      else if(parc > 2)
		{
		  arg = NULL; /* not used */
		  intarg = parv[2];
		}

	      if(parc > 2)
		{
		  newval = atoi(intarg);
		  if(newval < 0)
		    {
		      sento_one(sptr,
				":%s NOTICE %s :Value less than 0 illegal for %s",
				me.name, sptr->name,
				set_cmd_table[i].name);
		      return 0;
		    }
		}
	      else
		newval = -1;

	      if(set_cmd_table[i].expects_two_args)
		{
		  if(parc > 3)
		    return(set_cmd_table[i].handler( sptr, arg, newval ));
		  else
		    {
		      sendto_one(sptr,
				 ":%s NOTICE %s :%s expects two args",
				 me.name, sptr->name,
				 set_cmd_table[i].name);
		      return(0);
		    }
		}
	      else
		return(set_cmd_table[i].handler( sptr, newval ));
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

