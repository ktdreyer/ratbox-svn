/*  contrib/m_tburst.c
 *  Copyright (C) 2002 Hybrid Develompent Team
 *
 *  $Id$
 */

#include <string.h>
#include "handlers.h"
#include "client.h"
#include "common.h"
#include "ircd.h"
#include "send.h"
#include "msg.h"
#include "modules.h"
#include "hook.h"
#include "hash.h"
#include "s_serv.h"
#include "s_conf.h"


/* TBURST_PROPAGATE
 *
 * If this is defined, when we receive a TBURST thats successful
 * (ie: our topic changes), the TBURST will be propagated to other
 * servers that support TBURST
 */
#define TBURST_PROPAGATE



static void ms_tburst(struct Client *, struct Client *, int, char **);
static void set_topic(struct Client *, struct Channel *, 
		      time_t, char *, char *);
static void set_tburst_capab();
static void unset_tburst_capab();
int send_tburst(struct hook_burst_channel *);

struct Message tburst_msgtab = {
  "TBURST", 0, 0, 6, 0, MFLG_SLOW, 0,
  {m_ignore, m_ignore, ms_tburst, m_ignore}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&tburst_msgtab);
  hook_add_hook("burst_channel", (hookfn *)send_tburst);
  set_tburst_capab();
}

void
_moddeinit(void)
{
  mod_del_cmd(&tburst_msgtab);
  hook_del_hook("burst_channel", (hookfn *)send_tburst);
  unset_tburst_capab();
}

const char *_version = "$Revision$";
#endif

/* ms_tburst()
 * 
 *      parv[0] = sender prefix
 *      parv[1] = channel timestamp
 *      parv[2] = channel
 *      parv[3] = topic timestamp
 *      parv[4] = topic setter
 *      parv[5] = topic
 */
static void ms_tburst(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
  struct Channel *chptr;
  time_t newchannelts;
  time_t newtopicts;

  newchannelts = atol(parv[1]);
  newtopicts = atol(parv[3]);

  if((chptr = hash_find_channel(parv[2])))
  {
    if(chptr->channelts < newchannelts)
      return;

    else if(chptr->channelts == newchannelts)
    {
      if(!chptr->topic[0] || (chptr->topic_time > newtopicts))
	set_topic(source_p, chptr, newtopicts, parv[4], parv[5]);
      else
	return;
    }

    else
      set_topic(source_p, chptr, newtopicts, parv[4], parv[5]);
  }
}

static void set_topic(struct Client *source_p, struct Channel *chptr, 
		      time_t newtopicts, char *topicwho, char *topic)
{
  strlcpy(chptr->topic, topic, TOPICLEN);
  strlcpy(chptr->topic_info, topicwho, USERHOST_REPLYLEN);

  chptr->topic_time = newtopicts;

  sendto_channel_local(ALL_MEMBERS, chptr, ":%s TOPIC %s :%s",
		       ConfigServerHide.hide_servers ? me.name : source_p->name,
		       chptr->chname, topic);

#ifdef TBURST_PROPAGATE
  sendto_server(source_p, NULL, chptr, CAP_TBURST, NOCAPS, NOFLAGS,
		":%s TBURST %ld %s %ld %s :%s",
		source_p->name, chptr->channelts, chptr->chname,
		newtopicts, topicwho, topic);
#endif
}

static void set_tburst_capab()
{
  default_server_capabs |= CAP_TBURST;
}

static void unset_tburst_capab()
{
  default_server_capabs &= ~CAP_TBURST;
}

int send_tburst(struct hook_burst_channel *data)
{
  if(data->chptr->topic[0] && IsCapable(data->client, CAP_TBURST))
    sendto_one(data->client, ":%s TBURST %ld %s %ld %s :%s",
               me.name, data->chptr->channelts, data->chptr->chname,
	       data->chptr->topic_time, data->chptr->topic_info, 
	       data->chptr->topic);

  return 0;
}
