/*
 *  ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 *  channel.c: Controls channels.
 *
 * Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center 
 * Copyright (C) 1996-2002 Hybrid Development Team 
 * Copyright (C) 2002 ircd-ratbox development team 
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

#include "tools.h"
#include "channel.h"
#include "channel_mode.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"             /* captab */
#include "s_user.h"
#include "send.h"
#include "whowas.h"
#include "s_conf.h"             /* ConfigFileEntry, ConfigChannel */
#include "event.h"
#include "memory.h"
#include "balloc.h"
#include "resv.h"


#include "s_log.h"

struct config_channel_entry ConfigChannel;
dlink_list global_channel_list;
BlockHeap *channel_heap;
BlockHeap *ban_heap;
BlockHeap *topic_heap;

static void destroy_channel(struct Channel *);

static void delete_members(struct Channel *chptr, dlink_list * list);

static void send_mode_list(struct Client *client_p, char *chname,
                           dlink_list *top, char flag);
static int check_banned(struct Channel *chptr, struct Client *who,
                                                char *s, char *s2);

static char buf[BUFSIZE];
static char modebuf[MODEBUFLEN], parabuf[MODEBUFLEN];

/* 
 * init_channels
 *
 * Initializes the channel blockheap
 */
static void channelheap_garbage_collect(void *unused)
{
  BlockHeapGarbageCollect(channel_heap);
  BlockHeapGarbageCollect(ban_heap);
  BlockHeapGarbageCollect(topic_heap);
}

void init_channels(void)
{
  channel_heap = BlockHeapCreate(sizeof(struct Channel), CHANNEL_HEAP_SIZE);
  ban_heap = BlockHeapCreate(sizeof(struct Ban), BAN_HEAP_SIZE);
  topic_heap = BlockHeapCreate(TOPICLEN+1 + USERHOST_REPLYLEN, TOPIC_HEAP_SIZE);
  eventAddIsh("channelheap_garbage_collect", channelheap_garbage_collect,
              NULL, 45);
}

/*
 * add_user_to_channel
 * 
 * inputs       - pointer to channel to add client to
 *              - pointer to client (who) to add
 *              - flags for chanops etc
 * output       - none
 * side effects - adds a user to a channel by adding another link to the
 *                channels member chain.
 */

struct _add_table { int mode; dlink_list *list; };

void
add_user_to_channel(struct Channel *chptr, struct Client *who, int flags)
{
  int x, ok = 0;
  
  struct _add_table add_list[] = 
  {
     { MODE_PEON, &chptr->peons },
     { MODE_CHANOP, &chptr->chanops },
     { MODE_VOICE, &chptr->voiced },
     { MODE_CHANOP|MODE_VOICE, &chptr->chanops_voiced },
     { 0, NULL }  
  };  

  struct _add_table add_loclist[] = 
  {
     { MODE_PEON, &chptr->locpeons },
     { MODE_CHANOP, &chptr->locchanops },
     { MODE_VOICE, &chptr->locvoiced },
     { MODE_CHANOP|MODE_VOICE, &chptr->locchanops_voiced },
     { 0, NULL }  
  };  
  
  if(who->user == NULL)
     return;
  
  for(x = 0; add_list[x].list != NULL; x++)
  {
     if(add_list[x].mode == flags)
     {
        dlinkAddAlloc(who, add_list[x].list);
        ok++;
        break; 
     }
  }

  if(MyClient(who))
  {
    for(x = 0; add_loclist[x].list != NULL; x++)
    {
      if(add_loclist[x].mode == flags)
      {
        dlinkAddAlloc(who, add_loclist[x].list);
        ok++;
        break; 
      }
    }
  }

  if(flags & MODE_DEOPPED)
  {
     dlinkAddAlloc(who, &chptr->deopped);
  }
  
  if(!ok)
  {
    dlinkAddAlloc(who, &chptr->peons);
    if(MyClient(who))
      dlinkAddAlloc(who, &chptr->locpeons);
  }

  chptr->users++;
  if(MyClient(who))
     chptr->locusers++;

  dlinkAddAlloc(chptr, &who->user->channel);
  who->user->joined++;
}


