/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_challenge.c
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
#include <stdlib.h>
#include <string.h>
#include "handlers.h"
#include "client.h"
#include "ircd.h"
#include "modules.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "rsa.h"
#include "msg.h"
#include "parse.h"
#include "irc_string.h"
#include "s_log.h"

int oper_up( struct Client *source_p, struct ConfItem *aconf );

#ifndef HAVE_LIBCRYPTO
/* Maybe this should be an error or something?-davidt */
#ifndef STATIC_MODULES
void
_modinit(void)
{
  return;
}

void
_moddeinit(void)
{
  return;
}

char *_version = "20001122";
#endif
#else

static void m_challenge(struct Client*, struct Client*, int, char**);
void binary_to_hex( unsigned char * bin, char * hex, int length );

/* We have openssl support, so include /CHALLENGE */
struct Message challenge_msgtab = {
  "CHALLENGE", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_challenge, m_ignore, m_challenge}
};
#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&challenge_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&challenge_msgtab);
}

char *_version = "20001122";
#endif
/*
 * m_challenge - generate RSA challenge for wouldbe oper
 * parv[0] = sender prefix
 * parv[1] = operator to challenge for, or +response
 *
 */
static void m_challenge( struct Client *client_p, struct Client *source_p,
                        int parc, char *parv[] )
{
  char * challenge;
  dlink_node *ptr;
  struct ConfItem *aconf, *oconf;
  if(!(source_p->user) || !source_p->localClient)
    return;
  
  if (*parv[1] == '+')
    {
     /* Ignore it if we aren't expecting this... -A1kmm */
     if (!source_p->user->response)
       return;
     
     if (irccmp(source_p->user->response, ++parv[1]))
       {
         sendto_one(source_p, form_str(ERR_PASSWDMISMATCH), me.name,
                    source_p->name);
         return;
       }
     
     if (!(aconf = find_conf_by_name(source_p->user->auth_oper, CONF_OPERATOR)))
       {
         sendto_one (source_p, form_str(ERR_NOOPERHOST), me.name, parv[0]);
         return;
       }

    ptr = source_p->localClient->confs.head;
    oconf = ptr->data;
    detach_conf(source_p,oconf);

    if(attach_conf(source_p, aconf) != 0)
      {
	sendto_one(source_p,":%s NOTICE %s :Can't attach conf!",
                   me.name,source_p->name);   
	sendto_realops_flags(FLAGS_ALL, L_ALL,
	                     "Failed OPER attempt by %s (%s@%s) can't attach conf!",
	                     source_p->name, source_p->username, source_p->host);
	attach_conf(source_p, oconf);
	return;
      }
     
     /* Now make them an oper and tell the realops... */
     oper_up(source_p, aconf);
     ilog(L_TRACE, "OPER %s by %s!%s@%s",
	  source_p->user->auth_oper, source_p->name, source_p->username, source_p->host);
     log_oper(source_p, source_p->user->auth_oper);
     MyFree(source_p->user->response);
     MyFree(source_p->user->auth_oper);
     source_p->user->response = NULL;
     source_p->user->auth_oper = NULL;
     return;
    }
  
  MyFree(source_p->user->response);
  MyFree(source_p->user->auth_oper);
  source_p->user->response = NULL;
  source_p->user->response = NULL;

  /* XXX - better get the host matching working sometime... */
  if (!(aconf = find_conf_by_name (parv[1], CONF_OPERATOR))
      /*|| !(match(source_p->host, aconf->host) ||
           memcmp(&source_p->localClient->ip, &aconf->ip,
                  sizeof(struct irc_inaddr)))*/)
    {
     sendto_one (source_p, form_str(ERR_NOOPERHOST), me.name, parv[0]);
     return;
    }
  /* XXX this should be a config flag.. */
  if (!strchr(aconf->passwd, ' '))
    {
     sendto_one (source_p, ":%s NOTICE %s :I'm sorry, PK authentication "
                 "is not enabled for your oper{} block.", me.name,
                 parv[0]);
     return;
    }
  if (
   !generate_challenge (&challenge, &(source_p->user->response), aconf->passwd)
     )
    {
     sendto_one (source_p, form_str(RPL_RSACHALLENGE), me.name, parv[0],
                 challenge);
    }
  DupString(source_p->user->auth_oper, aconf->name);
  MyFree(challenge);
  return;
}

#endif /* HAVE_LIBCRYPTO */
