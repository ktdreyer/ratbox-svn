/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  m_olist.c: List channels.  olist is an oper only command
 *             that shows channels regardless of modes.  This
 *             is kinda evil, and might be morally wrong, but
 *             somebody will likely need it.
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "sprintf_irc.h"
#include "tools.h"
#include "handlers.h"
#include "channel.h"
#include "channel_mode.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "list.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "newconf.h"

#ifndef OPER_SPY
#define OPER_SPY 0x000400
#define IsOperSpy(x) ((x)->flags2 & OPER_SPY)
#endif

static void conf_set_oper_spy(void *);
static void mo_olist(struct Client*, struct Client*, int, char**);
static int list_all_channels(struct Client *);
static void list_one_channel(struct Client *,struct Channel *);

struct Message list_msgtab = {
  "OLIST", 0, 0, 0, 0, MFLG_SLOW, 0,
  {m_unregistered, m_ignore, m_ignore, mo_olist}
};
#ifndef STATIC_MODULES

void
_modinit(void)
{
  mod_add_cmd(&list_msgtab);
  add_conf_item("operator", "oper_spy", CF_YESNO, conf_set_oper_spy);
}

void
_moddeinit(void)
{
  mod_del_cmd(&list_msgtab);
  remove_conf_item("operator", "oper_spy");
}
const char *_version = "$Revision$";
#endif
static int list_all_channels(struct Client *source_p);
static int list_named_channel(struct Client *source_p,char *name);

/*
** mo_olist
**      parv[0] = sender prefix
**      parv[1] = channel
*/
static void mo_olist(struct Client *client_p,
                   struct Client *source_p,
                   int parc,
                   char *parv[])
{
  if(!IsOperSpy(source_p))
  {
    sendto_one(source_p, ":%s NOTICE %s :You need oper_spy = yes;",
               me.name, source_p->name);
    return;
  }

  /* If no arg, do all channels *whee*, else just one channel */
  if (parc < 2 || BadPtr(parv[1]))
    {
      list_all_channels(source_p);
    }
  else
    {
      list_named_channel(source_p,parv[1]);
    }
}


/*
 * list_all_channels
 * inputs	- pointer to client requesting list
 * output	- 0/1
 * side effects	- list all channels to source_p
 */
static int list_all_channels(struct Client *source_p)
{
  struct Channel *chptr;
  dlink_node *ptr;
  sendto_one(source_p, form_str(RPL_LISTSTART), me.name, source_p->name);

  DLINK_FOREACH(ptr, global_channel_list.head)
    {
      chptr = (struct Channel *)ptr->data;
      list_one_channel(source_p,chptr);
    }

  sendto_one(source_p, form_str(RPL_LISTEND), me.name, source_p->name);
  return 0;
}   
          
/*
 * list_named_channel
 * inputs       - pointer to client requesting list
 * output       - 0/1
 * side effects	- list all channels to source_p
 */
static int list_named_channel(struct Client *source_p,char *name)
{
  struct Channel *chptr;
  char id_and_topic[TOPICLEN+NICKLEN+6]; /* <!!>, space and null */
  char *p;

  sendto_one(source_p, form_str(RPL_LISTSTART), me.name, source_p->name);

  if((p = strchr(name,',')))
    *p = '\0';
      
  if(*name == '\0')
    {
      sendto_one(source_p, form_str(ERR_NOSUCHNICK),me.name, source_p->name, name);
      sendto_one(source_p, form_str(RPL_LISTEND), me.name, source_p->name);
      return 0;
    }

  chptr = hash_find_channel(name);

  if (chptr == NULL)
    {
      sendto_one(source_p, form_str(ERR_NOSUCHNICK),me.name, source_p->name, name);
      sendto_one(source_p, form_str(RPL_LISTEND), me.name, source_p->name);
      return 0;
    }

  ircsprintf(id_and_topic, "%s", chptr->topic == NULL ? "" : chptr->topic);

  sendto_one(source_p, form_str(RPL_LIST), me.name, source_p->name,
             chptr->chname, chptr->users, id_and_topic);
      
  sendto_one(source_p, form_str(RPL_LISTEND), me.name, source_p->name);
  return 0;
}


/*
 * list_one_channel
 *
 * inputs       - client pointer to return result to
 *              - pointer to channel to list
 * ouput	- none
 * side effects -
 */
static void list_one_channel(struct Client *source_p, struct Channel *chptr)
{
  static char	  modebuf[MODEBUFLEN];
  static char	  parabuf[MODEBUFLEN];
  static char	  buf[MODEBUFLEN*2+TOPICLEN];  
  channel_modes(chptr, source_p, modebuf, parabuf);  
  ircsprintf(buf, "%s %s %s", modebuf, parabuf, chptr->topic == NULL ? "" : chptr->topic);
  sendto_one(source_p, form_str(RPL_LIST), me.name, source_p->name,
             chptr->chname, chptr->users, buf );
}

void
conf_set_oper_spy(void *data)
{
  int yesno = *(int*) data;

  if(yesno)
    yy_achead->port |= OPER_SPY;
  else
    yy_achead->port &= ~OPER_SPY;
}