/*
 * remove_user_from_channel
 * 
 * inputs       - pointer to channel to remove client from
 *              - pointer to client (who) to remove
 * output       - did the channel get destroyed
 * side effects - deletes an user from a channel by removing a link in the
 *                channels member chain.
 */
int
remove_user_from_channel(struct Channel *chptr, struct Client *who)
{
  int x;

  dlink_list *chan_loclists[] =
  {
    &chptr->locpeons,   
    &chptr->locvoiced,  
    &chptr->locchanops, 
    &chptr->locchanops_voiced,
    NULL
  };

  dlink_list *chan_lists[] =
  {
    &chptr->peons,
    &chptr->voiced,
    &chptr->chanops,
    &chptr->chanops_voiced,
    NULL
  };

  if(MyClient(who))
  {
    for(x = 0; chan_loclists[x] != NULL; x++)
    {
       if(dlinkFindDestroy(chan_loclists[x], who))
         break;
    }
  }

  for(x = 0; chan_lists[x] != NULL; x++)
  {
     if(dlinkFindDestroy(chan_lists[x], who))
       break;
  }

  dlinkFindDestroy(&chptr->deopped, who);
  dlinkFindDestroy(&who->user->channel, chptr);

  chptr->users_last = CurrentTime;
  who->user->joined--;

  if (MyClient(who))
  {
    if (chptr->locusers > 0)
      chptr->locusers--;
  }

  if (--chptr->users <= 0)
  {
    assert(chptr->users >= 0);
    chptr->users = 0;           /* if chptr->users < 0, make sure it sticks at 0
                                 * It should never happen but...
                                 */
    destroy_channel(chptr);
    return 1;
  }

  return 0;
}

/* qs_user_from_channel()
 *
 * inputs       - channel to remove from, user to remove
 * outputs      -
 * side effects - user is removed from channel, made persisting if last
 *                user to leave.
 */
int
qs_user_from_channel(struct Channel *chptr, struct Client *who)
{
  int x;

  dlink_list *chan_loclists[] =
  {
    &chptr->locpeons,   
    &chptr->locvoiced,  
    &chptr->locchanops, 
    &chptr->locchanops_voiced,
    NULL
  };

  dlink_list *chan_lists[] =
  {
    &chptr->peons,
    &chptr->voiced,
    &chptr->chanops,
    &chptr->chanops_voiced,
    NULL
  };

  if(MyClient(who))
  {
    for(x = 0; chan_loclists[x] != NULL; x++)
    {
      if(dlinkFindDestroy(chan_loclists[x], who))
        break;
    }

    if (chptr->locusers > 0)
      chptr->locusers--;
  }

  for(x = 0; chan_lists[x] != NULL; x++)
  {
    if(dlinkFindDestroy(chan_lists[x], who))
      break;
  }

  dlinkFindDestroy(&chptr->deopped, who);
  dlinkFindDestroy(&who->user->channel, chptr);

  chptr->users_last = CurrentTime;
  who->user->joined--;

  assert(chptr->users > 0);

  if (--chptr->users <= 0)
  {
    chptr->users = 0;

    /* persistent channel - must be 12h old */
    if (!ConfigChannel.persist_time ||
       ((chptr->channelts + (60*60*12)) > CurrentTime))
    {
      destroy_channel(chptr);
      return 1;
    }
  }

  return 0;
}

/*
 * inputs       -
 * output       - NONE
 * side effects -
 */
static void
send_members(struct Client *client_p,
             char *lmodebuf,
             char *lparabuf,
             struct Channel *chptr, dlink_list * list, char *op_flag)
{
  dlink_node *ptr;
  int tlen;                     /* length of t (temp pointer) */
  int mlen;                     /* minimum length */
  int cur_len = 0;              /* current length */
  struct Client *target_p;
  int data_to_send = 0;
  char *t;                      /* temp char pointer */

  cur_len = mlen = ircsprintf(buf, ":%s SJOIN %lu %s %s %s:", me.name,
                   (unsigned long)chptr->channelts,
                   chptr->chname, lmodebuf, lparabuf);

  t = buf + mlen;

  DLINK_FOREACH(ptr, list->head)
  {
    target_p = ptr->data;
    ircsprintf(t, "%s%s ", op_flag, target_p->name);

    tlen = strlen(t);
    cur_len += tlen;
    t += tlen;
    data_to_send = 1;

    if (cur_len > (BUFSIZE - 80))
    {
      data_to_send = 0;
      sendto_one(client_p, "%s", buf);
      cur_len = mlen;
      t = buf + mlen;
    }
  }

