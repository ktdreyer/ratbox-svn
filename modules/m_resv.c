/*
 * modules/m_resv.c
 * Copyright (C) 2001 Hybrid Development Team
 *
 * $Id$
 */
#include "handlers.h"
#include "client.h"
#include "channel.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "s_conf.h"
#include "resv.h"
#include "hash.h"

static void mo_resv(struct Client *, struct Client *, int, char **);
static void mo_unresv(struct Client *, struct Client *, int, char **);

struct Message resv_msgtab = {
  "RESV", 0, 3, 0, MFLG_SLOW | MFLG_UNREG, 0,
  {m_ignore, m_not_oper, m_ignore, mo_resv}
};

struct Message unresv_msgtab = {
  "UNRESV", 0, 2, 0, MFLG_SLOW | MFLG_UNREG, 0,
  {m_ignore, m_not_oper, m_ignore, mo_unresv}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&resv_msgtab);
  mod_add_cmd(&unresv_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&resv_msgtab);
  mod_del_cmd(&unresv_msgtab);
}

char *_version = "20010626";
#endif

/*
 * mo_resv()
 *      parv[0] = sender prefix
 *      parv[1] = channel/nick to forbid
 */

static void mo_resv(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  struct Resv *resv_p;
  char ctype[BUFSIZE];
  int type;

  if(BadPtr(parv[1]))
    return;

  /* someone is creating a resv on a channel */
  if(IsChannelName(parv[1]))
  {
    type = RESV_CHANNEL;
    ircsprintf(ctype, "channel");
  }
  
  /* someone is creating a resv on a nick */
  else if(clean_nick_name(parv[1]))
  {
    type = RESV_NICK;
    ircsprintf(ctype, "nick");
  }
  
  /* neither of the above, tell them */
  else
  {
    sendto_one(source_p, ":%s NOTICE %s :You have specified an invalid resv: [%s]",
               me.name, source_p->name, parv[1]);
    return;
  }
    
  /* create_resv() makes the resv, and adds it to the hash table */
  resv_p = create_resv(parv[1], parv[2], type, 0);
  
  /* create_resv() returns NULL if the resv already exists.. */
  if(!(resv_p))
  {
    sendto_one(source_p,
               ":%s NOTICE %s :A RESV has already been placed on %s: %s",
               me.name, source_p->name, ctype, parv[1]);
    return;
  }
    
  /* it doesnt exist.. it returns resv_p which contains our info */
  sendto_one(source_p,
             ":%s NOTICE %s :A local RESV has been placed on %s: %s [%s]",
             me.name, source_p->name, ctype,
             resv_p->name, resv_p->reason);

  sendto_realops_flags(FLAGS_ALL,
                       "%s has placed a local RESV on %s: %s [%s]",
         	       get_oper_name(source_p), ctype, 
		       resv_p->name, resv_p->reason);
             
}

/*
 * mo_unresv()
 *     parv[0] = sender prefix
 *     parv[1] = channel/nick to unforbid
 */

static void mo_unresv(struct Client *client_p, struct Client *source_p,
                      int parc, char *parv[])
{
  struct Resv *resv_p;
  char ctype[BUFSIZE];
  int type;

  if(IsChannelName(parv[1]))
  {
    type = RESV_CHANNEL;
    ircsprintf(ctype, "channel");
  }
  else if(clean_nick_name(parv[1]))
  {
    type = RESV_NICK;
    ircsprintf(ctype, "nick");
  }
  else
    return;
						
  /* if theres no list of resv's, or we cant find the resv.. tell them */						
  if(!ResvList || !(resv_p = (struct Resv *)hash_find_resv(parv[1], (struct Resv *)NULL, type)))
  {
    sendto_one(source_p, 
               ":%s NOTICE %s :A RESV does not exist for %s: %s",
	       me.name, source_p->name, ctype, parv[1]);
    return;
  }
  /* if resv_p->conf, then it was added from ircd.conf, so we cant remove it.. */
  else if(resv_p->conf)
  {
    sendto_one(source_p,
       ":%s NOTICE %s :The RESV for %s: %s is in the config file and must be removed by hand.",
               me.name, source_p->name, ctype, parv[1]);
    return;	       
  }
  /* otherwise, delete it */
  else
  {
    delete_resv(resv_p);

    sendto_one(source_p,
               ":%s NOTICE %s :The local RESV has been removed on %s: %s",
	       me.name, source_p->name, ctype, parv[1]);
    sendto_realops_flags(FLAGS_ALL,
                         "%s has removed the local RESV for %s: %s",
			 get_oper_name(source_p), ctype, parv[1]);
	      
  }
}
