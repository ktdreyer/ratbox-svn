/************************************************************************
 *   IRC - Internet Relay Chat, src/m_oper.c
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
#include "common.h"
#include "fdlist.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_user.h"
#include "send.h"
#include "list.h"

#include <fcntl.h>
#include <unistd.h>


/*
 * m_functions execute protocol messages on this server:
 *
 *      cptr    is always NON-NULL, pointing to a *LOCAL* client
 *              structure (with an open socket connected!). This
 *              identifies the physical socket where the message
 *              originated (or which caused the m_function to be
 *              executed--some m_functions may call others...).
 *
 *      sptr    is the source of the message, defined by the
 *              prefix part of the message if present. If not
 *              or prefix not found, then sptr==cptr.
 *
 *              (!IsServer(cptr)) => (cptr == sptr), because
 *              prefixes are taken *only* from servers...
 *
 *              (IsServer(cptr))
 *                      (sptr == cptr) => the message didn't
 *                      have the prefix.
 *
 *                      (sptr != cptr && IsServer(sptr) means
 *                      the prefix specified servername. (?)
 *
 *                      (sptr != cptr && !IsServer(sptr) means
 *                      that message originated from a remote
 *                      user (not local).
 *
 *              combining
 *
 *              (!IsServer(sptr)) means that, sptr can safely
 *              taken as defining the target structure of the
 *              message in this server.
 *
 *      *Always* true (if 'parse' and others are working correct):
 *
 *      1)      sptr->from == cptr  (note: cptr->from == cptr)
 *
 *      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
 *              *cannot* be a local connection, unless it's
 *              actually cptr!). [MyConnect(x) should probably
 *              be defined as (x == x->from) --msa ]
 *
 *      parc    number of variable parameter strings (if zero,
 *              parv is allowed to be NULL)
 *
 *      parv    a NULL terminated list of parameter pointers,
 *
 *                      parv[0], sender (prefix string), if not present
 *                              this points to an empty string.
 *                      parv[1]...parv[parc-1]
 *                              pointers to additional parameters
 *                      parv[parc] == NULL, *always*
 *
 *              note:   it is guaranteed that parv[0]..parv[parc-1] are all
 *                      non-NULL pointers.
 */

static void log_fname( struct Client *sptr, char *name );
static struct ConfItem *find_password_aconf(char *name, struct Client *sptr);
static int match_oper_password(char *password, struct ConfItem *aconf);
static int oper_up( struct Client *sptr, struct ConfItem *aconf );
#ifdef CRYPT_OPER_PASSWORD
extern        char *crypt();
#endif /* CRYPT_OPER_PASSWORD */

/*
** m_oper
**      parv[0] = sender prefix
**      parv[1] = oper name
**      parv[2] = oper password
*/
int m_oper(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct ConfItem *aconf;
  char  *name;
  char  *password;

  name = parc > 1 ? parv[1] : (char *)NULL;
  password = parc > 2 ? parv[2] : (char *)NULL;

  if ((EmptyString(name) || EmptyString(password)))
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "OPER");
      return 0;
    }

  aconf = find_password_aconf(name,sptr);

  if(aconf == NULL)
    return 0;

  if ( match_oper_password(password,aconf) )
    {
      (void)attach_conf(sptr, aconf);
      (void)oper_up( sptr, aconf );

      log(L_TRACE, "OPER %s by %s!%s@%s",
	  name, parv[0], sptr->username, sptr->host);
      log_fname(sptr, name);
    }
  else
    {
      sendto_one(sptr,form_str(ERR_PASSWDMISMATCH),me.name, parv[0]);
      if (ConfigFileEntry.failed_oper_notice)
	{
	  sendto_realops("Failed OPER attempt by %s (%s@%s)",
			 parv[0], sptr->username, sptr->host);
	}
    }
  return 0;
}

/*
** mo_oper
**      parv[0] = sender prefix
**      parv[1] = oper name
**      parv[2] = oper password
*/
int mo_oper(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  sendto_one(sptr, form_str(RPL_YOUREOPER),
	     me.name, parv[0]);
  SendMessageFile(sptr, &ConfigFileEntry.opermotd);
  return 1;
}

/*
** ms_oper
**      parv[0] = sender prefix
**      parv[1] = oper name
**      parv[2] = oper password
*/
int ms_oper(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
        
  /* if message arrived from server, trust it, and set to oper */
  
  if (IsMe(cptr) && !IsGlobalOper(sptr))
    {
      if (sptr->status == STAT_CLIENT)
	sptr->handler = OPER_HANDLER;
      
      sptr->flags |= FLAGS_OPER;
      Count.oper++;
      sendto_serv_butone(cptr, ":%s MODE %s :+o", parv[0], parv[0]);
      if (IsMe(cptr))
        sendto_one(sptr, form_str(RPL_YOUREOPER),
                   me.name, parv[0]);
    }

  return 0;
}

/*
 * log_fname
 *
 * inputs	- pointer to client
 * output	- none
 * side effects - FNAME_OPERLOG is written to, if its present
 */