  if (data_to_send)
  {
    sendto_one(client_p, "%s", buf);
  }
}

/*
 * send_channel_modes
 *
 * inputs       - pointer to client client_p
 *              - pointer to channel pointer
 * output       - NONE
 * side effects - send "client_p" a full list of the modes for channel chptr.
 */
void
send_channel_modes(struct Client *client_p, struct Channel *chptr)
{
  if (*chptr->chname != '#')
    return;

  *modebuf = *parabuf = '\0';
  channel_modes(chptr, client_p, modebuf, parabuf);

  send_members(client_p, modebuf, parabuf, chptr, &chptr->chanops, "@");
  send_members(client_p, modebuf, parabuf, chptr, &chptr->chanops_voiced, "@+");
  send_members(client_p, modebuf, parabuf, chptr, &chptr->voiced, "+");
  send_members(client_p, modebuf, parabuf, chptr, &chptr->peons, "");

  send_mode_list(client_p, chptr->chname, &chptr->banlist, 'b');

  if (IsCapable(client_p, CAP_EX))
    send_mode_list(client_p, chptr->chname, &chptr->exceptlist, 'e');

  if (IsCapable(client_p, CAP_IE))
    send_mode_list(client_p, chptr->chname, &chptr->invexlist, 'I');
}

/*
 * send_mode_list
 * inputs       - client pointer to server
 *              - pointer to channel
 *              - pointer to top of mode link list to send
 *              - char flag flagging type of mode i.e. 'b' 'e' etc.
 * output       - NONE
 * side effects - sends +b/+e/+I
 *
 */
static void
send_mode_list(struct Client *client_p, char *chname, 
               dlink_list *top, char flag)
{
  dlink_node *lp;
  struct Ban *banptr;
  char mbuf[MODEBUFLEN];
  char pbuf[BUFSIZE];
  int tlen;
  int mlen;
  int cur_len;
  char *mp;
  char *pp;
  int count = 0;

  mlen = ircsprintf(buf, ":%s MODE %s +", me.name, chname);
  cur_len = mlen;

  mp = mbuf;
  pp = pbuf;

  DLINK_FOREACH(lp, top->head)
  {
    banptr = lp->data;
    tlen = strlen(banptr->banstr) + 3;

    /* uh oh */
    if(tlen > MODEBUFLEN)
      continue;

    if ((count >= MAXMODEPARAMS) || ((cur_len + tlen + 2) > (BUFSIZE - 3)))
    {
      sendto_one(client_p, "%s%s %s", buf, mbuf, pbuf);

      mp = mbuf;
      pp = pbuf;
      cur_len = mlen;
      count = 0;
    }

    *mp++ = flag;
    *mp = '\0';
    pp += ircsprintf(pp, "%s ", banptr->banstr);
    cur_len += tlen;
    count++;
  }

  if (count != 0)
    sendto_one(client_p, "%s%s %s", buf, mbuf, pbuf);
}


/*
 * check_channel_name
 * inputs       - channel name
 * output       - true (1) if name ok, false (0) otherwise
 * side effects - check_channel_name - check channel name for
 *                invalid characters
 */
int
check_channel_name(const char *name)
{
  assert(name != NULL);
  if(name == NULL)
    return 0;
    
  for (; *name; ++name)
  {
    if (!IsChanChar(*name))
      return 0;
  }

  return 1;
}

/*
 * free_channel_list
 *
 * inputs       - pointer to dlink_list
 * output       - NONE
 * side effects -
 */
void
free_channel_list(dlink_list * list)
{
  dlink_node *ptr;
  dlink_node *next_ptr;
  struct Ban *actualBan;

  DLINK_FOREACH_SAFE(ptr, next_ptr, list->head)
  {
    actualBan = ptr->data;
    MyFree(actualBan->banstr);
    MyFree(actualBan->who);
    BlockHeapFree(ban_heap, actualBan);

    free_dlink_node(ptr);
  }

  list->head = list->tail = NULL;
  list->length = 0;
}

/*
 * cleanup_channels
 *
 * inputs       - not used
 * output       - none
 * side effects - persistent channels... 
 */
void
cleanup_channels(void *unused)
{
  struct Channel *chptr;
  dlink_node *ptr, *next_ptr;

  DLINK_FOREACH_SAFE(ptr, next_ptr, global_channel_list.head)
  {
    chptr = (struct Channel *)ptr->data;
    if(chptr->users == 0)
    {
      if((chptr->users_last + ConfigChannel.persist_time) < CurrentTime)
      {
	destroy_channel(chptr);
      }
    }
  }
}

/*
 * destroy_channel
 * inputs       - channel pointer
 * output       - none
 * side effects - walk through this channel, and destroy it.
 */

static void
destroy_channel(struct Channel *chptr)
{
  dlink_node *ptr, *next;

  /* Walk through all the dlink's pointing to members of this channel,
   * then walk through each client found from each dlink, removing
   * any reference it has to this channel.
   * Finally, free now unused dlink's
   *
   * This test allows us to use this code both for LazyLinks and
   * persistent channels. In the case of a LL the channel need not
   * be empty, it only has to be empty of local users.
   */

  delete_members(chptr, &chptr->chanops);
  delete_members(chptr, &chptr->chanops_voiced);
  delete_members(chptr, &chptr->voiced);
  delete_members(chptr, &chptr->peons);

  delete_members(chptr, &chptr->locchanops);
  delete_members(chptr, &chptr->locchanops_voiced);
  delete_members(chptr, &chptr->locvoiced);
  delete_members(chptr, &chptr->locpeons);

  DLINK_FOREACH_SAFE(ptr, next, chptr->invites.head)
  {
    del_invite(chptr, ptr->data);
  }
  /* free all bans/exceptions/denies */
  free_channel_list(&chptr->banlist);
  free_channel_list(&chptr->exceptlist);
  free_channel_list(&chptr->invexlist);

  /* Free the topic */
  free_topic(chptr);

  dlinkDelete(&chptr->node, &global_channel_list);

  del_from_channel_hash_table(chptr->chname, chptr);
  BlockHeapFree(channel_heap, chptr);
  Count.chan--;
}

/*
 * delete_members
 *
 * inputs       - pointer to list (on channel)
 * output       - none
 * side effects - delete members of this list
 */
static void
delete_members(struct Channel *chptr, dlink_list * list)
{
  dlink_node *ptr;
  dlink_node *next_ptr;

  struct Client *who;

  DLINK_FOREACH_SAFE(ptr, next_ptr, list->head)
  {
    who = (struct Client *)ptr->data;
    dlinkFindDestroy(&who->user->channel, who);
    who->user->joined--;

    /* remove reference to who from chptr */
    dlinkDestroy(ptr, list);
  }
}

/*
 * channel_member_names
 *
 * inputs       - pointer to client struct requesting names
 *              - pointer to channel block
 *              - pointer to name of channel
 *              - show ENDOFNAMES numeric or not
 *                (don't want it with /names with no params)
 * output       - none
 * side effects - lists all names on given channel
 */
void
channel_member_names(struct Client *source_p,
                     struct Channel *chptr,
                     char *name_of_channel, int show_eon)