static void log_fname( struct Client *sptr, char *name )
{
#ifdef FNAME_OPERLOG
  int     logfile;
  static char buf[BUFSIZE];

  /*
   * This conditional makes the logfile active only after
   * it's been created - thus logging can be turned off by
   * removing the file.
   *
   */

  if (IsPerson(sptr) &&
      (logfile = file_open(FNAME_OPERLOG, O_WRONLY|O_APPEND, 0644)) != -1)
    {
      ircsprintf(buf, "%s OPER (%s) by (%s!%s@%s)\n",
		 myctime(CurrentTime), name, 
		 sptr->name, sptr->username,
		 sptr->host);
      write(logfile, buf, strlen(buf));
      file_close(logfile);
    }
#endif
}

/*
 * find_password_aconf
 *
 * inputs	-
 * output	-
 */

static struct ConfItem *find_password_aconf(char *name, struct Client *sptr)
{
  struct ConfItem *aconf;

  if (!(aconf = find_conf_exact(name, sptr->username, sptr->host,
                                CONF_OPS)) &&
      !(aconf = find_conf_exact(name, sptr->username,
                                inetntoa((char *)&sptr->ip), CONF_OPS)))
    {
      sendto_one(sptr, form_str(ERR_NOOPERHOST), me.name, sptr->name);
      if (ConfigFileEntry.failed_oper_notice && ConfigFileEntry.show_failed_oper_id)
	{
	  sendto_realops("Failed OPER attempt - host mismatch by %s (%s@%s)",
			 sptr->name, sptr->username, sptr->host);
	} /* failed_oper_notice && show_failed_oper_id */
      return 0;
    }
  return(aconf);
}

/*
 * match_oper_password
 *
 */

static int match_oper_password(char *password, struct ConfItem *aconf)
{
  char *encr;

  if (!aconf->status & CONF_OPS)
    return NO;

#ifdef CRYPT_OPER_PASSWORD
  /* use first two chars of the password they send in as salt */

  /* passwd may be NULL pointer. Head it off at the pass... */
  if (password && *aconf->passwd)
    encr = crypt(password, aconf->passwd);
  else
    encr = "";
#else
  encr = password;
#endif  /* CRYPT_OPER_PASSWORD */

  if( strcmp(encr, aconf->passwd) == 0 )
    return YES;
  else
    return NO;
}

/*
 * oper_up
 *
 * inputs	- pointer to given client to oper
 *		- pointer to ConfItem to use
 * output	- none
 * side effects	-
 * Blindly opers up given sptr, using aconf info
 * all checks on passwords have already been done.
 * This could also be used by rsa oper routines. 
 */

static int oper_up( struct Client *sptr, struct ConfItem *aconf )
{
  int old = (sptr->umodes & ALL_UMODES);
  char *operprivs;

  if (aconf->status == CONF_LOCOP)
    {
      SetLocOp(sptr);
      if((int)aconf->hold)
	{
	  sptr->umodes |= ((int)aconf->hold & ALL_UMODES); 
	  sendto_one(sptr, ":%s NOTICE %s :*** Oper flags set from conf",
		     me.name,sptr->name);
	}
      else
	{
	  sptr->umodes |= (LOCOP_UMODES);
	}
    }
  else
    {
      SetOper(sptr);
      if((int)aconf->hold)
	{
	  sptr->umodes |= ((int)aconf->hold & ALL_UMODES); 
	  if( !IsSetOperN(sptr) )
	    sptr->umodes &= ~FLAGS_NCHANGE;
	  
	  sendto_one(sptr, ":%s NOTICE %s :*** Oper flags set from conf",
		     me.name,sptr->name);
	}
      else
	{
	  sptr->umodes |= (OPER_UMODES);
	}
    }
  SetIPHidden(sptr);
  Count.oper++;

  SetElined(sptr);
      
  /* LINKLIST */  
  /* add to oper link list -Dianora */
  sptr->next_oper_client = oper_cptr_list;
  oper_cptr_list = sptr;

  if(sptr->confs)
    {
      struct ConfItem *aconf;
      aconf = sptr->confs->value.aconf;
      operprivs = oper_privs_as_string(sptr,aconf->port);
    }
  else
    operprivs = "";

#ifdef CUSTOM_ERR
  sendto_ops("%s (%s@%s) has just acquired the personality of a petty megalomaniacal tyrant [IRC(%c)p]", sptr->name,
	     sptr->username, sptr->host,
	     IsGlobalOper(sptr) ? 'O' : 'o');
#else
  sendto_ops("%s (%s@%s) is now operator (%c)", sptr->name,
	     sptr->username, sptr->host,
	     IsGlobalOper(sptr) ? 'O' : 'o');
#endif /* CUSTOM_ERR */

  send_umode_out(sptr, sptr, old);

  sendto_one(sptr, form_str(RPL_YOUREOPER), me.name, sptr->name);
  sendto_one(sptr, ":%s NOTICE %s :*** Oper privs are %s",me.name,
	     sptr->name,
	     operprivs);
  SendMessageFile(sptr, &ConfigFileEntry.opermotd);

  return 1;
}