#ifdef ANONOPS
{
  int mlen;
  int sublists_done = 0;
  int tlen;
  int cur_len;
  char lbuf[BUFSIZE];
  char *t;
  int reply_to_send = NO;
  dlink_node *members_ptr[NUMLISTS];
  char show_flags[NUMLISTS][2];
  struct Client *who;
  int is_member;
  int i;

  /* Find users on same channel (defined by chptr) */
  if (ShowChannel(source_p, chptr))
  {
    ircsprintf(lbuf, form_str(RPL_NAMREPLY),
               me.name, source_p->name, channel_pub_or_secret(chptr));
    mlen = strlen(lbuf);
    ircsprintf(lbuf + mlen, " %s :", name_of_channel);
    mlen = strlen(lbuf);
    cur_len = mlen;
    t = lbuf + cur_len;

    set_channel_mode_flags(show_flags, chptr, source_p);
    members_ptr[0] = chptr->chanops.head;
    members_ptr[1] = chptr->voiced.head;
    members_ptr[2] = chptr->peons.head;
    members_ptr[3] = chptr->chanops_voiced.head;

    is_member = IsMember(source_p, chptr);

    /* Note: This code will show one chanop followed by one voiced followed
     * followed by one peon followed by one chanop...
     * XXX - this is very predictable, randomise it later.
     */

    while (sublists_done != (1 << NUMLISTS) - 1)
    {
      for (i = 0; i < NUMLISTS; i++)
      {
        if (members_ptr[i] != NULL)
        {
          who = members_ptr[i]->data;

          if (IsInvisible(who) && !is_member)
          {
            /* We definitely need this code -A1kmm. */
            members_ptr[i] = members_ptr[i]->next;
            continue;
          }

          reply_to_send = YES;

          if (who == source_p && is_voiced(chptr, who)
	      && !is_chan_op(chptr, who)
              && chptr->mode.mode & MODE_HIDEOPS)
            ircsprintf(t, "+%s ", who->name);
          else 
            ircsprintf(t, "%s%s ", show_flags[i], who->name);

          tlen = strlen(t);
          cur_len += tlen;
          t += tlen;
          if ((cur_len + NICKLEN) > (BUFSIZE - 3))
          {
            sendto_one(source_p, "%s", lbuf);
            reply_to_send = NO;
            cur_len = mlen;
            t = lbuf + mlen;
          }

          members_ptr[i] = members_ptr[i]->next;
        }
        else
        {
          sublists_done |= 1 << i;
        }
      }
    }
    if (reply_to_send)
      sendto_one(source_p, "%s", lbuf);
  }

  if (show_eon)
    sendto_one(source_p, form_str(RPL_ENDOFNAMES), me.name,
               source_p->name, name_of_channel);
}
#else
{
  struct Client *target_p;
  dlink_node *ptr_list[NUMLISTS];
  dlink_node *ptr;
  char ptr_flags[NUMLISTS][2];
  char lbuf[BUFSIZE];
  char *t;
  int mlen;
  int tlen;
  int cur_len;
  int reply_to_send = NO;
  int is_member;
  int i;

  if(ShowChannel(source_p, chptr))
  {
    ptr_list[0] = chptr->chanops.head;
    ptr_list[1] = chptr->voiced.head;
    ptr_list[2] = chptr->peons.head;
    ptr_list[3] = chptr->chanops_voiced.head;

    set_channel_mode_flags(ptr_flags, chptr, source_p);

    is_member = IsMember(source_p, chptr);

    ircsprintf(lbuf, form_str(RPL_NAMREPLY),
	       me.name, source_p->name, channel_pub_or_secret(chptr));

    mlen = strlen(lbuf);
    
    ircsprintf(lbuf + mlen, " %s :", name_of_channel);
    cur_len = mlen = strlen(lbuf);

    t = lbuf + cur_len;

    for(i = 0; i < NUMLISTS; i++)
    {
      for(ptr = ptr_list[i]; ptr; ptr = ptr->next)
      {
        target_p = ptr->data;

        if(IsInvisible(target_p) && !is_member)
          continue;

        reply_to_send = YES;

        ircsprintf(t, "%s%s ", ptr_flags[i], target_p->name);

        tlen = strlen(t);
        cur_len += tlen;
        t += tlen;

        if ((cur_len + NICKLEN) > (BUFSIZE - 3))
        {
          sendto_one(source_p, "%s", lbuf);
  	  reply_to_send = NO;
	  cur_len = mlen;
	  t = lbuf + mlen;
        }
      }
    }

    if(reply_to_send)
      sendto_one(source_p, "%s", lbuf);
  }

  if(show_eon)
    sendto_one(source_p, form_str(RPL_ENDOFNAMES), me.name,
	       source_p->name, name_of_channel);
}
#endif /* ANONOPS */     
      

/*
 * channel_pub_or_secret
 *
 * inputs       - pointer to channel
 * output       - string pointer "=" if public, "@" if secret else "*"
 * side effects - NONE
 */
char *
channel_pub_or_secret(struct Channel *chptr)
{
  if (PubChannel(chptr))
    return ("=");
  else if (SecretChannel(chptr))
    return ("@");
  return ("*");
}

/*
 * add_invite
 *
 * inputs       - pointer to channel block
 *              - pointer to client to add invite to
 * output       - none
 * side effects - adds client to invite list
 *
 * This one is ONLY used by m_invite.c
 */
void
add_invite(struct Channel *chptr, struct Client *who)
{

  del_invite(chptr, who);
  /*
   * delete last link in chain if the list is max length
   */
  if (dlink_list_length(&who->user->invited) >=
      ConfigChannel.max_chans_per_user)
  {
    del_invite(chptr, who);
  }
  /*
   * add client to channel invite list
   */
  dlinkAddAlloc(who, &chptr->invites);

  /*
   * add channel to the end of the client invite list
   */
  dlinkAddAlloc(chptr, &who->user->invited);
}

/*
 * del_invite
 *
 * inputs       - pointer to dlink_list
 *              - pointer to client to remove invites from
 * output       - none
 * side effects - Delete Invite block from channel invite list
 *                and client invite list
 *
 */
void
del_invite(struct Channel *chptr, struct Client *who)
{
  dlinkFindDestroy(&chptr->invites, who);
  dlinkFindDestroy(&who->user->invited, chptr);
}

/*
 * channel_chanop_or_voice
 * inputs       - pointer to channel
 *              - pointer to client
 * output       - string either @,+% or"" depending on whether
 *                chanop, voiced or user
 * side effects -
 */
char *
channel_chanop_or_voice(struct Channel *chptr, struct Client *target_p)
{
  if (find_user_link(&chptr->chanops, target_p))
    return ("@");
  else if (find_user_link(&chptr->voiced, target_p))
    return ("+");
  else if (find_user_link(&chptr->chanops_voiced, target_p))
    return ("@+");
  return ("");
}

/*
 * is_banned
 *
 * inputs       - pointer to channel block
 *              - pointer to client to check access fo
 * output       - returns an int 0 if not banned,
 *                CHFL_BAN if banned
 *
 * IP_BAN_ALL from comstud
 * always on...
 *
 * +e code from orabidoo
 */
int
is_banned(struct Channel *chptr, struct Client *who)
{
  char src_host[NICKLEN + USERLEN + HOSTLEN + 6];
  char src_iphost[NICKLEN + USERLEN + HOSTLEN + 6];

  if (!IsPerson(who))
    return (0);

  ircsprintf(src_host,"%s!%s@%s", who->name, who->username, who->host);
  ircsprintf(src_iphost,"%s!%s@%s", who->name, who->username,
	     who->localClient->sockhost);

  return (check_banned(chptr, who, src_host, src_iphost));
}

/*
 * check_banned
 *
 * inputs       - pointer to channel block
 *              - pointer to client to check access fo
 *              - pointer to pre-formed nick!user@host
 *              - pointer to pre-formed nick!user@ip
 * output       - returns an int 0 if not banned,
 *                CHFL_BAN if banned
 *
 * IP_BAN_ALL from comstud
 * always on...
 *
 * +e code from orabidoo
 */
static int
check_banned(struct Channel *chptr, struct Client *who, char *s, char *s2)
{
  dlink_node *ban;
  dlink_node *except;
  struct Ban *actualBan = NULL;
  struct Ban *actualExcept = NULL;

  DLINK_FOREACH(ban, chptr->banlist.head)
  {
    actualBan = ban->data;
    if (match(actualBan->banstr, s) || 
    	match(actualBan->banstr, s2) ||
        match_cidr(actualBan->banstr, s2))
      break;
    else
      actualBan = NULL;
  }

  if ((actualBan != NULL) && ConfigChannel.use_except)
  {
    DLINK_FOREACH(except, chptr->exceptlist.head)
    {
      actualExcept = except->data;

      if (match(actualExcept->banstr, s) || 
          match(actualExcept->banstr, s2) ||
          match_cidr(actualExcept->banstr, s2))
      {
        return CHFL_EXCEPTION;
      }
    }
  }

  return ((actualBan ? CHFL_BAN : 0));
}

/* small series of "helper" functions */

/*
 * can_join
 *
 * inputs       -
 * output       -
 * side effects - NONE
 */
int
can_join(struct Client *source_p, struct Channel *chptr, char *key)
{
  dlink_node *lp;
  dlink_node *ptr;
  struct Ban *invex = NULL;
  char src_host[NICKLEN + USERLEN + HOSTLEN + 6];
  char src_iphost[NICKLEN + USERLEN + HOSTLEN + 6];

  assert(source_p->localClient != NULL);

  ircsprintf(src_host,
	     "%s!%s@%s", source_p->name, source_p->username, source_p->host);
  ircsprintf(src_iphost,"%s!%s@%s", source_p->name, source_p->username,
	     source_p->localClient->sockhost);

  if ((check_banned(chptr, source_p, src_host, src_iphost)) == CHFL_BAN)
    return (ERR_BANNEDFROMCHAN);

  if (chptr->mode.mode & MODE_INVITEONLY)
  {
    DLINK_FOREACH(lp, source_p->user->invited.head)
    {
      if (lp->data == chptr)
        break;
    }
    if (lp == NULL)
    {
      if (!ConfigChannel.use_invex)
        return (ERR_INVITEONLYCHAN);
      DLINK_FOREACH(ptr, chptr->invexlist.head)
      {
        invex = ptr->data;
        if (match(invex->banstr, src_host) || match(invex->banstr, src_iphost) ||
            match_cidr(invex->banstr, src_iphost))
          break;
      }
      if (ptr == NULL)
        return (ERR_INVITEONLYCHAN);
    }
  }

  if (*chptr->mode.key && (BadPtr(key) || irccmp(chptr->mode.key, key)))
    return (ERR_BADCHANNELKEY);

  if (chptr->mode.limit && chptr->users >= chptr->mode.limit)
    return (ERR_CHANNELISFULL);

  return 0;
}

/*
 * is_chan_op
 *
 * inputs       - pointer to channel to check chanop on
 *              - pointer to client struct being checked
 * output       - yes if chanop no if not
 * side effects -
 */
int
is_chan_op(struct Channel *chptr, struct Client *who)
{
  if (chptr)
  {
    if (find_user_link(&chptr->chanops, who) != NULL)
      return 1;

    if (find_user_link(&chptr->chanops_voiced, who) != NULL)
      return 1;
  }
  return 0;
}

/*
 * is_voiced
 *
 * inputs       - pointer to channel to check voice on
 *              - pointer to client struct being checked
 * output       - yes if voiced no if not
 * side effects -
 */
int
is_voiced(struct Channel *chptr, struct Client *who)
{
  if (chptr)
  {
    if (find_user_link(&chptr->voiced, who) != NULL)
      return 1;
    if (find_user_link(&chptr->chanops_voiced, who) != NULL)
      return 1;
  }
  return 0;
}


/*
 * can_send
 *
 * inputs       - pointer to channel
 *              - pointer to client
 * outputs      - CAN_SEND_OPV if op or voiced on channel
 *              - CAN_SEND_NONOP if can send to channel but is not an op
 *                CAN_SEND_NO if they cannot send to channel
 *                Just means they can send to channel.
 * side effects - NONE
 */
int
can_send(struct Channel *chptr, struct Client *source_p)
{
  if(MyClient(source_p) && find_channel_resv(chptr->chname))
    return CAN_SEND_NO;
    
  if (is_chan_op(chptr, source_p))
    return CAN_SEND_OPV;
  if (is_voiced(chptr, source_p))
    return CAN_SEND_OPV;
  if (IsServer(source_p))
    return CAN_SEND_OPV;

  if (chptr->mode.mode & MODE_MODERATED)
    return CAN_SEND_NO;

  if (ConfigChannel.quiet_on_ban && MyClient(source_p) &&
      (is_banned(chptr, source_p) == CHFL_BAN))
  {
    return (CAN_SEND_NO);
  }

  if (chptr->mode.mode & MODE_NOPRIVMSGS && !IsMember(source_p, chptr))
    return (CAN_SEND_NO);

  return CAN_SEND_NONOP;
}

/* void check_spambot_warning(struct Client *source_p)
 * Input: Client to check, channel name or NULL if this is a part.
 * Output: none
 * Side-effects: Updates the client's oper_warn_count_down, warns the
 *    IRC operators if necessary, and updates join_leave_countdown as
 *    needed.
 */
void
check_spambot_warning(struct Client *source_p, const char *name)
{
  int t_delta;
  int decrement_count;
  if ((GlobalSetOptions.spam_num &&
       (source_p->localClient->join_leave_count >=
        GlobalSetOptions.spam_num)))
  {
    if (source_p->localClient->oper_warn_count_down > 0)
      source_p->localClient->oper_warn_count_down--;
    else
      source_p->localClient->oper_warn_count_down = 0;
    if (source_p->localClient->oper_warn_count_down == 0)
    {
      /* Its already known as a possible spambot */
      if (name != NULL)
        sendto_realops_flags(UMODE_BOTS, L_ALL,
                             "User %s (%s@%s) trying to join %s is a possible spambot",
                             source_p->name, source_p->username,
                             source_p->host, name);
      else
        sendto_realops_flags(UMODE_BOTS, L_ALL,
                             "User %s (%s@%s) is a possible spambot",
                             source_p->name, source_p->username,
                             source_p->host);
      source_p->localClient->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
    }
  }
  else
  {
    if ((t_delta = (CurrentTime - source_p->localClient->last_leave_time))
        > JOIN_LEAVE_COUNT_EXPIRE_TIME)
    {
      decrement_count = (t_delta / JOIN_LEAVE_COUNT_EXPIRE_TIME);
      if (decrement_count > source_p->localClient->join_leave_count)
        source_p->localClient->join_leave_count = 0;
      else
        source_p->localClient->join_leave_count -= decrement_count;
    }
    else
    {
      if ((CurrentTime - (source_p->localClient->last_join_time)) <
          GlobalSetOptions.spam_time)
      {
        /* oh, its a possible spambot */
        source_p->localClient->join_leave_count++;
      }
    }
    if (name != NULL)
      source_p->localClient->last_join_time = CurrentTime;
    else
      source_p->localClient->last_leave_time = CurrentTime;
  }
}

/* check_splitmode()
 *
 * input	-
 * output	-
 * side effects - compares usercount and servercount against their split
 *                values and adjusts splitmode accordingly
 */
void check_splitmode(void *unused)
{
  if(splitchecking && (ConfigChannel.no_join_on_split ||
     ConfigChannel.no_create_on_split))
  {
    if((Count.server < split_servers) &&
       (Count.total < split_users))
    {
      if(!splitmode)
      {
        splitmode = 1;

        sendto_realops_flags(UMODE_ALL,L_ALL,
                           "Network split, activating splitmode");
        eventAddIsh("check_splitmode", check_splitmode, NULL, 60);
      }
    }
    else if(splitmode)
    {
      splitmode = 0;
    
      sendto_realops_flags(UMODE_ALL, L_ALL,
                           "Network rejoined, deactivating splitmode");
      eventDelete(check_splitmode, NULL);
    }
  }
}


/*
 * input	- Channel to allocate a new topic for
 * output	- Success or failure
 * side effects - Allocates a new topic
 */

int allocate_topic(struct Channel *chptr)
{
  void *ptr;
  if(chptr == NULL)
    return FALSE;
  
  ptr = BlockHeapAlloc(topic_heap);  
  /* Basically we allocate one large block for the topic and
   * the topic info.  We then split it up into two and shove it
   * in the chptr 
   */
  chptr->topic = ptr;
  chptr->topic_info = (char *)ptr + TOPICLEN+1;
  *chptr->topic = '\0';
  *chptr->topic_info = '\0';
  return TRUE;

}

void free_topic(struct Channel *chptr)
{
  void *ptr;
  
  if(chptr == NULL)
    return;
  if(chptr->topic == NULL)
    return;
  /* This is safe for now - If you change allocate_topic you
   * MUST change this as well
   */
  ptr = chptr->topic; 
  BlockHeapFree(topic_heap, ptr);    
  chptr->topic = NULL;
  chptr->topic_info = NULL;
}

/*
 * set_channel_topic - Sets the channel topic
 */
void set_channel_topic(struct Channel *chptr, const char *topic, const char *topic_info, time_t topicts)
{
  if(strlen(topic) > 0)
  {
    if(chptr->topic == NULL)
      allocate_topic(chptr);
    strlcpy(chptr->topic, topic, TOPICLEN+1);
    strlcpy(chptr->topic_info, topic_info, USERHOST_REPLYLEN);
    chptr->topic_time = topicts; 
  } else
  {
    if(chptr->topic != NULL)
      free_topic(chptr);
    chptr->topic_time = 0;
  }
}

