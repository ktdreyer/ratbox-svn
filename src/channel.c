/************************************************************************
 *   IRC - Internet Relay Chat, src/channel.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Co Center
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
 * $Id$
 */
#include "tools.h"
#include "channel.h"
#include "vchannel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "s_serv.h"             /* captab */
#include "s_user.h"
#include "send.h"
#include "whowas.h"
#include "s_conf.h"             /* ConfigFileEntry, ConfigChannel */
#include "event.h"
#include "memory.h"
#include "balloc.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "s_log.h"

struct config_channel_entry ConfigChannel;

struct Channel *GlobalChannelList = NullChn;

static int add_id(struct Client *, struct Channel *, char *, int);
static int del_id(struct Channel *, char *, int);

static void free_channel_list(dlink_list * list);
static void sub1_from_channel(struct Channel *, int);
static void destroy_channel(struct Channel *);

static void send_mode_list(struct Client *, char *, dlink_list *, char, int);

static void send_oplist(const char *, struct Client *, dlink_list *,
                        char *, int);


static void send_members(struct Client *client_p,
                         char *modebuf, char *parabuf,
                         struct Channel *chptr,
                         dlink_list * list, char *op_flag);


static void delete_members(struct Channel *chptr, dlink_list * list);

static int check_banned(struct Channel *chptr, struct Client *who,
                        char *s, char *s2);

/* static functions used in set_channel_mode */
static char *pretty_mask(char *);
static char *fix_key(char *);
static char *fix_key_old(char *);
#ifndef USE_TABLE_MODE
static void collapse_signs(char *);
static int errsent(int, int *);
#endif

static int change_channel_membership(struct Channel *chptr,
                                     dlink_list * to_list,
                                     dlink_list * loc_to_list,
                                     struct Client *who);

/*
 * some buffers for rebuilding channel/nick lists with ,'s
 */

static char buf[BUFSIZE];
static char modebuf[MODEBUFLEN], parabuf[MODEBUFLEN];
#ifndef USE_TABLE_MODE
static char modebuf2[MODEBUFLEN], parabuf2[MODEBUFLEN];
static char parabuf_id[MODEBUFLEN], parabuf2_id[MODEBUFLEN];
#else
static char mask_buf[BUFSIZE];
static int mask_pos;
#endif
void check_spambot_warning(struct Client *source_p, const char *name);

/* 
 * init_channels
 *
 * Initializes the channel blockheap
 */
BlockHeap *channel_heap;
static BlockHeap *ban_heap;
static void channelheap_garbage_collect(void *unused)
{
  BlockHeapGarbageCollect(channel_heap);
  BlockHeapGarbageCollect(ban_heap);
}

void init_channels(void)
{
  channel_heap = BlockHeapCreate(sizeof(struct Channel), 2048);
  /* Dianora and I agree that 4 banids to a channel is a good 
   * rough guess. Actual stats anyone? -A1kmm. */
  ban_heap = BlockHeapCreate(sizeof(struct Ban), 16384);
  eventAddIsh("channelheap_garbage_collect", channelheap_garbage_collect,
              NULL, 45);
}

/*
 * check_string
 *
 * inputs       - string to check
 * output       - pointer to modified string
 * side effects - Fixes a string so that the first white space found
 *                becomes an end of string marker (`\0`).
 *                returns the 'fixed' string or "*" if the string
 *                was NULL length or a NULL pointer.
 */
static char *
check_string(char *s)
{
  char *str = s;

  if (s == NULL)
    return "*";

  for (; *s; ++s)
  {
    if (IsSpace(*s))
    {
      *s = '\0';
      break;
    }
  }
  return str;
}

/*
 * make_nick_user_host
 *
 * inputs       - pointer to location to place string
 *              - pointer to nick
 *              - pointer to name
 *              - pointer to host
 * side effects -
 * create a string of form "foo!bar@fubar" given foo, bar and fubar
 * as the parameters.  If NULL, they become "*".
 */
static void
make_nick_user_host(char *s,
                    const char *nick, const char *name, const char *host)
{
  int n;
  const char *p;

  for (p = nick, n = NICKLEN; *p && n--;)
    *s++ = *p++;
  *s++ = '!';
  for (p = name, n = USERLEN; *p && n--;)
    *s++ = *p++;
  *s++ = '@';
  for (p = host, n = HOSTLEN; *p && n--;)
    *s++ = *p++;
  *s = '\0';
}

/*
 * Ban functions to work with mode +b/e/d/I
 */
/* add the specified ID to the channel.. 
 *   -is 8/9/00 
 */

static int
add_id(struct Client *client_p, struct Channel *chptr, char *banid, int type)
{
  dlink_list *list;
  dlink_node *ban;
  struct Ban *actualBan;

  /* dont let local clients overflow the banlist */
  if ((!IsServer(client_p)) && (chptr->num_mask >= ConfigChannel.maxbans))
  {
    if (MyClient(client_p))
    {
      sendto_one(client_p, form_str(ERR_BANLISTFULL),
                 me.name, client_p->name, chptr->chname, banid);
      return -1;
    }
  }

  if (MyClient(client_p))
    collapse(banid);

  switch (type)
  {
    case CHFL_BAN:
      list = &chptr->banlist;
      break;
    case CHFL_EXCEPTION:
      list = &chptr->exceptlist;
      break;
    case CHFL_INVEX:
      list = &chptr->invexlist;
      break;
    default:
      sendto_realops_flags(FLAGS_ALL, L_ALL,
                           "add_id() called with unknown ban type %d!", type);
      return -1;
  }

  for (ban = list->head; ban; ban = ban->next)
  {
    actualBan = ban->data;
    if (match(actualBan->banstr, banid))
      return -1;
  }

  ban = make_dlink_node();

  actualBan = (struct Ban *)BlockHeapAlloc(ban_heap);
  DupString(actualBan->banstr, banid);

  if (IsPerson(client_p))
  {
    actualBan->who =
      (char *)MyMalloc(strlen(client_p->name) +
                       strlen(client_p->username) +
                       strlen(client_p->host) + 3);
    ircsprintf(actualBan->who, "%s!%s@%s",
               client_p->name, client_p->username, client_p->host);
  }
  else
  {
    DupString(actualBan->who, client_p->name);
  }

  actualBan->when = CurrentTime;

  dlinkAdd(actualBan, ban, list);

  chptr->num_mask++;
  return 0;
}

/*
 *
 * "del_id - delete an id belonging to client_p
 * if banid is null, deleteall banids belonging to client_p."
 *
 * from orabidoo
 * modified 8/9/00 by is: now we handle add ban types here
 * (invex/excemp/etc)
 */
static int
del_id(struct Channel *chptr, char *banid, int type)
{
  dlink_list *list;
  dlink_node *ban;
  struct Ban *banptr;

  if (!banid)
    return -1;

  switch (type)
  {
    case CHFL_BAN:
      list = &chptr->banlist;
      break;
    case CHFL_EXCEPTION:
      list = &chptr->exceptlist;
      break;
    case CHFL_INVEX:
      list = &chptr->invexlist;
      break;
    default:
      sendto_realops_flags(FLAGS_ALL, L_ALL,
                           "del_id() called with unknown ban type %d!", type);
      return -1;
  }

  for (ban = list->head; ban; ban = ban->next)
  {
    banptr = ban->data;

    if (irccmp(banid, banptr->banstr) == 0)
    {
      MyFree(banptr->banstr);
      MyFree(banptr->who);
      BlockHeapFree(ban_heap, banptr);

      /* num_mask should never be < 0 */
      if (chptr->num_mask > 0)
        chptr->num_mask--;
      else
        chptr->num_mask = 0;

      dlinkDelete(ban, list);
      free_dlink_node(ban);

      break;
    }
  }
  return 0;
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
  char s[NICKLEN + USERLEN + HOSTLEN + 6];
  char s2[NICKLEN + USERLEN + HOSTLEN + 6];

  if (!IsPerson(who))
    return (0);

  make_nick_user_host(s, who->name, who->username, who->host);
  make_nick_user_host(s2, who->name, who->username,
                      who->localClient->sockhost);

  return (check_banned(chptr, who, s, s2));
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

  for (ban = chptr->banlist.head; ban; ban = ban->next)
  {
    actualBan = ban->data;
    if (match(actualBan->banstr, s) || match(actualBan->banstr, s2))
      break;
    else
      actualBan = NULL;
  }

  if ((actualBan != NULL) && ConfigChannel.use_except)
  {
    for (except = chptr->exceptlist.head; except; except = except->next)
    {
      actualExcept = except->data;

      if (match(actualExcept->banstr, s) || match(actualExcept->banstr, s2))
      {
        return CHFL_EXCEPTION;
      }
    }
  }

  return ((actualBan ? CHFL_BAN : 0));
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
void
add_user_to_channel(struct Channel *chptr, struct Client *who, int flags)
{
  dlink_node *ptr;
  dlink_node *lptr = NULL;

  if (who->user)
  {
    ptr = make_dlink_node();
    if (MyClient(who))
      lptr = make_dlink_node();
    switch (flags)
    {
      default:
      case MODE_PEON:
        dlinkAdd(who, ptr, &chptr->peons);
        if (MyClient(who))
          dlinkAdd(who, lptr, &chptr->locpeons);
        break;

      case MODE_CHANOP:
        chptr->opcount++;
        dlinkAdd(who, ptr, &chptr->chanops);
        if (MyClient(who))
          dlinkAdd(who, lptr, &chptr->locchanops);
        break;

      case MODE_HALFOP:
        dlinkAdd(who, ptr, &chptr->halfops);
        if (MyClient(who))
          dlinkAdd(who, lptr, &chptr->lochalfops);
        break;

      case MODE_VOICE:
        dlinkAdd(who, ptr, &chptr->voiced);
        if (MyClient(who))
          dlinkAdd(who, lptr, &chptr->locvoiced);
        break;
    }

    chptr->users++;

    if (MyClient(who))
      chptr->locusers++;

    chptr->users_last = CurrentTime;

    ptr = make_dlink_node();
    dlinkAdd(chptr, ptr, &who->user->channel);
    who->user->joined++;
  }
}

/*
 * remove_user_from_channel
 * 
 * inputs       - pointer to channel to remove client from
 *              - pointer to client (who) to remove
 *              - integer: 1 - make persistant
 *                         0 - dont make persistant
 * output       - none
 * side effects - deletes an user from a channel by removing a link in the
 *                channels member chain.
 *                sets a vchan_id if the last user is just leaving
 */
void
remove_user_from_channel(struct Channel *chptr, struct Client *who, int perm)
{
  dlink_node *ptr;
  dlink_node *next_ptr;

  /* last user in the channel.. set a vchan_id incase we need it */
  if (chptr->users == 1)
    ircsprintf(chptr->vchan_id, "!%s", who->name);

  if ((ptr = find_user_link(&chptr->peons, who)))
    dlinkDelete(ptr, &chptr->peons);
  else if ((ptr = find_user_link(&chptr->chanops, who)))
  {
    chptr->opcount--;
    dlinkDelete(ptr, &chptr->chanops);
  }
  else if ((ptr = find_user_link(&chptr->voiced, who)))
    dlinkDelete(ptr, &chptr->voiced);
  else if ((ptr = find_user_link(&chptr->halfops, who)))
    dlinkDelete(ptr, &chptr->halfops);
  else
    return;                     /* oops */

  free_dlink_node(ptr);

  if (MyClient(who))
  {
    if ((ptr = find_user_link(&chptr->locpeons, who)))
      dlinkDelete(ptr, &chptr->locpeons);
    else if ((ptr = find_user_link(&chptr->locchanops, who)))
    {
      chptr->opcount--;
      dlinkDelete(ptr, &chptr->locchanops);
    }
    else if ((ptr = find_user_link(&chptr->locvoiced, who)))
      dlinkDelete(ptr, &chptr->locvoiced);
    else if ((ptr = find_user_link(&chptr->lochalfops, who)))
      dlinkDelete(ptr, &chptr->lochalfops);
    else
      return;                   /* XXX */

    free_dlink_node(ptr);
  }

  chptr->users_last = CurrentTime;

  for (ptr = who->user->channel.head; ptr; ptr = next_ptr)
  {
    next_ptr = ptr->next;

    if (ptr->data == chptr)
    {
      dlinkDelete(ptr, &who->user->channel);
      free_dlink_node(ptr);
      break;
    }
  }

  who->user->joined--;

  if (IsVchan(chptr))
    del_vchan_from_client_cache(who, chptr);

  if (MyClient(who))
  {
    if (chptr->locusers > 0)
      chptr->locusers--;
  }
  sub1_from_channel(chptr, perm);
}

/*
 * find_user_link
 * inputs       -
 *              - client pointer to find
 * output       - pointer to link or NULL if not found
 * side effects - Look for ptr in the linked listed pointed to by link.
 */
dlink_node *
find_user_link(dlink_list * list, struct Client *who)
{
  if (who)
    return (dlinkFind(list, who));
  return (NULL);
}

/*
 * change_channel_membership
 *
 * inputs       - pointer to channel
 *              - pointer to membership list of given channel to modify
 *              - pointer to membership list for local clients to modify 
 *              - pointer to client struct being modified
 * output       - int success 1 or 0 if failure
 * side effects - change given user "who" from whichever membership list
 *                it is on, to the given membership list in to_list.
 *                
 */
static int
change_channel_membership(struct Channel *chptr,
                          dlink_list * to_list,
                          dlink_list * loc_to_list, struct Client *who)
{
  dlink_node *ptr;
  int ok = 1;

  /* local clients need to be moved from local list too */
  if (MyClient(who))
  {
    if ((ptr = find_user_link(&chptr->locpeons, who)))
    {
      if (to_list != &chptr->locpeons)
      {
        dlinkDelete(ptr, &chptr->locpeons);
        dlinkAdd(who, ptr, loc_to_list);
      }
    }
    else if ((ptr = find_user_link(&chptr->locvoiced, who)))
    {
      if (to_list != &chptr->locvoiced)
      {
        dlinkDelete(ptr, &chptr->locvoiced);
        dlinkAdd(who, ptr, loc_to_list);
      }
    }
    else if ((ptr = find_user_link(&chptr->lochalfops, who)))
    {
      if (to_list != &chptr->lochalfops)
      {
        dlinkDelete(ptr, &chptr->lochalfops);
        dlinkAdd(who, ptr, loc_to_list);
      }
    }
    else if ((ptr = find_user_link(&chptr->locchanops, who)))
    {
      if (to_list != &chptr->locchanops)
      {
        dlinkDelete(ptr, &chptr->locchanops);
        dlinkAdd(who, ptr, loc_to_list);
      }
    }
    else
      ok = 0;
  }

  if ((ptr = find_user_link(&chptr->peons, who)))
  {
    if (to_list != &chptr->peons)
    {
      dlinkDelete(ptr, &chptr->peons);
      dlinkAdd(who, ptr, to_list);
    }
  }
  else if ((ptr = find_user_link(&chptr->voiced, who)))
  {
    if (to_list != &chptr->voiced)
    {
      dlinkDelete(ptr, &chptr->voiced);
      dlinkAdd(who, ptr, to_list);
    }
  }
  else if ((ptr = find_user_link(&chptr->halfops, who)))
  {
    if (to_list != &chptr->halfops)
    {
      dlinkDelete(ptr, &chptr->halfops);
      dlinkAdd(who, ptr, to_list);
    }
  }
  else if ((ptr = find_user_link(&chptr->chanops, who)))
  {
    if (to_list != &chptr->chanops)
    {
      dlinkDelete(ptr, &chptr->chanops);
      dlinkAdd(who, ptr, to_list);
    }
  }
  else
    ok = 0;

  return ok;
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
  char s[NICKLEN + USERLEN + HOSTLEN + 6];
  char s2[NICKLEN + USERLEN + HOSTLEN + 6];

  assert(source_p->localClient != NULL);

  make_nick_user_host(s, source_p->name, source_p->username, source_p->host);
  make_nick_user_host(s2, source_p->name, source_p->username,
                      source_p->localClient->sockhost);

  if ((check_banned(chptr, source_p, s, s2)) == CHFL_BAN)
    return (ERR_BANNEDFROMCHAN);

  if (chptr->mode.mode & MODE_INVITEONLY)
  {
    for (lp = source_p->user->invited.head; lp; lp = lp->next)
      if (lp->data == chptr)
        break;
    if (!lp)
    {
      if (!ConfigChannel.use_invex)
        return (ERR_INVITEONLYCHAN);
      for (ptr = chptr->invexlist.head; ptr; ptr = ptr->next)
      {
        invex = ptr->data;
        if (match(invex->banstr, s) || match(invex->banstr, s2))
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
    if ((find_user_link(&chptr->chanops, who)))
      return (1);
  }

  return 0;
}

/*
 * is_any_op
 *
 * inputs       - pointer to channel to check for chanop or halfops on
 *              - pointer to client struct being checked
 * output       - yes if anyop no if not
 * side effects -
 */
int
is_any_op(struct Channel *chptr, struct Client *who)
{
  if (chptr)
  {
    if ((find_user_link(&chptr->chanops, who)))
      return (1);
    if ((find_user_link(&chptr->halfops, who)))
      return (1);
  }

  return 0;
}

/*
 * is_half_op
 *
 * inputs       - pointer to channel to check for chanop or halfops on
 *              - pointer to client struct being checked
 * output       - yes if anyop no if not
 * side effects -
 */
int
is_half_op(struct Channel *chptr, struct Client *who)
{
  if (chptr)
  {
    if ((find_user_link(&chptr->halfops, who)))
      return (1);
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
    if ((find_user_link(&chptr->voiced, who)))
      return (1);
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
  if (is_any_op(chptr, source_p))
    return CAN_SEND_OPV;
  if (is_voiced(chptr, source_p))
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

/*
 * channel_modes
 * inputs       - pointer to channel
 *              - pointer to client
 *              - pointer to mode buf
 *              - pointer to parameter buf
 * output       - NONE
 * side effects - write the "simple" list of channel modes for channel
 * chptr onto buffer mbuf with the parameters in pbuf.
 */
void
channel_modes(struct Channel *chptr, struct Client *client_p,
              char *mbuf, char *pbuf)
{
  *mbuf++ = '+';
  *pbuf = '\0';

  if (chptr->mode.mode & MODE_SECRET)
    *mbuf++ = 's';
  if (chptr->mode.mode & MODE_PRIVATE)
    *mbuf++ = 'p';
  if (chptr->mode.mode & MODE_MODERATED)
    *mbuf++ = 'm';
  if (chptr->mode.mode & MODE_TOPICLIMIT)
    *mbuf++ = 't';
  if (chptr->mode.mode & MODE_INVITEONLY)
    *mbuf++ = 'i';
  if (chptr->mode.mode & MODE_NOPRIVMSGS)
    *mbuf++ = 'n';
  if (chptr->mode.mode & MODE_HIDEOPS)
    *mbuf++ = 'a';
  if (chptr->mode.limit)
  {
    *mbuf++ = 'l';
    if (IsMember(client_p, chptr) || IsServer(client_p))
      ircsprintf(pbuf, "%d ", chptr->mode.limit);
  }
  if (*chptr->mode.key)
  {
    *mbuf++ = 'k';
    if (IsMember(client_p, chptr) || IsServer(client_p))
      (void)strcat(pbuf, chptr->mode.key);
  }

  *mbuf++ = '\0';
  return;
}

/*
 * send_mode_list
 * inputs       - client pointer to server
 *              - pointer to channel
 *              - pointer to top of mode link list to send
 *              - char flag flagging type of mode i.e. 'b' 'e' etc.
 *              - clear (remove all current modes, for ophiding, etc)
 * output       - NONE
 * side effects - sends +b/+e/+I
 *
 */
static void
send_mode_list(struct Client *client_p,
               char *chname, dlink_list * top, char flag, int clear)
{
  dlink_node *lp;
  struct Ban *banptr;
  char mbuf[MODEBUFLEN];
  char pbuf[MODEBUFLEN];
  int tlen;
  int mlen;
  int cur_len;
  char *mp;
  char *pp;
  int count;

  ircsprintf(buf, ":%s MODE %s ", me.name, chname);
  cur_len = mlen = (strlen(buf) + 2);
  count = 0;
  mp = mbuf;
  *mp++ = (clear ? '-' : '+');
  *mp = '\0';
  pp = pbuf;

  for (lp = top->head; lp; lp = lp->next)
  {
    banptr = lp->data;
    tlen = strlen(banptr->banstr);
    tlen++;

    if ((count >= MAXMODEPARAMS) || ((cur_len + tlen + 2) > MODEBUFLEN))
    {
      sendto_one(client_p, "%s%s %s", buf, mbuf, pbuf);
      mp = mbuf;
      *mp++ = (clear ? '-' : '+');
      *mp = '\0';
      pp = pbuf;
      cur_len = mlen;
      count = 0;
    }

    *mp++ = flag;
    *mp = '\0';
    ircsprintf(pp, "%s ", banptr->banstr);
    pp += tlen;
    cur_len += tlen;
    count++;
  }

  if (count != 0)
    sendto_one(client_p, "%s%s %s", buf, mbuf, pbuf);
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

  if (IsCapable(client_p, CAP_HOPS))
    send_members(client_p, modebuf, parabuf, chptr, &chptr->halfops, "%");
  else
  {
    /* Ok, halfops can still generate a kick, they'll just looked opped */
    send_members(client_p, modebuf, parabuf, chptr, &chptr->halfops, "@");
  }

  send_members(client_p, modebuf, parabuf, chptr, &chptr->voiced, "+");
  send_members(client_p, modebuf, parabuf, chptr, &chptr->peons, "");

  send_mode_list(client_p, chptr->chname, &chptr->banlist, 'b', 0);

  if (IsCapable(client_p, CAP_EX))
    send_mode_list(client_p, chptr->chname, &chptr->exceptlist, 'e', 0);

  if (IsCapable(client_p, CAP_IE))
    send_mode_list(client_p, chptr->chname, &chptr->invexlist, 'I', 0);
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

  ircsprintf(buf, ":%s SJOIN %lu %s %s %s :", me.name,
             (unsigned long)chptr->channelts,
             chptr->chname, lmodebuf, lparabuf);

  cur_len = mlen = strlen(buf);
  t = buf + mlen;

  for (ptr = list->head; ptr && ptr->data; ptr = ptr->next)
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

#ifdef USE_TABLE_MODE
/* static char *pretty_mask(char *mask);
 * Input: A mask.
 * Output: A "user-friendly" version of the mask, in mask_buf.
 * Side-effects: mask_buf is appended to. mask_pos is incremented.
 * Notes: The following transitions are made:
 *  x!y@z =>  x!y@z
 *  y@z   =>  *!y@z
 *  x!y   =>  x!y@*
 *  x     =>  x!*@*
 *  z.d   =>  *!*@z.d
 */
static char *
pretty_mask(char *mask)
{
  int old_mask_pos;
  char *nick = "*", *user = "*", *host = "*";
  char *t;
  mask = check_string(mask);

  if (BUFSIZE - mask_pos < strlen(mask) + 5)
    return NULL;

  old_mask_pos = mask_pos;

  if ((t = strchr(mask, '@')) != NULL)
  {
    *t++ = '\0';
    host = t;

    if ((t = strchr(mask, '!')) != NULL)
    {
      *t++ = '\0';
      user = t;
      nick = mask;
    }
    else
      user = mask;
  }
  else if ((t = strchr(mask, '!')) != NULL)
  {
    *t++ = '\0';
    nick = mask;
    user = t;
  }
  else if (strchr(mask, '.') != NULL && strchr(mask, ':') != NULL)
    host = mask;
  else
    nick = mask;

  mask_pos += ircsprintf(mask_buf + mask_pos, "%s!%s@%s", nick, user, host)
    + 1;

  return mask_buf + old_mask_pos;
}
#else
/*
 * pretty_mask
 * 
 * inputs       - pointer string
 * output       - pointer to cleaned up mask
 * side effects - NONE
 *
 * stolen from Undernet's ircd  -orabidoo
 */
static char *
pretty_mask(char *mask)
{
  static char s[NICKLEN + USERLEN + HOSTLEN + 6];
  char *cp = mask;
  char *user;
  char *host;

  if ((user = strchr(cp, '!')))
    *user++ = '\0';

  if ((host = strrchr(user ? user : cp, '@')))
  {
    *host++ = '\0';
    if (!user)
    {
      make_nick_user_host(s, "*", check_string(cp), check_string(host));
      return (s);
    }

  }
  else if (!user && strchr(cp, '.'))
  {
    make_nick_user_host(s, "*", "*", check_string(cp));
    return (s);
  }

  make_nick_user_host(s, check_string(cp), check_string(user),
                      check_string(host));
  return (s);
}
#endif

/*
 * fix_key
 * 
 * inputs       - pointer to key to clean up
 * output       - pointer to cleaned up key
 * side effects - input string is modified
 *
 * stolen from Undernet's ircd  -orabidoo
 */
static char *
fix_key(char *arg)
{
  u_char *s, *t, c;

  for (s = t = (u_char *) arg; (c = *s); s++)
  {
    c &= 0x7f;
    if (c != ':' && c > ' ')
      *t++ = c;
  }
  *t = '\0';
  return arg;
}

/*
 * fix_key_old
 * 
 * inputs       - pointer to key to clean up
 * output       - pointer to cleaned up key
 * side effects - input string is modifed 
 *
 * Here we attempt to be compatible with older non-hybrid servers.
 * We can't back down from the ':' issue however.  --Rodder
 */
static char *
fix_key_old(char *arg)
{
  u_char *s, *t, c;

  for (s = t = (u_char *) arg; (c = *s); s++)
  {
    c &= 0x7f;
    if ((c != 0x0a) && (c != ':') && (c != 0x0d))
      *t++ = c;
  }
  *t = '\0';
  return arg;
}

#ifndef USE_TABLE_MODE
/*
 * collapse_signs
 * 
 * inputs       - pointer to signs to collapse
 * output       - pointer to collapsed string
 * side effects - 
 * like the name says...  take out the redundant signs in a modechange list
 */
static void
collapse_signs(char *s)
{
  char plus = '\0', *t = s, c;
  while ((c = *s++))
  {
    if (c != plus)
      *t++ = c;
    if (c == '+' || c == '-')
      plus = c;
  }
  *t = '\0';
}

/* little helper function to avoid returning duplicate errors */
static int
errsent(int err, int *errs)
{
  if (err & *errs)
    return 1;
  *errs |= err;
  return 0;
}
#endif

/* bitmasks for various error returns that set_channel_mode should only return
 * once per call  -orabidoo
 */

#define SM_ERR_NOTS             0x00000001      /* No TS on channel */
#define SM_ERR_NOOPS            0x00000002      /* No chan ops */
#define SM_ERR_UNKNOWN          0x00000004
#define SM_ERR_RPL_C            0x00000008
#define SM_ERR_RPL_B            0x00000010
#define SM_ERR_RPL_E            0x00000020
#define SM_ERR_NOTONCHANNEL     0x00000040      /* Not on channel */
#define SM_ERR_RESTRICTED       0x00000080      /* Restricted chanop */
#define SM_ERR_RPL_I            0x00000100
#define SM_ERR_RPL_D            0x00000200

#ifdef USE_TABLE_MODE

/* Now lets do some stuff to keep track of what combinations of
 * servers exist...
 * Note that the number of combinations doubles each time you add
 * something to this list. Each one is only quick if no servers use that
 * combination, but if the numbers get too high here MODE will get too
 * slow. I suggest if you get more than 7 here, you consider getting rid
 * of some and merging or something. If it wasn't for irc+cs we would
 * probably not even need to bother about most of these, but unfortunately
 * we do. -A1kmm
 */
int channel_capabs[] = { CAP_HOPS, CAP_AOPS, CAP_EX, CAP_IE, CAP_UID };
#define NCHCAP_COMS (1 << (sizeof(channel_capabs)/sizeof(int)))
int chcap_usage_counts[NCHCAP_COMS], chcap_yes[NCHCAP_COMS],
  chcap_no[NCHCAP_COMS];

/* void init_chcap_usage_counts(void)
 * Input: none
 * Output: none
 * Side-effects: Initialises the usage counts to zero. Fills in the
 *               chcap_yes and chcap_no combination tables.
 */
void
init_chcap_usage_counts(void)
{
  unsigned long m, c, y, n;
  memset(chcap_usage_counts, 0, sizeof(chcap_usage_counts));
  for (m = 0; m < NCHCAP_COMS; m++)
  {
    for (c = y = n = 0; c < (sizeof(channel_capabs) / sizeof(int)); c++)
      if ((m & (1 << c)) == 0)
        n |= channel_capabs[c];
      else
        y |= channel_capabs[c];
    chcap_yes[m] = y;
    chcap_no[m] = n;
  }
}

/* void set_chcap_usage_counts(struct Client *serv_p)
 * Input: serv_p; The client whose capabs to register.
 * Output: none
 * Side-effects: Increments the usage counts for the correct capab
 *               combination.
 */
void
set_chcap_usage_counts(struct Client *serv_p)
{
  int n;
  for (n = 0; n < NCHCAP_COMS; n++)
  {
    if (((serv_p->localClient->caps & chcap_yes[n]) == chcap_yes[n]) &&
        ((serv_p->localClient->caps & chcap_no[n]) == 0))
    {
      chcap_usage_counts[n]++;
      return;
    }
  }
  /* This should be impossible -A1kmm. */
  assert(0);
}

/* void set_chcap_usage_counts(struct Client *serv_p)
 * Input: serv_p; The client whose capabs to register.
 * Output: none
 * Side-effects: Decrements the usage counts for the correct capab
 *               combination.
 */
void
unset_chcap_usage_counts(struct Client *serv_p)
{
  int n;
  for (n = 0; n < NCHCAP_COMS; n++)
    if ((serv_p->localClient->caps & chcap_yes[n]) == chcap_yes[n] &&
        (serv_p->localClient->caps & chcap_no[n]) == 0)
    {
      /* Hopefully capabs can't change dynamically or anything... */
      assert(chcap_usage_counts[n] > 0);
      chcap_usage_counts[n]--;
      return;
    }
  /* This should be impossible -A1kmm. */
  assert(0);
}

struct ChModeChange mode_changes_plus[BUFSIZE], mode_changes_minus[BUFSIZE];
struct ChResyncOp resync_ops[BUFSIZE];
int mode_count_plus, mode_count_minus, resync_count;
int mode_limit;
static void chm_nosuch(struct Client *, struct Client *,
                       struct Channel *, int, int *, char **, int *, int,
                       int, char, void *, const char *chname);
static void chm_simple(struct Client *, struct Client *, struct Channel *,
                       int, int *, char **, int *, int, int, char, void *,
                       const char *chname);
static void chm_limit(struct Client *, struct Client *, struct Channel *,
                      int, int *, char **, int *, int, int, char, void *,
                      const char *chname);
static void chm_key(struct Client *, struct Client *, struct Channel *,
                    int, int *, char **, int *, int, int, char, void *,
                    const char *chname);
static void chm_op(struct Client *, struct Client *, struct Channel *, int,
                   int *, char **, int *, int, int, char, void *,
                   const char *chname);
static void chm_halfop(struct Client *, struct Client *, struct Channel *,
                       int, int *, char **, int *, int, int, char, void *,
                       const char *chname);
static void chm_voice(struct Client *, struct Client *, struct Channel *,
                      int, int *, char **, int *, int, int, char, void *,
                      const char *chname);
static void chm_ban(struct Client *, struct Client *, struct Channel *, int,
                    int *, char **, int *, int, int, char, void *,
                    const char *chname);
static void chm_except(struct Client *, struct Client *, struct Channel *,
                       int, int *, char **, int *, int, int, char, void *,
                       const char *chname);
static void chm_invex(struct Client *, struct Client *, struct Channel *,
                      int, int *, char **, int *, int, int, char, void *,
                      const char *chname);
static void chm_hideops(struct Client *, struct Client *, struct Channel *,
                        int, int *, char **, int *, int, int, char, void *,
                        const char *chname);
static void send_cap_mode_changes(struct Client *, struct Client *,
                                  struct Channel *, int, int);
static void send_mode_changes(struct Client *, struct Client *,
                              struct Channel *, char *chname);
static int get_channel_access(struct Client *, struct Channel *);


/* Mode functions handle mode changes for a particular mode... */
static void
chm_nosuch(struct Client *client_p, struct Client *source_p,
           struct Channel *chptr, int parc, int *parn,
           char **parv, int *errors, int alev, int dir, char c, void *d,
           const char *chname)
{
  if (*errors & SM_ERR_UNKNOWN)
    return;
  *errors |= SM_ERR_UNKNOWN;
  sendto_one(source_p, form_str(ERR_UNKNOWNMODE), me.name, source_p->name, c);
}

static void
chm_simple(struct Client *client_p, struct Client *source_p,
           struct Channel *chptr, int parc, int *parn,
           char **parv, int *errors, int alev, int dir, char c, void *d,
           const char *chname)
{
  long mode_type;
  int i;

  if (alev < CHACCESS_CHANOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  mode_type = (long)d;

  /* setting + */
  if (dir < 0 && !(chptr->mode.mode & mode_type))
  {
    chptr->mode.mode |= mode_type;

    for (i = 0; i < mode_count_minus; i++)
      if (mode_changes_minus[i].letter == c)
      {
        /* + and - make 0, so return... */
        mode_changes_minus[i].letter = 0;
        return;
      }

    mode_changes_plus[mode_count_plus].letter = c;
    mode_changes_plus[mode_count_plus].caps = 0;
    mode_changes_plus[mode_count_plus].nocaps = 0;
    mode_changes_plus[mode_count_plus].id = NULL;
    mode_changes_plus[mode_count_plus].mems = ALL_MEMBERS;
    mode_changes_plus[mode_count_plus++].arg = NULL;
  }
  /* setting - */
  else if (dir > 0 && (chptr->mode.mode & mode_type))
  {
    chptr->mode.mode &= ~mode_type;

    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c)
      {
        /* - and + make 0, so return... */
        mode_changes_plus[i].letter = 0;
        return;
      }

    mode_changes_minus[mode_count_minus].letter = c;
    mode_changes_minus[mode_count_minus].caps = 0;
    mode_changes_minus[mode_count_minus].nocaps = 0;
    mode_changes_minus[mode_count_minus].mems = ALL_MEMBERS;
    mode_changes_minus[mode_count_minus].id = NULL;
    mode_changes_minus[mode_count_minus++].arg = NULL;
  }
}

static void
chm_hideops(struct Client *client_p, struct Client *source_p,
            struct Channel *chptr, int parc, int *parn,
            char **parv, int *errors, int alev, int dir, char c, void *d,
            const char *chname)
{
  int i;

  if (alev < CHACCESS_CHANOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir < 0 && !(chptr->mode.mode & MODE_HIDEOPS))
  {
    chptr->mode.mode |= MODE_HIDEOPS;
    for (i = 0; i < mode_count_minus; i++)
      if (mode_changes_minus[i].letter == c)
      {
        /* + and - make 0, so return... */
        mode_changes_minus[i].letter = 0;
        return;
      }

    mode_changes_plus[mode_count_plus].letter = c;
    mode_changes_plus[mode_count_plus].caps = CAP_AOPS;
    mode_changes_plus[mode_count_plus].nocaps = 0;
    mode_changes_plus[mode_count_plus].mems = ALL_MEMBERS;
    mode_changes_plus[mode_count_plus].id = NULL;
    mode_changes_plus[mode_count_plus++].arg = NULL;
  }
  else if (dir > 0 && (chptr->mode.mode & MODE_HIDEOPS))
  {
    chptr->mode.mode &= ~MODE_HIDEOPS;
    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c)
      {
        /* + and - make 0, so return... */
        mode_changes_plus[i].letter = 0;
        return;
      }

    mode_changes_minus[mode_count_minus].letter = c;
    mode_changes_minus[mode_count_minus].caps = CAP_AOPS;
    mode_changes_minus[mode_count_minus].nocaps = 0;
    mode_changes_minus[mode_count_minus].mems = ALL_MEMBERS;
    mode_changes_minus[mode_count_minus].id = NULL;
    mode_changes_minus[mode_count_minus++].arg = NULL;
    sync_channel_oplists(chptr, 0);
  }
}

static void
chm_ban(struct Client *client_p, struct Client *source_p,
        struct Channel *chptr, int parc, int *parn,
        char **parv, int *errors, int alev, int dir, char c, void *d,
        const char *chname)
{
  int i;
  char *mask;
  dlink_node *ptr;
  struct Ban *banptr;

  if (dir == 0 || parc <= *parn)
  {
    if ((*errors & SM_ERR_RPL_B) != 0)
      return;
    *errors |= SM_ERR_RPL_B;

    if ((chptr->mode.mode & MODE_HIDEOPS) && alev < CHACCESS_CHANOP)
      for (ptr = chptr->banlist.head; ptr; ptr = ptr->next)
      {
        banptr = ptr->data;
        sendto_one(client_p, form_str(RPL_BANLIST),
                   me.name, client_p->name, chname,
                   banptr->banstr, me.name, banptr->when);
      }
    else
      for (ptr = chptr->banlist.head; ptr; ptr = ptr->next)
      {
        banptr = ptr->data;
        sendto_one(client_p, form_str(RPL_BANLIST),
                   me.name, client_p->name, chname,
                   banptr->banstr, banptr->who, banptr->when);
      }
    sendto_one(source_p, form_str(RPL_ENDOFBANLIST), me.name,
               source_p->name, chname);
    return;
  }

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (!IsServer(source_p) && !MyConnect(source_p) && mode_limit++ > 4)
    return;

  mask = pretty_mask(parv[(*parn)++]);
  if (strlen(mask) > HOSTLEN+NICKLEN+USERLEN)
   return;
  if (dir < 0 && add_id(source_p, chptr, mask, CHFL_BAN) == 0)
  {
    for (i = 0; i < mode_count_minus; i++)
      if (mode_changes_minus[i].letter == c &&
          !irccmp(mode_changes_minus[i].arg, mask))
      {
        mode_changes_minus[i].letter = 0;
        return;
      }

    mode_changes_plus[mode_count_plus].letter = c;
    mode_changes_plus[mode_count_plus].caps = 0;
    mode_changes_plus[mode_count_plus].nocaps = 0;
    mode_changes_plus[mode_count_plus].mems = ALL_MEMBERS;
    mode_changes_plus[mode_count_plus].id = NULL;
    mode_changes_plus[mode_count_plus++].arg = mask;
  }
  else if (dir > 0 && del_id(chptr, mask, CHFL_BAN) == 0)
  {
    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c &&
          !irccmp(mode_changes_plus[i].arg, mask))
      {
        mode_changes_plus[i].letter = 0;
        return;
      }

    mode_changes_minus[mode_count_minus].letter = c;
    mode_changes_minus[mode_count_minus].caps = 0;
    mode_changes_minus[mode_count_minus].nocaps = 0;
    mode_changes_minus[mode_count_minus].mems = ALL_MEMBERS;
    mode_changes_minus[mode_count_minus].id = NULL;
    mode_changes_minus[mode_count_minus++].arg = mask;
  }
}

static void
chm_except(struct Client *client_p, struct Client *source_p,
           struct Channel *chptr, int parc, int *parn,
           char **parv, int *errors, int alev, int dir, char c, void *d,
           const char *chname)
{
  int i;
  dlink_node *ptr;
  struct Ban *banptr;
  char *mask;

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == 0 || parc <= *parn)
  {
    if ((*errors & SM_ERR_RPL_E) != 0)
      return;
    *errors |= SM_ERR_RPL_E;

    for (ptr = chptr->exceptlist.head; ptr; ptr = ptr->next)
    {
      banptr = ptr->data;
      sendto_one(client_p, form_str(RPL_EXCEPTLIST),
                 me.name, client_p->name, chname,
                 banptr->banstr, banptr->who, banptr->when);
    }
    sendto_one(source_p, form_str(RPL_ENDOFEXCEPTLIST), me.name,
               source_p->name, chname);
    return;
  }

  if (!IsServer(source_p) && !MyConnect(source_p) && mode_limit++ > 4)
    return;

  mask = pretty_mask(parv[(*parn)++]);
  if (strlen(mask) > NICKLEN+USERLEN+HOSTLEN)
    return;
  if (dir < 0 && add_id(source_p, chptr, mask, CHFL_EXCEPTION) == 0)
  {
    for (i = 0; i < mode_count_minus; i++)
      if (mode_changes_minus[i].letter == c &&
          !irccmp(mode_changes_minus[i].arg, mask))
      {
        mode_changes_minus[i].letter = 0;
        return;
      }

    mode_changes_plus[mode_count_plus].letter = c;
    mode_changes_plus[mode_count_plus].caps = CAP_EX;
    mode_changes_plus[mode_count_plus].nocaps = 0;
    mode_changes_plus[mode_count_plus].mems = ONLY_CHANOPS_HALFOPS;
    mode_changes_plus[mode_count_plus].id = NULL;
    mode_changes_plus[mode_count_plus++].arg = mask;
  }
  else if (dir > 0 && del_id(chptr, mask, CHFL_EXCEPTION) == 0)
  {
    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c &&
          !irccmp(mode_changes_plus[i].arg, mask))
      {
        mode_changes_plus[i].letter = 0;
        return;
      }

    mode_changes_minus[mode_count_minus].letter = c;
    mode_changes_minus[mode_count_minus].caps = CAP_EX;
    mode_changes_minus[mode_count_minus].nocaps = 0;
    mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
    mode_changes_minus[mode_count_minus].id = NULL;
    mode_changes_minus[mode_count_minus++].arg = mask;
  }
}

static void
chm_invex(struct Client *client_p, struct Client *source_p,
          struct Channel *chptr, int parc, int *parn,
          char **parv, int *errors, int alev, int dir, char c, void *d,
          const char *chname)
{
  int i;
  char *mask;
  dlink_node *ptr;
  struct Ban *banptr;

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == 0 || parc <= *parn)
  {
    if ((*errors & SM_ERR_RPL_I) != 0)
      return;
    *errors |= SM_ERR_RPL_I;

    for (ptr = chptr->invexlist.head; ptr; ptr = ptr->next)
    {
      banptr = ptr->data;
      sendto_one(client_p, form_str(RPL_INVITELIST), me.name,
                 client_p->name, chname, banptr->banstr,
                 banptr->who, banptr->when);
    }
    sendto_one(source_p, form_str(RPL_ENDOFINVITELIST), me.name,
               source_p->name, chname);
    return;
  }

  if (!IsServer(source_p) && !MyConnect(source_p) && mode_limit++ > 4)
    return;

  mask = pretty_mask(parv[(*parn)++]);
  if (strlen(mask) > NICKLEN+USERLEN+HOSTLEN)
    return;
  if (dir < 0 && add_id(source_p, chptr, mask, CHFL_INVEX) == 0)
  {
    for (i = 0; i < mode_count_minus; i++)
      if (mode_changes_minus[i].letter == c &&
          !irccmp(mode_changes_minus[i].arg, mask))
      {
        /* It cancels out, just return... */
        mode_changes_minus[i].letter = 0;
        return;
      }

    mode_changes_plus[mode_count_plus].letter = c;
    mode_changes_plus[mode_count_plus].caps = CAP_IE;
    mode_changes_plus[mode_count_plus].nocaps = 0;
    mode_changes_plus[mode_count_plus].mems = ONLY_CHANOPS_HALFOPS;
    mode_changes_plus[mode_count_plus].id = NULL;
    mode_changes_plus[mode_count_plus++].arg = mask;
  }
  else if (dir > 0 && del_id(chptr, mask, CHFL_INVEX) == 0)
  {
    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c &&
          !irccmp(mode_changes_plus[i].arg, mask))
      {
        /* It cancels out, so just return... */
        mode_changes_plus[i].letter = 0;
        return;
      }

    mode_changes_minus[mode_count_minus].letter = c;
    mode_changes_minus[mode_count_minus].caps = CAP_IE;
    mode_changes_minus[mode_count_minus].nocaps = 0;
    mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
    mode_changes_minus[mode_count_minus].id = NULL;
    mode_changes_minus[mode_count_minus++].arg = mask;
  }
}

static void
chm_op(struct Client *client_p, struct Client *source_p,
       struct Channel *chptr, int parc, int *parn,
       char **parv, int *errors, int alev, int dir, char c, void *d,
       const char *chname)
{
  int i;
  /* Note on was_opped etc...
   * The was_opped variable is set to -1 if they were set -o in this mode,
   * was implies that previously they were +o, so we should not send a
   * +o out. wasnt_opped is set to -1 if they were +o in this mode, which
   * implies they were previously -o so we don't send -o out. Note that
   * was_hopped and was_voiced are along with is_half_op/is_voiced to
   * decide if we need to -h/-v first to support servers/clients that
   * allow more than one +h/+v/+o at a time.
   * -A1kmm.
   */
  int was_opped = 0, was_hopped = 0, was_voiced = 0, wasnt_opped = 0,
    wasnt_hopped = 0, t_op = 0, t_voice = 0, t_hop = 0;
  char *opnick;
  struct Client *targ_p;

  if (alev < CHACCESS_CHANOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == 0 || parc <= *parn)
    return;

  opnick = parv[(*parn)++];

  if ((targ_p = find_client(opnick, NULL)) == NULL)
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one(source_p, form_str(ERR_NOSUCHNICK), me.name,
                 source_p->name, opnick);
    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if (!IsMember(targ_p, chptr))
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one(source_p, form_str(ERR_USERNOTINCHANNEL), me.name,
                 source_p->name, chname, opnick);
    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if ((t_op = is_chan_op(chptr, targ_p)) != 0)
    ;
  else if ((t_hop = is_half_op(chptr, targ_p)) != 0)
    ;
  else
    t_voice = is_voiced(chptr, targ_p);

  if ((dir < 0 && t_op) || (dir > 0 && !t_op))
    return;

  if (!IsServer(source_p) && MyConnect(source_p) && mode_limit++ > 4)
    return;

  /* Cancel mode changes... */
  for (i = 0; i < mode_count_plus; i++)
    if ((mode_changes_plus[i].letter == 'o' ||
         mode_changes_plus[i].letter == 'h' ||
         mode_changes_plus[i].letter == 'v')
        && !irccmp(mode_changes_plus[i].arg, opnick))
    {
      if (mode_changes_plus[i].letter == 'o')
        wasnt_opped = -1;
      else if (mode_changes_plus[i].letter == 'h')
        wasnt_hopped = -1;
      mode_changes_plus[i].letter = 0;
    }

  for (i = 0; i < mode_count_minus; i++)
    if ((mode_changes_minus[i].letter == 'o' ||
         mode_changes_minus[i].letter == 'h' ||
         mode_changes_minus[i].letter == 'v')
        && !irccmp(mode_changes_minus[i].arg, opnick))
    {
      if (mode_changes_minus[i].letter == 'o')
        was_opped = -1;
      else if (mode_changes_minus[i].letter == 'v')
        was_voiced = -1;
      else if (mode_changes_minus[i].letter == 'h')
        was_hopped = -1;
      mode_changes_minus[i].letter = 0;
    }

  if (dir < 0)
  {
    if (was_voiced || t_voice)
    {
      mode_changes_minus[mode_count_minus].letter = 'v';
      mode_changes_minus[mode_count_minus].caps = 0;
      mode_changes_minus[mode_count_minus].nocaps = 0;
      mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
      mode_changes_minus[mode_count_minus].id = targ_p->user->id;
      mode_changes_minus[mode_count_minus++].arg = targ_p->name;
    }
    else if (was_hopped || t_hop)
    {
      mode_changes_minus[mode_count_minus].letter = 'h';
      mode_changes_minus[mode_count_minus].caps = CAP_HOPS;
      mode_changes_minus[mode_count_minus].nocaps = 0;
      mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
      mode_changes_minus[mode_count_minus].id = targ_p->user->id;
      mode_changes_minus[mode_count_minus++].arg = targ_p->name;
    }

    change_channel_membership(chptr, &chptr->chanops,
                              &chptr->locchanops, targ_p);

    if (was_opped == 0)
    {
      mode_changes_plus[mode_count_plus].letter = c;
      mode_changes_plus[mode_count_plus].caps = 0;
      mode_changes_plus[mode_count_plus].nocaps = 0;
      mode_changes_plus[mode_count_plus].mems = ONLY_CHANOPS_HALFOPS;
      mode_changes_plus[mode_count_plus].id = targ_p->user->id;
      mode_changes_plus[mode_count_plus++].arg = targ_p->name;
    }

    for (i = 0; i < resync_count; i++)
      if (resync_ops[i].client_p == targ_p)
      {
        if (resync_ops[i].dir == dir || resync_ops[i].c == c)
          resync_ops[i].client_p = NULL;
        else
          resync_ops[i].sync = 0;
        resync_ops[i].send = -1;
      }
    if (was_opped == 0 && MyConnect(targ_p) &&
        chptr->mode.mode & MODE_HIDEOPS)
    {
      resync_ops[resync_count].client_p = targ_p;
      resync_ops[resync_count].dir = dir;
      resync_ops[resync_count].c = c;
      resync_ops[resync_count].send = 0;
      /* -h+o or the implicit form of this shouldn't resync... */
      resync_ops[resync_count++].sync = (was_hopped || t_hop) ? 0 : -1;
    }
  }
  else
  {
    /* Converting -o to -h... */
    if (t_hop)
    {
      c = 'h';
      if (IsServer(source_p) || !MyConnect(source_p))
        return;

      if (wasnt_hopped == 0)
      {
        mode_changes_minus[mode_count_minus].letter = 'h';
        mode_changes_minus[mode_count_minus].caps = CAP_HOPS;
        mode_changes_minus[mode_count_minus].nocaps = 0;
        mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
        mode_changes_minus[mode_count_minus].id = NULL;
        mode_changes_minus[mode_count_minus++].arg = targ_p->name;
      }

      change_channel_membership(chptr, &chptr->peons,
                                &chptr->locpeons, targ_p);
    }
    else
    {
      if (wasnt_opped == 0)
      {
        mode_changes_minus[mode_count_minus].letter = c;
        mode_changes_minus[mode_count_minus].caps = 0;
        mode_changes_minus[mode_count_minus].nocaps = 0;
        mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
        mode_changes_minus[mode_count_minus].id = NULL;
        mode_changes_minus[mode_count_minus++].arg = targ_p->name;
      }

      change_channel_membership(chptr, &chptr->peons,
                                &chptr->locpeons, targ_p);
    }
    for (i = 0; i < resync_count; i++)
      if (resync_ops[i].client_p == targ_p)
        resync_ops[i].client_p = NULL;
    if (wasnt_opped == 0 && MyConnect(targ_p) &&
        chptr->mode.mode & MODE_HIDEOPS)
    {
      resync_ops[resync_count].client_p = targ_p;
      resync_ops[resync_count].dir = dir;
      resync_ops[resync_count].c = c;
      resync_ops[resync_count].send = -1;
      resync_ops[resync_count++].sync = -1;
    }
  }
}

static void
chm_halfop(struct Client *client_p, struct Client *source_p,
           struct Channel *chptr, int parc, int *parn,
           char **parv, int *errors, int alev, int dir, char c, void *d,
           const char *chname)
{
  int i, was_opped = 0, was_hopped = 0, was_voiced = 0, wasnt_hopped = 0;
  int t_voice = 0, t_op = 0, t_hop = 0;
  char *opnick;
  struct Client *targ_p;

  if (alev <
      ((chptr->mode.mode & MODE_PRIVATE) ? CHACCESS_CHANOP : CHACCESS_HALFOP))
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == 0 || parc <= *parn)
    return;

  if (!IsServer(source_p) && !MyConnect(source_p) && mode_limit++ > 4)
    return;

  opnick = parv[(*parn)++];

  if ((targ_p = find_client(opnick, NULL)) == NULL)
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one(source_p, form_str(ERR_NOSUCHNICK), me.name,
                 source_p->name, opnick);
    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if ((t_op = is_chan_op(chptr, targ_p)) != 0)
    t_op = -1;
  else if ((t_hop = is_half_op(chptr, targ_p)) != 0)
    t_hop = -1;
  else
    t_voice = is_voiced(chptr, targ_p);

  /* Ignore +/-h on op... */
  if (MyConnect(source_p) && !IsServer(source_p) && t_op)
    return;

  if (!IsMember(targ_p, chptr))
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one(source_p, form_str(ERR_USERNOTINCHANNEL), me.name,
                 source_p->name, chname, opnick);
    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if ((dir < 0 && (t_hop || t_op)) || (dir > 0 && !t_hop))
    return;

  /* Cancel out all other mode changes... */
  for (i = 0; i < mode_count_plus; i++)
    if ((mode_changes_plus[i].letter == 'o' ||
         mode_changes_plus[i].letter == 'h' ||
         mode_changes_plus[i].letter == 'v')
        && !irccmp(mode_changes_plus[i].arg, opnick))
    {
      if (mode_changes_plus[i].letter == 'h')
        wasnt_hopped = -1;
      /* +h-h is the only case we should get here, wipe it... */
      mode_changes_plus[i].letter = 0;
    }

  for (i = 0; i < mode_count_minus; i++)
    if ((mode_changes_minus[i].letter == 'o' ||
         mode_changes_minus[i].letter == 'h' ||
         mode_changes_minus[i].letter == 'v')
        && !irccmp(mode_changes_minus[i].arg, opnick))
    {
      if (mode_changes_minus[i].letter == 'o')
        was_opped = -1;
      else if (mode_changes_minus[i].letter == 'h')
        was_hopped = -1;
      else if (mode_changes_minus[i].letter == 'v')
        was_voiced = -1;
      /* -h+h or -v+h or +v+h should be wiped, -o+h should not. */
      if (mode_changes_minus[i].letter != 'o')
        mode_changes_minus[i].letter = 0;
    }

  if (dir < 0)
  {
    if (was_voiced || t_voice)
    {
      mode_changes_minus[mode_count_minus].letter = 'v';
      mode_changes_minus[mode_count_minus].caps = 0;
      mode_changes_minus[mode_count_minus].nocaps = 0;
      mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
      mode_changes_minus[mode_count_minus].id = targ_p->user->id;
      mode_changes_minus[mode_count_minus++].arg = targ_p->name;
    }
    if (was_hopped == 0)
    {
      mode_changes_plus[mode_count_plus].letter = c;
      mode_changes_plus[mode_count_plus].caps = CAP_HOPS;
      mode_changes_plus[mode_count_plus].nocaps = 0;
      mode_changes_plus[mode_count_plus].mems = ONLY_CHANOPS_HALFOPS;
      mode_changes_plus[mode_count_plus].id = targ_p->user->id;
      mode_changes_plus[mode_count_plus++].arg = targ_p->name;
      change_channel_membership(chptr, &chptr->halfops,
                                &chptr->lochalfops, targ_p);
    }
    for (i = 0; i < resync_count; i++)
      if (resync_ops[i].client_p == targ_p)
      {
        if (resync_ops[i].dir == dir || resync_ops[i].c == c)
          resync_ops[i].client_p = NULL;
        else
          resync_ops[i].sync = 0;
        resync_ops[i].send = 0;
      }
    if (was_hopped == 0 && MyConnect(targ_p) &&
        chptr->mode.mode & MODE_HIDEOPS)
    {
      resync_ops[resync_count].client_p = targ_p;
      resync_ops[resync_count].dir = dir;
      resync_ops[resync_count].c = c;
      resync_ops[resync_count].send = 0;
      /* -o+h shouldn't send massdeop... */
      resync_ops[resync_count++].sync = was_opped ? 0 : -1;
    }
  }
  else
  {
    if (wasnt_hopped == 0)
    {
      mode_changes_minus[mode_count_minus].letter = 'h';
      mode_changes_minus[mode_count_minus].caps = CAP_HOPS;
      mode_changes_minus[mode_count_minus].nocaps = 0;
      mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
      mode_changes_minus[mode_count_minus].id = targ_p->user->id;
      mode_changes_minus[mode_count_minus++].arg = targ_p->name;
    }

    change_channel_membership(chptr, &chptr->peons, &chptr->locpeons, targ_p);
    for (i = 0; i < resync_count; i++)
      if (resync_ops[i].client_p == targ_p)
        resync_ops[i].client_p = NULL;
    if (wasnt_hopped == 0 && MyConnect(targ_p) &&
        chptr->mode.mode & MODE_HIDEOPS)
    {
      resync_ops[resync_count].client_p = targ_p;
      resync_ops[resync_count].dir = dir;
      resync_ops[resync_count].c = c;
      resync_ops[resync_count].send = -1;
      resync_ops[resync_count++].sync = -1;
    }
  }
}

static void
chm_voice(struct Client *client_p, struct Client *source_p,
          struct Channel *chptr, int parc, int *parn,
          char **parv, int *errors, int alev, int dir, char c, void *d,
          const char *chname)
{
  int i, was_hopped = 0, was_opped = 0, was_voiced = 0, wasnt_voiced = 0;
  int t_op = 0, t_voice = 0, t_hop = 0;
  char *opnick;
  struct Client *targ_p;

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == 0 || parc <= *parn)
    return;

  opnick = parv[(*parn)++];

  if ((targ_p = find_client(opnick, NULL)) == NULL)
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one(source_p, form_str(ERR_NOSUCHNICK), me.name,
                 source_p->name, opnick);
    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if (!IsMember(targ_p, chptr))
  {
    if (!(*errors & SM_ERR_NOTONCHANNEL))
      sendto_one(source_p, form_str(ERR_USERNOTINCHANNEL), me.name,
                 source_p->name, chname, opnick);
    *errors |= SM_ERR_NOTONCHANNEL;
    return;
  }

  if ((t_op = is_chan_op(chptr, targ_p)) != 0)
    t_op = -1;
  else if ((t_hop = is_half_op(chptr, targ_p)) != 0)
    t_hop = -1;
  else
    t_voice = is_voiced(chptr, targ_p);

  /* Ignore +/-v on op... Now we need not worry about limiting the
   * number of +/-v... */
  if (MyConnect(source_p) && !IsServer(source_p) && (t_op || t_hop))
    return;

  if (!IsServer(source_p) && !MyConnect(source_p) && mode_limit++ > 4)
    return;

  if (t_op || t_hop || (dir < 0 && t_voice) || (dir > 0 && !t_voice))
    return;

  /* Remove other changes... */
  for (i = 0; i < mode_count_plus; i++)
    if ((mode_changes_plus[i].letter == 'o' ||
         mode_changes_plus[i].letter == 'h' ||
         mode_changes_plus[i].letter == 'v')
        && !irccmp(mode_changes_plus[i].arg, opnick))
    {
      if (mode_changes_plus[i].letter == 'v')
        wasnt_voiced = -1;
      mode_changes_plus[i].letter = 0;
    }

  for (i = 0; i < mode_count_minus; i++)
    if ((mode_changes_minus[i].letter == 'o' ||
         mode_changes_minus[i].letter == 'h' ||
         mode_changes_minus[i].letter == 'v')
        && !irccmp(mode_changes_minus[i].arg, opnick))
    {
      if (mode_changes_minus[i].letter == 'o')
        was_opped = -1;
      if (mode_changes_minus[i].letter == 'h')
        was_hopped = -1;
      if (mode_changes_minus[i].letter == 'v')
        was_voiced = -1;
      /* No assignment to 0 here needed... -A1kmm */
    }

  if (dir < 0)
  {
    change_channel_membership(chptr, &chptr->voiced, &chptr->locvoiced,
                              targ_p);
    if (was_voiced == 0)
    {
      mode_changes_plus[mode_count_plus].letter = c;
      mode_changes_plus[mode_count_plus].caps = 0;
      mode_changes_plus[mode_count_plus].nocaps = 0;
      mode_changes_plus[mode_count_plus].mems = ONLY_CHANOPS_HALFOPS;
      mode_changes_plus[mode_count_plus].id = targ_p->user->id;
      mode_changes_plus[mode_count_plus++].arg = targ_p->name;
    }
    for (i = 0; i < resync_count; i++)
      if (resync_ops[i].client_p == targ_p)
      {
        if (resync_ops[i].dir == dir || resync_ops[i].c == c)
          resync_ops[i].client_p = NULL;
      }
    if (was_voiced == 0 && MyConnect(targ_p) &&
        chptr->mode.mode & MODE_HIDEOPS)
    {
      resync_ops[resync_count].client_p = targ_p;
      resync_ops[resync_count].dir = dir;
      resync_ops[resync_count].c = c;
      resync_ops[resync_count].sync = 0;
      /* voiced users don't see mode changes, so send even on +v... */
      resync_ops[resync_count++].send = -1;
    }
  }
  else
  {
    change_channel_membership(chptr, &chptr->peons, &chptr->locpeons, targ_p);
    /* Don't send redundant modes on +v-v nick nick */
    if (wasnt_voiced == 0)
    {
      mode_changes_minus[mode_count_minus].letter = 'v';
      mode_changes_minus[mode_count_minus].caps = 0;
      mode_changes_minus[mode_count_minus].nocaps = 0;
      mode_changes_minus[mode_count_minus].mems = ONLY_CHANOPS_HALFOPS;
      mode_changes_minus[mode_count_minus].arg = targ_p->user->id;
      mode_changes_minus[mode_count_minus++].arg = targ_p->name;
    }
    resync_ops[resync_count].sync = 0;
    for (i = 0; i < resync_count; i++)
      if (resync_ops[i].client_p == targ_p)
      {
        resync_ops[i].client_p = NULL;
        if (resync_ops[i].sync != 0)
          resync_ops[resync_count].sync = -1;
      }
    if (was_voiced == 0 && MyConnect(targ_p) &&
        chptr->mode.mode & MODE_HIDEOPS)
    {
      resync_ops[resync_count].client_p = targ_p;
      resync_ops[resync_count].dir = dir;
      resync_ops[resync_count].c = c;
      resync_ops[resync_count++].send = -1;
    }
  }
}

static void
chm_limit(struct Client *client_p, struct Client *source_p,
          struct Channel *chptr, int parc, int *parn,
          char **parv, int *errors, int alev, int dir, char c, void *d,
          const char *chname)
{
  int i, limit;
  char *lstr;

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == 0)
    return;

  if (dir < 0 && parc > *parn)
  {
    lstr = parv[(*parn)++];

    if ((limit = strtoul(lstr, NULL, 10)) <= 0)
      return;

    ircsprintf(lstr, "%d", limit);

    for (i = 0; i < mode_count_minus; i++)
      if (mode_changes_minus[i].letter == c)
        mode_changes_minus[i].letter = 0;

    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c)
        mode_changes_plus[i].letter = 0;

    mode_changes_plus[mode_count_plus].letter = c;
    mode_changes_plus[mode_count_plus].caps = 0;
    mode_changes_plus[mode_count_plus].nocaps = 0;
    mode_changes_plus[mode_count_plus].mems = ALL_MEMBERS;
    mode_changes_plus[mode_count_plus].id = NULL;
    mode_changes_plus[mode_count_plus++].arg = lstr;

    chptr->mode.mode |= MODE_LIMIT;
    chptr->mode.limit = limit;
  }
  else if (dir > 0)
  {
    if (!(chptr->mode.mode & MODE_LIMIT))
      return;

    chptr->mode.mode &= ~MODE_LIMIT;
    chptr->mode.limit = 0;

    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c)
      {
        mode_changes_plus[i].letter = 0;
        return;
      }

    mode_changes_minus[mode_count_minus].letter = c;
    mode_changes_minus[mode_count_minus].caps = 0;
    mode_changes_minus[mode_count_minus].nocaps = 0;
    mode_changes_minus[mode_count_minus].mems = ALL_MEMBERS;
    mode_changes_minus[mode_count_minus].id = NULL;
    mode_changes_minus[mode_count_minus++].arg = NULL;
  }
}

static void
chm_key(struct Client *client_p, struct Client *source_p,
        struct Channel *chptr, int parc, int *parn,
        char **parv, int *errors, int alev, int dir, char c, void *d,
        const char *chname)
{
  int i;
  char *key;

  if (alev < CHACCESS_HALFOP)
  {
    if (!(*errors & SM_ERR_NOOPS))
      sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                 source_p->name, chname);
    *errors |= SM_ERR_NOOPS;
    return;
  }

  if (dir == 0)
    return;

  if (dir < 0 && parc > *parn)
  {
    key = parv[(*parn)++];
    if (MyConnect(source_p))
      fix_key(key);
    else
      fix_key_old(key);
    strncpy_irc(chptr->mode.key, key, KEYLEN - 1)[KEYLEN - 1] = 0;
    for (i = 0; i < mode_count_minus; i++)
      if (mode_changes_minus[i].letter == c)
        mode_changes_minus[i].letter = 0;
    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c)
        mode_changes_plus[i].letter = 0;
    mode_changes_plus[mode_count_plus].letter = c;
    mode_changes_plus[mode_count_plus].caps = 0;
    mode_changes_plus[mode_count_plus].nocaps = 0;
    mode_changes_plus[mode_count_plus].mems = ALL_MEMBERS;
    mode_changes_plus[mode_count_plus].id = NULL;
    mode_changes_plus[mode_count_plus++].arg = key;
    chptr->mode.mode |= MODE_KEY;
  }
  else if (dir > 0)
  {
    if (!(chptr->mode.mode & MODE_KEY))
      return;

    chptr->mode.mode &= ~MODE_KEY;
    *chptr->mode.key = 0;

    for (i = 0; i < mode_count_plus; i++)
      if (mode_changes_plus[i].letter == c)
        mode_changes_plus[i].letter = 0;

    mode_changes_minus[mode_count_minus].letter = c;
    mode_changes_minus[mode_count_minus].caps = 0;
    mode_changes_minus[mode_count_minus].nocaps = 0;
    mode_changes_minus[mode_count_minus].mems = ALL_MEMBERS;
    mode_changes_minus[mode_count_minus].id = NULL;
    mode_changes_minus[mode_count_minus++].arg = "*";
  }
}

struct ChannelMode
{
  void (*func) (struct Client *, struct Client *, struct Channel *,
                int parc, int *parn, char **parv, int *errors, int alev,
                int dir, char c, void *d, const char *chname);
  void *d;
}
/* *INDENT-OFF* */
ModeTable[255] =
{
  {chm_nosuch, NULL},
  {chm_nosuch, NULL},                             /* A */
  {chm_nosuch, NULL},                             /* B */
  {chm_nosuch, NULL},                             /* C */
  {chm_nosuch, NULL},                             /* D */
  {chm_nosuch, NULL},                             /* E */
  {chm_nosuch, NULL},                             /* F */
  {chm_nosuch, NULL},                             /* G */
  {chm_nosuch, NULL},                             /* H */
  {chm_invex, NULL},                              /* I */
  {chm_nosuch, NULL},                             /* J */
  {chm_nosuch, NULL},                             /* K */
  {chm_nosuch, NULL},                             /* L */
  {chm_nosuch, NULL},                             /* M */
  {chm_nosuch, NULL},                             /* N */
  {chm_nosuch, NULL},                             /* O */
  {chm_nosuch, NULL},                             /* P */
  {chm_nosuch, NULL},                             /* Q */
  {chm_nosuch, NULL},                             /* R */
  {chm_nosuch, NULL},                             /* S */
  {chm_nosuch, NULL},                             /* T */
  {chm_nosuch, NULL},                             /* U */
  {chm_nosuch, NULL},                             /* V */
  {chm_nosuch, NULL},                             /* W */
  {chm_nosuch, NULL},                             /* X */
  {chm_nosuch, NULL},                             /* Y */
  {chm_nosuch, NULL},                             /* Z */
  {chm_nosuch, NULL},
  {chm_nosuch, NULL},
  {chm_nosuch, NULL},
  {chm_nosuch, NULL},
  {chm_nosuch, NULL},
  {chm_nosuch, NULL},
  {chm_hideops, NULL},                            /* a */
  {chm_ban, NULL},                                /* b */
  {chm_nosuch, NULL},                             /* c */
  {chm_nosuch, NULL},                             /* d */
  {chm_except, NULL},                             /* e */
  {chm_nosuch, NULL},                             /* f */
  {chm_nosuch, NULL},                             /* g */
  {chm_halfop, NULL},                             /* h */
  {chm_simple, (void *) MODE_INVITEONLY},         /* i */
  {chm_nosuch, NULL},                             /* j */
  {chm_key, NULL},                                /* k */
  {chm_limit, NULL},                              /* l */
  {chm_simple, (void *) MODE_MODERATED},          /* m */
  {chm_simple, (void *) MODE_NOPRIVMSGS},         /* n */
  {chm_op, NULL},                                 /* o */
  {chm_simple, (void *) MODE_PRIVATE},            /* p */
  {chm_nosuch, NULL},                             /* q */
  {chm_nosuch, NULL},                             /* r */
  {chm_simple, (void *) MODE_SECRET},             /* s */
  {chm_simple, (void *) MODE_TOPICLIMIT},         /* t */
  {chm_nosuch, NULL},                             /* u */
  {chm_voice, NULL},                              /* v */
  {chm_nosuch, NULL},                             /* w */
  {chm_nosuch, NULL},                             /* x */
  {chm_nosuch, NULL},                             /* y */
  {chm_nosuch, NULL},                             /* z */
};
/* *INDENT-ON* */

/* int get_channel_access(struct Client *source_p, struct Channel *chptr)
 * Input: The client, the channel
 * Output: MODE_CHANOP if we should let them have chanop level access,
 *         MODE_HALFOP for halfop level access,
 *         MODE_PEON for peon level access.
 * Side-effects: None.
 */
static int
get_channel_access(struct Client *source_p, struct Channel *chptr)
{
  /* Let hacked servers in for now... */
  if (!MyConnect(source_p) || IsServer(source_p))
    return CHACCESS_CHANOP;

  if (is_chan_op(chptr, source_p))
    return CHACCESS_CHANOP;

  if (is_half_op(chptr, source_p))
    return CHACCESS_HALFOP;

  return CHACCESS_PEON;
}

/* void send_cap_mode_changes(struct Client *client_p,
 *                        struct Client *source_p,
 *                        struct Channel *chptr, int cap, int nocap)
 * Input: The client sending(client_p), the source client(source_p),
 *        the channel to send mode changes for(chptr)
 * Output: None.
 * Side-effects: Sends the appropriate mode changes to capable servers.
 *
 * send_cap_mode_changes() will loop the server list itself, because
 * at this point in time we have 4 capabs for channels, CAP_IE, CAP_EX,
 * CAP_AOPS, CAP_HOPS, and a server could support any number of these..
 * so we make the modebufs per server, tailoring them to each servers
 * specific demand.  Its not very pretty, but its one of the few realistic
 * ways to handle having this many capabs for channel modes.. --fl_
 *
 * Reverted back to my original design, except that we now keep a count
 * of the number of servers which each combination as an optimisation, so
 * the capabs combinations which are not needed are not worked out. -A1kmm
 */
static void
send_cap_mode_changes(struct Client *client_p, struct Client *source_p,
                      struct Channel *chptr, int cap, int nocap)
{
  int i, mbl, pbl, nc = 0;
  char *arg;

  pbl = 0;
  parabuf[0] = 0;
  ircsprintf(modebuf, ":%s MODE %s ",
             (cap&CAP_UID&&source_p->user&&source_p->user->id[0]=='.') ?
             source_p->user->id : source_p->name, chptr->chname);
  mbl = strlen(modebuf);

  if (mode_count_minus > 0)
  {
    modebuf[mbl++] = '-';
    modebuf[mbl] = 0;
  }

  /* loop the list of - modes we have */
  for (i = 0; i < mode_count_minus; i++)
  {
    /* if they dont support the cap we need, or they do support a cap they
     * cant have, then dont add it to the modebuf.. that way they wont see
     * the mode
     */
    if (mode_changes_minus[i].letter == 0 ||
        (cap & mode_changes_minus[i].caps) != mode_changes_minus[i].caps
        || (nocap & mode_changes_minus[i].nocaps) != 0)
      continue;
    arg = mode_changes_minus[i].arg;
    if ((cap & CAP_UID) && mode_changes_minus[i].id != NULL)
      arg = mode_changes_minus[i].id;
    nc++;
    /* if we're creeping past the buf size, we need to send it and make
     * another line for the other modes
     * XXX - this could give away server topology with uids being
     * different lengths, but not much we can do, except possibly break
     * them as if they were the longest of the nick or uid at all times,
     * which even then won't work as we don't always know the uid -A1kmm.
     */
    if (arg != NULL &&
        strlen(arg) + mbl + pbl + 2 > BUFSIZE)
    {
      if (nc != 0)
        sendto_server(client_p, source_p, chptr, cap, nocap,
                      LL_ICHAN | LL_ICLIENT, "%s %s", modebuf, parabuf);
      nc = 0;
      ircsprintf(modebuf, ":%s MODE %s -",
            (cap&CAP_UID&&source_p->user&&source_p->user->id[0]=='.') ?
            source_p->user->id : source_p->name, chptr->chname);
      mbl = strlen(modebuf);
      pbl = 0;
      parabuf[0] = 0;
    }

    modebuf[mbl++] = mode_changes_minus[i].letter;
    modebuf[mbl] = 0;

    if (arg != NULL)
      pbl = strlcat(parabuf, arg, MODEBUFLEN);

    parabuf[pbl++] = ' ';
    parabuf[pbl] = 0;
  }

  if (mode_count_plus > 0)
  {
    if (mbl > 0 && modebuf[mbl - 1] == '-')
      modebuf[mbl - 1] = '+';
    else
      modebuf[mbl++] = '+';

    modebuf[mbl] = 0;
  }

  /* loop the + modes */
  for (i = 0; i < mode_count_plus; i++)
  {
    /* same as above, check they support needed capabs, and dont have
     * capabs we dont want..
     */
    if (mode_changes_plus[i].letter == 0 ||
        (cap & mode_changes_plus[i].caps) != mode_changes_plus[i].caps
        || (nocap & mode_changes_plus[i].nocaps) != 0)
      continue;

    arg = mode_changes_plus[i].arg;
    if ((cap & CAP_UID) && mode_changes_plus[i].id != NULL)
      arg = mode_changes_plus[i].id;
    nc++;

    if (arg != NULL &&
        strlen(arg) + mbl + pbl + 2 > BUFSIZE)
    {
      if (nc != 0)
        sendto_server(client_p, source_p, chptr, cap, nocap,
                      LL_ICHAN | LL_ICLIENT, "%s %s", modebuf, parabuf);
      nc = 0;
      ircsprintf(modebuf, ":%s MODE %s +",
            (cap&CAP_UID&&source_p->user&&source_p->user->id[0]=='.') ?
            source_p->user->id : source_p->name, chptr->chname);
      mbl = strlen(modebuf);
      pbl = 0;
      parabuf[0] = 0;
    }

    modebuf[mbl++] = mode_changes_plus[i].letter;
    modebuf[mbl] = 0;

    if (mode_changes_plus[i].arg != NULL)
      pbl = strlcat(parabuf, mode_changes_plus[i].arg, MODEBUFLEN);

    parabuf[pbl++] = ' ';
    parabuf[pbl] = 0;
  }

  if (pbl && parabuf[pbl - 1] == '+')
    parabuf[pbl - 1] = 0;

  if (nc != 0)
    sendto_server(client_p, source_p, chptr, cap, nocap,
                  LL_ICHAN | LL_ICLIENT, "%s %s", modebuf, parabuf);
}

/* void send_mode_changes(struct Client *client_p,
 *                        struct Client *source_p,
 *                        struct Channel *chptr)
 * Input: The client sending(client_p), the source client(source_p),
 *        the channel to send mode changes for(chptr),
 *        mode change globals.
 * Output: None.
 * Side-effects: Sends the appropriate mode changes to other clients
 *               and propagates to servers.
 */
static void
send_mode_changes(struct Client *client_p, struct Client *source_p,
                  struct Channel *chptr, char *chname)
{
  int pbl, mbl, nc;
  int i, st;

  if (mode_count_plus == 0 && mode_count_minus == 0)
    return;
  /* Send any resyncs that we need to send... */
  if (chptr->mode.mode & MODE_HIDEOPS)
  {
    for (i = 0; i < resync_count; i++)
    {
      if (resync_ops[i].client_p == NULL)
        continue;
      if (resync_ops[i].sync != 0)
      {
        if (resync_ops[i].dir == -1)
          sync_oplists(chptr, resync_ops[i].client_p, 0, chname);
        else
          sync_oplists(chptr, resync_ops[i].client_p, 1, chname);
      }
      if (resync_ops[i].dir == -1 && resync_ops[i].c == 'v')
        sendto_one(resync_ops[i].client_p, ":%s MODE %s +v %s", me.name,
                   chname, resync_ops[i].client_p->name);
      else if (resync_ops[i].send != 0)
        sendto_one(resync_ops[i].client_p, ":%s MODE %s -%c %s", me.name,
                   chname, resync_ops[i].c, resync_ops[i].client_p->name);
    }
  }

  /* Send all mode changes to the chanops/halfops, and even peons if
   * we are not +a... */
  st = (chptr->mode.mode & MODE_HIDEOPS) ? ONLY_CHANOPS_HALFOPS : ALL_MEMBERS;

  if (IsServer(source_p))
    ircsprintf(modebuf, ":%s MODE %s ", me.name, chname);
  else
    ircsprintf(modebuf, ":%s!%s@%s MODE %s ", source_p->name,
               source_p->username, source_p->host, chname);

  mbl = strlen(modebuf);
  pbl = 0;
  parabuf[0] = '\0';
  nc = 0;

  if (mode_count_minus > 0)
  {
    modebuf[mbl++] = '-';
    modebuf[mbl] = '\0';
  }

  for (i = 0; i < mode_count_minus; i++)
  {
    if (mode_changes_minus[i].letter == 0 ||
        mode_changes_minus[i].mems == NON_CHANOPS)
      continue;
    nc++;
    if (mode_changes_minus[i].arg != NULL &&
        strlen(mode_changes_minus[i].arg) + mbl + pbl + 2 > BUFSIZE)
    {
      if (pbl && parabuf[pbl - 1] == '+')
        parabuf[pbl - 1] = '\0';

      if (nc != 0)
        sendto_channel_local(st, chptr, "%s %s", modebuf, parabuf);

      nc = 0;

      if (IsServer(source_p))
        ircsprintf(modebuf, ":%s MODE %s -", me.name, chname);
      else
        ircsprintf(modebuf, ":%s!%s@%s MODE %s -", source_p->name,
                   source_p->username, source_p->host, chname);

      mbl = strlen(modebuf);
      pbl = 0;
      parabuf[0] = '\0';
    }

    modebuf[mbl++] = mode_changes_minus[i].letter;
    modebuf[mbl] = '\0';

    if (mode_changes_minus[i].arg != NULL)
      pbl = strlen(strcat(parabuf, mode_changes_minus[i].arg));

    parabuf[pbl++] = ' ';
    parabuf[pbl] = '\0';
  }

  if (mode_count_plus > 0)
  {
    if (mbl > 0 && modebuf[mbl - 1] == '-')
      modebuf[mbl - 1] = '+';
    else
      modebuf[mbl++] = '+';
    modebuf[mbl] = '\0';
  }

  for (i = 0; i < mode_count_plus; i++)
  {
    if (mode_changes_plus[i].letter == 0 ||
        mode_changes_plus[i].mems == NON_CHANOPS)
      continue;
    nc++;
    if (mode_changes_plus[i].arg != NULL &&
        strlen(mode_changes_plus[i].arg) + mbl + pbl + 2 > BUFSIZE)
    {
      if (pbl && parabuf[pbl - 1] == '+')
        parabuf[pbl - 1] = '\0';

      if (nc != 0)
        sendto_channel_local(st, chptr, "%s %s", modebuf, parabuf);

      nc = 0;

      if (IsServer(source_p))
        ircsprintf(modebuf, ":%s MODE %s +", me.name, chname);
      else
        ircsprintf(modebuf, ":%s!%s@%s MODE %s +", source_p->name,
                   source_p->username, source_p->host, chname);

      mbl = strlen(modebuf);
      pbl = 0;
      parabuf[0] = '\0';
    }

    modebuf[mbl++] = mode_changes_plus[i].letter;
    modebuf[mbl] = '\0';

    if (mode_changes_plus[i].arg != NULL)
      pbl = strlen(strcat(parabuf, mode_changes_plus[i].arg));

    parabuf[pbl++] = ' ';
    parabuf[pbl] = '\0';
  }

  if (pbl && parabuf[pbl - 1] == '+')
    parabuf[pbl - 1] = '\0';

  if (nc != 0)
    sendto_channel_local(st, chptr, "%s %s", modebuf, parabuf);

  nc = 0;

  /* If peons were missed out above, send to them now... */
  if (chptr->mode.mode & MODE_HIDEOPS)
  {
    st = NON_CHANOPS;
    ircsprintf(modebuf, ":%s MODE %s ", me.name, chname);
    mbl = strlen(modebuf);
    pbl = 0;
    parabuf[0] = '\0';

    if (mode_count_minus > 0)
    {
      modebuf[mbl++] = '-';
      modebuf[mbl] = '\0';
    }

    for (i = 0; i < mode_count_minus; i++)
    {
      if (mode_changes_minus[i].letter == 0 ||
          mode_changes_minus[i].mems != ALL_MEMBERS)
        continue;

      nc++;

      if (mode_changes_minus[i].arg != NULL &&
          strlen(mode_changes_minus[i].arg) + mbl + pbl + 2 > BUFSIZE)
      {
        if (pbl && parabuf[pbl - 1] == '+')
          parabuf[pbl - 1] = '\0';

        if (nc != 0)
          sendto_channel_local(st, chptr, "%s %s", modebuf, parabuf);

        nc = 0;
        ircsprintf(modebuf, ":%s MODE %s -", me.name, chname);
        mbl = strlen(modebuf);
        pbl = 0;
        parabuf[0] = '\0';
      }

      modebuf[mbl++] = mode_changes_minus[i].letter;
      modebuf[mbl] = '\0';

      if (mode_changes_minus[i].arg != NULL)
        pbl = strlen(strcat(parabuf, mode_changes_minus[i].arg));

      parabuf[pbl++] = ' ';
      parabuf[pbl] = '\0';
    }

    if (mode_count_plus > 0)
    {
      if (mbl > 0 && modebuf[mbl - 1] == '-')
        modebuf[mbl - 1] = '+';
      else
        modebuf[mbl++] = '+';

      modebuf[mbl] = '\0';
    }

    for (i = 0; i < mode_count_plus; i++)
    {
      if (mode_changes_plus[i].letter == 0 ||
          mode_changes_plus[i].mems != ALL_MEMBERS)
        continue;

      nc++;

      if (mode_changes_plus[i].arg != NULL &&
          strlen(mode_changes_plus[i].arg) + mbl + pbl + 2 > BUFSIZE)
      {
        if (pbl && parabuf[pbl - 1] == '+')
          parabuf[pbl - 1] = '\0';

        if (nc != 0)
          sendto_channel_local(st, chptr, "%s %s", modebuf, parabuf);

        nc = 0;
        ircsprintf(modebuf, ":%s MODE %s +", me.name, chname);
        mbl = strlen(modebuf);
        pbl = 0;
        parabuf[0] = '\0';
      }

      modebuf[mbl++] = mode_changes_plus[i].letter;
      modebuf[mbl] = '\0';

      if (mode_changes_plus[i].arg != NULL)
        pbl = strlen(strcat(parabuf, mode_changes_plus[i].arg));

      parabuf[pbl++] = ' ';
      parabuf[pbl] = '\0';
    }

    if (pbl && parabuf[pbl - 1] == '+')
      parabuf[pbl - 1] = '\0';

    if (nc != 0)
      sendto_channel_local(st, chptr, "%s %s", modebuf, parabuf);
  }

  /* Now send to servers... */
  for (i = 0; i < NCHCAP_COMS; i++)
    if (chcap_usage_counts[i] != 0)
      send_cap_mode_changes(client_p, source_p, chptr, chcap_yes[i],
                            chcap_no[i]);
}

/* void set_channel_mode(struct Client *client_p, struct Client *source_p,
 *               struct Channel *chptr, int parc, char **parv,
 *               char *chname)
 * Input: The client we received this from, the client this originated
 *        from, the channel, the parameter count starting at the modes,
 *        the parameters, the channel name.
 * Output: None.
 * Side-effects: Changes the channel membership and modes appropriately,
 *               sends the appropriate MODE messages to the appropriate
 *               clients.
 */
void
set_channel_mode(struct Client *client_p, struct Client *source_p,
                 struct Channel *chptr, int parc, char **parv, char *chname)
{
  int dir = -1;                 /* '-' => +1, '+' => -1, '=' => 0. Start with '+' */
  int parn = 1;
  int alevel, errors = 0;
  char *ml = parv[0], c, table_position;
  mask_pos = 0;
  alevel = get_channel_access(source_p, chptr);
  mode_count_plus = 0;
  mode_count_minus = 0;
  resync_count = 0;
  mode_limit = 0;

  for (; (c = *ml) != 0; ml++)
    switch (c)
    {
      case '+':
        dir = -1;
        break;
      case '-':
        dir = +1;
        break;
      case '=':
        dir = 0;
        break;
      default:
        if (c < 'A' || c > 'z')
          table_position = 0;
        else
          table_position = c - 'A' + 1;
        ModeTable[(int)table_position].func(client_p, source_p, chptr,
                                            parc, &parn,
                                            parv, &errors, alevel, dir, c,
                                            ModeTable[(int)table_position].d,
                                            chname);
        break;
    }

  send_mode_changes(client_p, source_p, chptr, chname);
}
#else
/*
** Apply the mode changes passed in parv to chptr, sending any error
** messages and MODE commands out.  Rewritten to do the whole thing in
** one pass, in a desperate attempt to keep the code sane.  -orabidoo
*/
void
set_channel_mode(struct Client *client_p,
                 struct Client *source_p,
                 struct Channel *chptr, int parc, char *parv[], char *chname)
{
  int errors_sent = 0, opcnt = 0;
  int len = 0;
  int tmp, nusers;
  int keychange = 0, limitset = 0;
  int whatt = MODE_ADD, the_mode = 0;
  int done_s = NO, done_p = NO;
  int done_i = NO, done_m = NO, done_n = NO, done_t = NO;
  int done_a = NO;

  struct Client *who;
  char *curr = parv[0], c, *arg, plus = '+', *tmpc;
  char numeric[16];
  /* mbufw gets the non-capab mode chars, which nonops can see
   * on a +a channel (+smntilk)
   * pbufw gets the params.
   * mbuf2w gets the other non-capab mode chars, always with their sign
   * pbuf2w gets the other non-capab params
   */

  char modebuf_ex[MODEBUFLEN] = "";
  char parabuf_ex[MODEBUFLEN] = "";

  char modebuf_invex[MODEBUFLEN] = "";
  char parabuf_invex[MODEBUFLEN] = "";

  char modebuf_hops[MODEBUFLEN] = "";
  char parabuf_hops[MODEBUFLEN] = "";
  char parabuf_hops_id[MODEBUFLEN] = "";

  char modebuf_aops[MODEBUFLEN] = "";   /* +a doesn't take params */

  char *mbufw = modebuf, *mbuf2w = modebuf2;
  char *pbufw = parabuf, *pbuf2w = parabuf2;
  char *pbufw_id = parabuf_id, *pbuf2w_id = parabuf2_id;

  char *mbufw_ex = modebuf_ex;
  char *pbufw_ex = parabuf_ex;

  char *mbufw_invex = modebuf_invex;
  char *pbufw_invex = parabuf_invex;

  char *mbufw_hops = modebuf_hops;
  char *pbufw_hops = parabuf_hops;
  char *pbufw_hops_id = parabuf_hops_id;

  char *mbufw_aops = modebuf_aops;

  int ischop;
  int isok;
  int isok_c;
  int isdeop;
  int chan_op;
  int type;

  int target_was_chop;
  int target_was_op;
  int target_was_hop;
  int target_was_voice;

  int halfop_deop_self;
  int halfop_setting_voice;

  dlink_node *ptr;
  dlink_list *to_list = NULL;
  dlink_list *loc_to_list = NULL;
  struct Ban *banptr;

  chan_op = is_chan_op(chptr, source_p);

  /* has ops or is a server */
  ischop = IsServer(source_p) || chan_op;

  isdeop = 0;

  /* is an op or server or remote user on a TS channel */
  isok = ischop || (!isdeop && IsServer(client_p) && chptr->channelts);
  isok_c = isok || is_half_op(chptr, source_p);

  /* parc is the number of _remaining_ args (where <0 means 0);
     ** parv points to the first remaining argument
   */
  parc--;
  parv++;

  for (;;)
  {
    halfop_deop_self = 0;
    halfop_setting_voice = 0;
    if (BadPtr(curr))
    {
      /*
       * Deal with mode strings like "+m +o blah +i"
       */
      if (parc-- > 0)
      {
        curr = *parv++;
        continue;
      }
      break;
    }
    c = *curr++;

    switch (c)
    {
      case '+':
        whatt = MODE_ADD;
        plus = '+';
        continue;
        /* NOT REACHED */
        break;

      case '-':
        whatt = MODE_DEL;
        plus = '-';
        continue;
        /* NOT REACHED */
        break;

      case '=':
        whatt = MODE_QUERY;
        plus = '=';
        continue;
        /* NOT REACHED */
        break;

      case 'h':
      case 'o':
      case 'v':
        if (MyClient(source_p))
        {
          if (!IsMember(source_p, chptr))
          {
            if (!errsent(SM_ERR_NOTONCHANNEL, &errors_sent))
              sendto_one(source_p, form_str(ERR_NOTONCHANNEL),
                         me.name, source_p->name, chname);
            /* eat the parameter */
            parc--;
            parv++;
            break;
          }
          else
          {
            if (IsRestricted(source_p) && (whatt == MODE_ADD))
            {
              sendto_one(source_p,
                         ":%s NOTICE %s :*** NOTICE -- You are restricted and cannot chanop others",
                         me.name, source_p->name);
              parc--;
              parv++;
              break;
            }
          }
        }
        if (whatt == MODE_QUERY)
          break;
        if (parc-- <= 0)
          break;
        arg = check_string(*parv++);

        if (MyClient(source_p) && opcnt >= MAXMODEPARAMS)
          break;

        if (!(who = find_chasing(source_p, arg, NULL)))
          break;

        if (!who->user)
          break;

        /* no more of that mode bouncing crap */
        if (!IsMember(who, chptr))
        {
          if (MyClient(source_p))
            sendto_one(source_p, form_str(ERR_USERNOTINCHANNEL),
                       me.name, source_p->name, arg, chname);
          break;
        }

        /* ignore server-generated MODE +-ovh */
        /* naw, allow it but still flag it */
	/* ignore +ovh and flag it, allow -ovh */
        if (IsServer(source_p) && (whatt == MODE_ADD))
        {
          ts_warn("MODE +%c on %s for %s from server %s",
                  c, chname, who->name, source_p->name);
          break;
        }

        target_was_chop = is_chan_op(chptr, who);
        target_was_hop = is_half_op(chptr, who);
        target_was_voice = is_voiced(chptr, who);
        target_was_op = target_was_chop | target_was_hop;

        if (c == 'o')
        {
          /* convert attempts to -o a +h user into a -h
           * ignore attempts to -o a +v user.
           */
          if ((whatt == MODE_DEL) && target_was_hop)
          {
            the_mode = MODE_HALFOP;
            c = 'h';
          }
          else if ((whatt == MODE_DEL) && target_was_voice)
            break;
          else
            the_mode = MODE_CHANOP;

          to_list = &chptr->chanops;
          loc_to_list = &chptr->locchanops;
        }
        else if (c == 'v')
        {
          /* ignore attempts to +v/-v if they are +o/+h */
          if (target_was_op)
            break;
          the_mode = MODE_VOICE;
          to_list = &chptr->voiced;
          loc_to_list = &chptr->locvoiced;
        }
        else if (c == 'h')
        {
          /* ignore attempts to +h/-h if they are +o
           * ignore attempts to -h if they are +v */
          if (target_was_chop || ((whatt == MODE_DEL) && target_was_voice))
            break;
          the_mode = MODE_HALFOP;
          to_list = &chptr->halfops;
          loc_to_list = &chptr->lochalfops;
        }

        if (whatt == MODE_DEL)
        {
          to_list = &chptr->peons;
          loc_to_list = &chptr->locpeons;
        }

        if (isdeop && (c == 'o') && whatt == MODE_ADD)
          change_channel_membership(chptr, &chptr->peons,
                                    &chptr->locpeons, who);

        /* Allow users to -h themselves */
        if ((whatt == MODE_DEL) && target_was_hop && (c == 'h') &&
            (who == source_p))
        {
          halfop_deop_self = 1;
        }

        /* let half-ops set voices */
        if (isok_c && (c == 'v'))
          halfop_setting_voice = 1;

        if (!isok && !halfop_deop_self && !halfop_setting_voice)
        {
          if (MyClient(source_p) && !errsent(SM_ERR_NOOPS, &errors_sent))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if (chptr->mode.mode & MODE_HIDEOPS)
        {
          if ((the_mode == MODE_CHANOP || the_mode == MODE_HALFOP)
              && whatt == MODE_DEL)
            if (MyClient(who))
              sendto_one(who, ":%s!%s@%s MODE %s -%c %s",
                         source_p->name, source_p->username,
                         source_p->host, chname, c, who->name);
        }

        tmp = strlen(arg);
        if (len + tmp + 2 >= MODEBUFLEN)
          break;

        if (c == 'h')
        {
          *mbufw_hops++ = plus;
          *mbufw_hops++ = c;
          strcpy(pbufw_hops, who->name);
          pbufw_hops += strlen(pbufw_hops);
          *pbufw_hops++ = ' ';
          strcpy(pbufw_hops_id, HasID(who) ? who->user->id : who->name);
          pbufw_hops_id += strlen(pbufw_hops_id);
          *pbufw_hops_id++ = ' ';
        }
        else
        {
          *mbuf2w++ = plus;
          *mbuf2w++ = c;
          strcpy(pbuf2w, who->name);
          pbuf2w += strlen(pbuf2w);
          *pbuf2w++ = ' ';
          strcpy(pbuf2w_id, HasID(who) ? who->user->id : who->name);
          pbuf2w_id += strlen(pbuf2w_id);
          *pbuf2w_id++ = ' ';
        }

        len += tmp + 1;
        opcnt++;

        if (change_channel_membership(chptr, to_list, loc_to_list, who))
        {
          if ((to_list == &chptr->chanops) && (whatt == MODE_ADD))
          {
            chptr->opcount++;
          }
        }

        /* Keep things in sync on +a channels... */
        if ((chptr->mode.mode & MODE_HIDEOPS) && MyClient(who))
        {
          /* send mass-server-op */
          if ((!target_was_op) && (whatt == MODE_ADD) &&
              ((to_list == &chptr->chanops) || (to_list == &chptr->halfops)))
            sync_oplists(chptr, who, 0, chname);
          /* send mass-sever-deop */
          else if (target_was_op && (whatt == MODE_DEL) &&
                   ((to_list != &chptr->chanops) &&
                    (to_list != &chptr->halfops)))
            sync_oplists(chptr, who, 1, chname);
          /* send single server voice */
          else if ((!target_was_voice) && (whatt == MODE_ADD) &&
                   (to_list == &chptr->voiced))
            sendto_one(who, ":%s MODE %s +v %s", me.name, chname, who->name);
          /* send single server devoice */
          else if (target_was_voice && (whatt == MODE_DEL) &&
                   (to_list != &chptr->voiced))
            sendto_one(who, ":%s MODE %s -v %s", me.name, chname, who->name);
        }

/*
 * XXX
 * This could take up a sizeable amount of bandwidth,
 * and once opped the clients can just /mode #chan +eI,
 * Disabled for now.. but *shrug*
 * -davidt
 */

#if 0
        if (MyClient(who))
        {
          if ((!target_was_op) && (whatt == MODE_ADD) &&
              ((to_list == &chptr->chanops) || (to_list == &chptr->halfops)))
          {
            send_mode_list(who, chname, &chptr->exceptlist, 'e', 0);
            send_mode_list(who, chname, &chptr->invexlist, 'I', 0);
          }
          else if (target_was_op && (whatt == MODE_DEL) &&
                   ((to_list != &chptr->chanops) &&
                    (to_list != &chptr->halfops)))
          {
            send_mode_list(who, chname, &chptr->exceptlist, 'e', 1);
            send_mode_list(who, chname, &chptr->invexlist, 'I', 1);
          }
        }
#endif

        break;

      case 'k':
        if (whatt == MODE_QUERY)
          break;
        if (parc-- <= 0)
        {
          /* allow arg-less mode -k */
          if (whatt == MODE_DEL)
            arg = "*";
          else
            break;
        }
        else
        {
          if (whatt == MODE_DEL)
          {
            arg = check_string(*parv++);
          }
          else
          {
            if MyClient
              (source_p) arg = fix_key(check_string(*parv++));
            else
              arg = fix_key_old(check_string(*parv++));
          }
        }

        if (keychange++)
          break;

        if (!*arg)
          break;

        if (!isok_c)
        {
          if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(source_p))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if ((tmp = strlen(arg)) > KEYLEN)
        {
          arg[KEYLEN] = '\0';
          tmp = KEYLEN;
        }

        if (len + tmp + 2 >= MODEBUFLEN)
          break;

        /* if there is already a key, and the client is adding one
         * remove the old one, then add the new one
         */

        if ((whatt == MODE_ADD) && *chptr->mode.key)
        {
          /* If the key is the same, don't do anything */

          if (!strcmp(chptr->mode.key, arg))
            break;

          if (chptr->mode.mode & MODE_HIDEOPS)
          {
            if (IsServer(source_p))
              sendto_channel_local(ONLY_CHANOPS_HALFOPS,
                                   chptr,
                                   ":%s!%s@%s MODE %s -k %s",
                                   me.name,
                                   source_p->username,
                                   source_p->host, chname, chptr->mode.key);
            else
              sendto_channel_local(ONLY_CHANOPS_HALFOPS,
                                   chptr,
                                   ":%s!%s@%s MODE %s -k %s",
                                   source_p->name,
                                   source_p->username,
                                   source_p->host, chname, chptr->mode.key);
          }
          else
          {
            if (IsServer(source_p))
              sendto_channel_local(ALL_MEMBERS,
                                   chptr,
                                   ":%s!%s@%s MODE %s -k %s",
                                   me.name,
                                   source_p->username,
                                   source_p->host, chname, chptr->mode.key);
            else
              sendto_channel_local(ALL_MEMBERS,
                                   chptr,
                                   ":%s!%s@%s MODE %s -k %s",
                                   source_p->name,
                                   source_p->username,
                                   source_p->host, chname, chptr->mode.key);
          }

          sendto_server(client_p, source_p, chptr, NOCAPS, NOCAPS, LL_ICLIENT,  /* but not channel */
                        ":%s MODE %s -k %s",
                        source_p->name, chname, chptr->mode.key);
        }

        if (whatt == MODE_DEL)
        {
          if ((arg[0] == '*') && (arg[1] == '\0'))
            arg = chptr->mode.key;
          else
          {
            if (strcmp(arg, chptr->mode.key))
              break;
          }
        }

        *mbufw++ = plus;
        *mbufw++ = 'k';
        strcpy(pbufw, arg);
        pbufw += strlen(pbufw);
        *pbufw++ = ' ';
        len += tmp + 1;

        strcpy(pbufw_id, arg);
        pbufw_id += strlen(pbufw_id);
        *pbufw_id++ = ' ';

        if (whatt == MODE_DEL)
          *chptr->mode.key = '\0';
        else
        {
          /* chptr was zeroed */
          strncpy_irc(chptr->mode.key, arg, KEYLEN);
        }

        break;

      case 'I':
        if (whatt == MODE_QUERY || parc-- <= 0)
        {
          if (!MyClient(source_p))
            break;
          if (errsent(SM_ERR_RPL_I, &errors_sent))
            break;
          /* don't allow a non chanop to see the invex list
           */
          if (isok_c)
          {
            for (ptr = chptr->invexlist.head; ptr; ptr = ptr->next)
            {
              banptr = ptr->data;

              sendto_one(client_p, form_str(RPL_INVITELIST),
                         me.name, client_p->name,
                         chname, banptr->banstr, banptr->who, banptr->when);
            }

            sendto_one(source_p, form_str(RPL_ENDOFINVITELIST),
                       me.name, source_p->name, chname);
          }
          else
          {
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          }
          break;
        }
        arg = check_string(*parv++);

        if (MyClient(source_p) && opcnt >= MAXMODEPARAMS)
          break;

        if (!isok_c)
        {
          if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(source_p))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        /* user-friendly ban mask generation, taken
           ** from Undernet's ircd  -orabidoo
         */
        if (MyClient(source_p))
          arg = collapse(pretty_mask(arg));

        if (*arg == ':')
        {
          parc--;
          parv++;
          break;
        }

        tmp = strlen(arg);
        if (len + tmp + 2 >= MODEBUFLEN)
          break;

        if (!
            (((whatt & MODE_ADD)
              && !add_id(source_p, chptr, arg, CHFL_INVEX))
             || ((whatt & MODE_DEL) && !del_id(chptr, arg, CHFL_INVEX))))
          break;

        /* This stuff can go back in when all servers understand +e 
         * with the pbufw_ex nonsense removed
         */

        len += tmp + 1;
        opcnt++;

        *mbufw_invex++ = plus;
        *mbufw_invex++ = 'I';
        strcpy(pbufw_invex, arg);
        pbufw_invex += strlen(pbufw_invex);
        *pbufw_invex++ = ' ';

        break;

      case 'e':
        if (whatt == MODE_QUERY || parc-- <= 0)
        {
          if (!MyClient(source_p))
            break;
          if (errsent(SM_ERR_RPL_E, &errors_sent))
            break;
          /* don't allow a non chanop to see the exception list
           * suggested by Matt on operlist nov 25 1998
           */
          if (isok_c)
          {
            for (ptr = chptr->exceptlist.head; ptr; ptr = ptr->next)
            {
              banptr = ptr->data;
              sendto_one(client_p, form_str(RPL_EXCEPTLIST),
                         me.name, client_p->name,
                         chname, banptr->banstr, banptr->who, banptr->when);
            }

            sendto_one(source_p, form_str(RPL_ENDOFEXCEPTLIST),
                       me.name, source_p->name, chname);
          }
          else
          {
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          }
          break;
        }
        arg = check_string(*parv++);

        if (MyClient(source_p) && opcnt >= MAXMODEPARAMS)
          break;

        if (!isok_c)
        {
          if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(source_p))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        /* user-friendly ban mask generation, taken
           ** from Undernet's ircd  -orabidoo
         */
        if (MyClient(source_p))
          arg = collapse(pretty_mask(arg));

        if (*arg == ':')
        {
          parc--;
          parv++;
          break;
        }

        tmp = strlen(arg);
        if (len + tmp + 2 >= MODEBUFLEN)
          break;

        if (!
            (((whatt & MODE_ADD)
              && !add_id(source_p, chptr, arg, CHFL_EXCEPTION))
             || ((whatt & MODE_DEL) && !del_id(chptr, arg, CHFL_EXCEPTION))))
          break;

        /* This stuff can go back in when all servers understand +e 
         * with the pbufw_ex nonsense removed 
         */

        len += tmp + 1;
        opcnt++;

        *mbufw_ex++ = plus;
        *mbufw_ex++ = 'e';
        strcpy(pbufw_ex, arg);
        pbufw_ex += strlen(pbufw_ex);
        *pbufw_ex++ = ' ';

        break;

      case 'b':
        if (whatt == MODE_QUERY || parc-- <= 0)
        {
          if (!MyClient(source_p))
            break;

          if (errsent(SM_ERR_RPL_B, &errors_sent))
            break;

          if (!(chptr->mode.mode & MODE_HIDEOPS) || isok_c)
          {
            for (ptr = chptr->banlist.head; ptr; ptr = ptr->next)
            {
              banptr = ptr->data;
              sendto_one(client_p, form_str(RPL_BANLIST),
                         me.name, client_p->name,
                         chname, banptr->banstr, banptr->who, banptr->when);
            }
          }
          else
          {
            for (ptr = chptr->banlist.head; ptr; ptr = ptr->next)
            {
              banptr = ptr->data;
              sendto_one(client_p, form_str(RPL_BANLIST),
                         me.name, client_p->name,
                         chname, banptr->banstr, me.name, banptr->when);
            }
          }

          sendto_one(source_p, form_str(RPL_ENDOFBANLIST),
                     me.name, source_p->name, chname);
          break;
        }

        arg = check_string(*parv++);

        if (MyClient(source_p) && opcnt >= MAXMODEPARAMS)
          break;

        /* allow ops and halfops to set bans */
        if (!isok_c)
        {
          if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(source_p))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        /* user-friendly ban mask generation, taken
           ** from Undernet's ircd  -orabidoo
         */
        if (MyClient(source_p))
        {
          if ((*arg == ':') && (whatt & MODE_ADD))
          {
            parc--;
            parv++;
            break;
          }
          arg = collapse(pretty_mask(arg));
        }

        tmp = strlen(arg);
        if (len + tmp + 2 >= MODEBUFLEN)
          break;

        if (!
            (((whatt & MODE_ADD)
              && !add_id(source_p, chptr, arg, CHFL_BAN))
             || ((whatt & MODE_DEL) && !del_id(chptr, arg, CHFL_BAN))))
          break;

        *mbufw++ = plus;
        *mbufw++ = 'b';
        strcpy(pbufw, arg);
        pbufw += strlen(pbufw);
        *pbufw++ = ' ';
        len += tmp + 1;
        opcnt++;

        break;

      case 'l':
        if (whatt == MODE_QUERY)
          break;
        /* allow ops and halfops to set limits */
        if (!isok_c)
        {
          if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(source_p))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);

          if (whatt == MODE_ADD && parc-- > 0)
            parv++;

          break;
        }

        if (limitset++)
        {
          if (whatt == MODE_ADD && parc-- > 0)
            parv++;
          break;
        }

        if (whatt == MODE_ADD)
        {
          if (parc-- <= 0)
          {
            if (MyClient(source_p))
              sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
                         me.name, source_p->name, "MODE +l");
            break;
          }

          arg = check_string(*parv++);
          if ((nusers = atoi(arg)) <= 0)
            break;

          ircsprintf(numeric, "%d", nusers);
          if ((tmpc = strchr(numeric, ' ')))
            *tmpc = '\0';
          arg = numeric;
          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          chptr->mode.limit = nusers;
          chptr->mode.mode |= MODE_LIMIT;

          *mbufw++ = '+';
          *mbufw++ = 'l';
          strcpy(pbufw, arg);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          len += tmp + 1;

          strcpy(pbufw_id, arg);
          pbufw_id += strlen(pbufw_id);
          *pbufw_id++ = ' ';
        }
        else
        {
          chptr->mode.limit = 0;
          chptr->mode.mode &= ~MODE_LIMIT;
          *mbufw++ = '-';
          *mbufw++ = 'l';
        }

        break;

        /* Traditionally, these are handled separately
         * but I decided to combine them all into this one case
         * statement keeping it all sane
         *
         * The disadvantage is a lot more code duplicated ;-/
         */

      case 'i':
        if (whatt == MODE_QUERY)        /* shouldn't happen. */
          break;
        if (!isok_c)
        {
          if (MyClient(source_p) && !errsent(SM_ERR_NOOPS, &errors_sent))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if (MyClient(source_p))
        {
          if (done_i)
            break;
          else
            done_i = YES;
        }

        if (whatt == MODE_ADD)
        {
          if (len + 2 >= MODEBUFLEN)
            break;
          chptr->mode.mode |= MODE_INVITEONLY;
          *mbufw++ = '+';
          *mbufw++ = 'i';
          len += 2;
        }
        else
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          while ((ptr = chptr->invites.head))
            del_invite(chptr, ptr->data);

          chptr->mode.mode &= ~MODE_INVITEONLY;
          *mbufw++ = '-';
          *mbufw++ = 'i';
          len += 2;
        }
        break;

      case 'm':
        if (!isok_c)
        {
          if (MyClient(source_p) && !errsent(SM_ERR_NOOPS, &errors_sent))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if (MyClient(source_p))
        {
          if (done_m)
            break;
          else
            done_m = YES;
        }

        if (whatt == MODE_ADD)
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          chptr->mode.mode |= MODE_MODERATED;
          *mbufw++ = '+';
          *mbufw++ = 'm';
          len += 2;
        }
        else
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          chptr->mode.mode &= ~MODE_MODERATED;
          *mbufw++ = '-';
          *mbufw++ = 'm';
          len += 2;
        }
        break;

      case 'n':
        if (!isok_c)
        {
          if (MyClient(source_p) && !errsent(SM_ERR_NOOPS, &errors_sent))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if (MyClient(source_p))
        {
          if (done_n)
            break;
          else
            done_n = YES;
        }

        if (whatt == MODE_ADD)
        {
          if (len + 2 >= MODEBUFLEN)
            break;
          chptr->mode.mode |= MODE_NOPRIVMSGS;
          *mbufw++ = '+';
          *mbufw++ = 'n';
          len += 2;
        }
        else
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          chptr->mode.mode &= ~MODE_NOPRIVMSGS;
          *mbufw++ = '-';
          *mbufw++ = 'n';
          len += 2;
        }
        break;

      case 'a':
        if (!isok_c)
        {
          if (MyClient(source_p) && !errsent(SM_ERR_NOOPS, &errors_sent))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if (MyClient(source_p))
        {
          if (done_a)
            break;
          else
            done_a = YES;
        }

        if (whatt == MODE_ADD)
        {
          if (len + 2 >= MODEBUFLEN)
            break;
          if (!(chptr->mode.mode & MODE_HIDEOPS))
            sync_channel_oplists(chptr, 1);
          chptr->mode.mode |= MODE_HIDEOPS;
          *mbufw_aops++ = '+';
          *mbufw_aops++ = 'a';
          len += 2;
        }
        else
        {
          if (len + 2 >= MODEBUFLEN)
            break;
          if (chptr->mode.mode & MODE_HIDEOPS)
            sync_channel_oplists(chptr, 0);
          chptr->mode.mode &= ~MODE_HIDEOPS;
          *mbufw_aops++ = '-';
          *mbufw_aops++ = 'a';
          len += 2;
        }
        break;

      case 'p':
        if (!isok_c)
        {
          if (MyClient(source_p) && !errsent(SM_ERR_NOOPS, &errors_sent))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if (MyClient(source_p))
        {
          if (done_p)
            break;
          else
            done_p = YES;
        }

        if (whatt == MODE_ADD)
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          chptr->mode.mode |= MODE_PRIVATE;
          *mbufw++ = '+';
          *mbufw++ = 'p';
          len += 2;
        }
        else
        {
          if (len + 2 >= MODEBUFLEN)
            break;
          chptr->mode.mode &= ~MODE_PRIVATE;
          *mbufw++ = '-';
          *mbufw++ = 'p';
          len += 2;
        }
        break;

      case 's':
        if (!isok_c)
        {
          if (MyClient(source_p) && !errsent(SM_ERR_NOOPS, &errors_sent))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if (MyClient(source_p))
        {
          if (done_s)
            break;
          else
            done_s = YES;
        }

        if (whatt == MODE_ADD)
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          chptr->mode.mode |= MODE_SECRET;
          *mbufw++ = '+';
          *mbufw++ = 's';
          len += 2;
        }
        else
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          chptr->mode.mode &= ~MODE_SECRET;
          *mbufw++ = '-';
          *mbufw++ = 's';
          len += 2;
        }
        break;

      case 't':
        if (!isok_c)
        {
          if (MyClient(source_p) && !errsent(SM_ERR_NOOPS, &errors_sent))
            sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, source_p->name, chname);
          break;
        }

        if (MyClient(source_p))
        {
          if (done_t)
            break;
          else
            done_t = YES;
        }

        if (whatt == MODE_ADD)
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          chptr->mode.mode |= MODE_TOPICLIMIT;
          *mbufw++ = '+';
          *mbufw++ = 't';
          len += 2;
        }
        else
        {
          if (len + 2 >= MODEBUFLEN)
            break;

          chptr->mode.mode &= ~MODE_TOPICLIMIT;
          *mbufw++ = '-';
          *mbufw++ = 't';
          len += 2;
        }
        break;

      default:
        if (whatt == MODE_QUERY)
          break;

        /* only one "UNKNOWNMODE" per mode... we don't want
           ** to generate a storm, even if it's just to a 
           ** local client  -orabidoo
         */
        if (MyClient(source_p) && !errsent(SM_ERR_UNKNOWN, &errors_sent))
          sendto_one(source_p, form_str(ERR_UNKNOWNMODE), me.name,
                     source_p->name, c);
        break;
    }
  }

  /*
     ** WHEW!!  now all that's left to do is put the various bufs
     ** together and send it along.
   */

  *mbufw = *mbuf2w = *pbufw = *pbuf2w = *mbufw_ex = *pbufw_ex =
    *mbufw_invex = *pbufw_invex =
    *mbufw_hops = *pbufw_hops = *pbufw_id = *pbufw_hops_id = *pbuf2w_id =
    '\0';

  collapse_signs(modebuf);
  collapse_signs(modebuf2);
  collapse_signs(modebuf_ex);
  collapse_signs(modebuf_invex);
  collapse_signs(modebuf_hops);
  /* modebuf_aops only ever holds one mode */

  if (chptr->mode.mode & MODE_HIDEOPS)
    type = ONLY_CHANOPS_HALFOPS;
  else
    type = ALL_MEMBERS;

  /* 
   * Standard modes, seen by everyone
   * on +a channels pretend its a server mode to nonops
   */
  if (*modebuf)
  {
    if (IsServer(source_p))
      sendto_channel_local(ALL_MEMBERS,
                           chptr,
                           ":%s MODE %s %s %s",
                           me.name, chname, modebuf, parabuf);
    else
    {
      sendto_channel_local(type,
                           chptr,
                           ":%s!%s@%s MODE %s %s %s",
                           source_p->name,
                           source_p->username,
                           source_p->host, chname, modebuf, parabuf);
      if (chptr->mode.mode & MODE_HIDEOPS)
        sendto_channel_local(NON_CHANOPS,
                             chptr,
                             ":%s MODE %s %s %s",
                             me.name, chname, modebuf, parabuf);
    }

    sendto_server(client_p, source_p, chptr, NOCAPS, CAP_UID, LL_ICLIENT,       /* but not channel */
                  ":%s MODE %s %s %s",
                  source_p->name, chptr->chname, modebuf, parabuf);

    sendto_server(client_p, source_p, chptr, CAP_UID, NOCAPS, LL_ICLIENT,       /* but not server */
                  ":%s MODE %s %s %s",
                  ID(source_p), chptr->chname, modebuf, parabuf);
  }

  /*
   * Hidden modes (currently +o and +b, [also +h see modebuf_hops])
   * +o seen only by chanops/halfops on +a channels
   * +b should be seen by everyone, but ops only on +a channel
   */
  if (*modebuf2)
  {
    if (IsServer(source_p))
      sendto_channel_local(type,
                           chptr,
                           ":%s MODE %s %s %s",
                           me.name, chname, modebuf2, parabuf2);
    else
      sendto_channel_local(type,
                           chptr,
                           ":%s!%s@%s MODE %s %s %s",
                           source_p->name,
                           source_p->username,
                           source_p->host, chname, modebuf2, parabuf2);

    sendto_server(client_p, source_p, chptr, NOCAPS, CAP_UID, LL_ICLIENT,       /* but not channel */
                  ":%s MODE %s %s %s",
                  source_p->name, chptr->chname, modebuf2, parabuf2);

    sendto_server(client_p, source_p, chptr, CAP_UID, NOCAPS, LL_ICLIENT,       /* but not channel */
                  ":%s MODE %s %s %s",
                  ID(source_p), chptr->chname, modebuf2, parabuf2_id);
  }

  /*
   * mode +e, seen by everyone.
   * Only send remotely to servers with EX
   * On +a channels pretend to nonops that it's a server mode.
   */
  if (*modebuf_ex)
  {
    if (IsServer(source_p))
      sendto_channel_local(ONLY_CHANOPS_HALFOPS,
                           chptr,
                           ":%s MODE %s %s %s",
                           me.name, chname, modebuf_ex, parabuf_ex);
    else
    {
      sendto_channel_local(ONLY_CHANOPS_HALFOPS,
                           chptr,
                           ":%s!%s@%s MODE %s %s %s",
                           source_p->name,
                           source_p->username,
                           source_p->host, chname, modebuf_ex, parabuf_ex);
    }

    sendto_server(client_p, source_p, chptr, CAP_EX, CAP_UID, LL_ICLIENT,       /* but not channel */
                  ":%s MODE %s %s %s",
                  source_p->name, chptr->chname, modebuf_ex, parabuf_ex);

    sendto_server(client_p, source_p, chptr, CAP_EX | CAP_UID, NOCAPS, LL_ICLIENT,      /* but not channel */
                  ":%s MODE %s %s %s",
                  ID(source_p), chptr->chname, modebuf_ex, parabuf_ex);
  }

  /*
   * mode +I, seen by ops only.
   * Only send remotely to servers with IE
   */
  if (*modebuf_invex)
  {
    if (IsServer(source_p))
      sendto_channel_local(ONLY_CHANOPS_HALFOPS,
                           chptr,
                           ":%s MODE %s %s %s",
                           me.name, chname, modebuf_invex, parabuf_invex);
    else
      sendto_channel_local(ONLY_CHANOPS_HALFOPS,
                           chptr,
                           ":%s!%s@%s MODE %s %s %s",
                           source_p->name,
                           source_p->username,
                           source_p->host,
                           chname, modebuf_invex, parabuf_invex);

    sendto_server(client_p, source_p, chptr, CAP_IE, CAP_UID, LL_ICLIENT,       /* but not channel */
                  ":%s MODE %s %s %s",
                  source_p->name, chptr->chname,
                  modebuf_invex, parabuf_invex);
    sendto_server(client_p, source_p, chptr, CAP_IE | CAP_UID, NOCAPS, LL_ICLIENT,      /* but not channel */
                  ":%s MODE %s %s %s",
                  ID(source_p), chptr->chname, modebuf_invex, parabuf_invex);
  }

  /*
   * mode +h, seen only by chanops/halfops on +a channels
   * Only send remotely to servers with HOPS
   */
  if (*modebuf_hops)
  {
    if (IsServer(source_p))
      sendto_channel_local(type,
                           chptr,
                           ":%s MODE %s %s %s",
                           me.name, chname, modebuf_hops, parabuf_hops);
    else
      sendto_channel_local(type,
                           chptr,
                           ":%s!%s@%s MODE %s %s %s",
                           source_p->name,
                           source_p->username,
                           source_p->host,
                           chname, modebuf_hops, parabuf_hops);

    sendto_server(client_p, source_p, chptr, CAP_HOPS, CAP_UID, LL_ICLIENT,     /* but not channel */
                  ":%s MODE %s %s %s",
                  source_p->name, chptr->chname, modebuf_hops, parabuf_hops);

    sendto_server(client_p, source_p, chptr, CAP_HOPS | CAP_UID, NOCAPS, LL_ICLIENT,    /* but not channel */
                  ":%s MODE %s %s %s",
                  ID(source_p), chptr->chname, modebuf_hops, parabuf_hops_id);
  }

  /*
   * mode +a, seen by everyone.
   * Only send remotely to servers with ANONOPS
   * On +a channels pretend to nonops that it's a server mode.
   */
  if (*modebuf_aops)
  {
    if (IsServer(source_p))
      sendto_channel_local(ALL_MEMBERS,
                           chptr,
                           ":%s MODE %s %s", me.name, chname, modebuf_aops);
    else
    {
      sendto_channel_local(type,
                           chptr,
                           ":%s!%s@%s MODE %s %s",
                           source_p->name,
                           source_p->username,
                           source_p->host, chname, modebuf_aops);
      if (chptr->mode.mode & MODE_HIDEOPS)
        sendto_channel_local(NON_CHANOPS,
                             chptr,
                             ":%s MODE %s %s", me.name, chname, modebuf_aops);
    }

    sendto_server(client_p, source_p, chptr, CAP_AOPS, CAP_UID, LL_ICLIENT,     /* but not channel */
                  ":%s MODE %s %s",
                  source_p->name, chptr->chname, modebuf_aops);

    sendto_server(client_p, source_p, chptr, CAP_AOPS | CAP_UID, NOCAPS, LL_ICLIENT,    /* but not channel */
                  ":%s MODE %s %s",
                  ID(source_p), chptr->chname, modebuf_aops);
  }

  return;
}
#endif


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
  assert(0 != name);

  for (; *name; ++name)
  {
    if (!IsChanChar(*name))
      return 0;
  }

  return 1;
}

/*
 * get_channel
 * inputs       - client pointer
 *              - channel name
 *              - flag == CREATE if non existent
 * output       - returns channel block
 *
 *  Get Channel block for chname (and allocate a new channel
 *  block, if it didn't exist before).
 */
struct Channel *
get_channel(struct Client *client_p, char *chname, int flag)
{
  struct Channel *chptr;
  int len;

  if (BadPtr(chname))
    return NULL;

  len = strlen(chname);
  if (len > CHANNELLEN)
  {
    if (IsServer(client_p))
    {
      sendto_realops_flags(FLAGS_DEBUG, L_ALL,
                           "*** Long channel name from %s (%d > %d): %s",
                           client_p->name,
                           len,
                           CHANNELLEN,
                           chname);
    }
    len = CHANNELLEN;
    *(chname + CHANNELLEN) = '\0';
  }

  if ((chptr = hash_find_channel(chname, NULL)))
    return (chptr);

  if (flag == CREATE)
  {
#if 0
    chptr = (struct Channel *)MyMalloc(sizeof(struct Channel) + len + 1);
#endif
    chptr = BlockHeapAlloc(channel_heap);
    memset(chptr, 0, sizeof(*chptr)-CHANNELLEN);
    /*
     * NOTE: strcpy ok here, we have allocated strlen + 1
     */
    strcpy(chptr->chname, chname);

    if (GlobalChannelList)
      GlobalChannelList->prevch = chptr;

    chptr->prevch = NULL;
    chptr->nextch = GlobalChannelList;
    GlobalChannelList = chptr;
    chptr->channelts = CurrentTime;     /* doesn't hurt to set it here */
    add_to_channel_hash_table(chname, chptr);
    Count.chan++;
  }
  return chptr;
}


/*
**  Subtract one user from channel (and free channel
**  block, if channel became empty).
*/
static void
sub1_from_channel(struct Channel *chptr, int perm)
{
  if (--chptr->users <= 0)
  {
    chptr->users = 0;           /* if chptr->users < 0, make sure it sticks at 0
                                 * It should never happen but...
                                 */
    /* persistent channel */
    if (perm == 0 || (chptr->channelts + ConfigChannel.persist_time) > CurrentTime)
      destroy_channel(chptr);
  }
}

/*
 * free_channel_list
 *
 * inputs       - pointer to dlink_list
 * output       - NONE
 * side effects -
 */
static void
free_channel_list(dlink_list * list)
{
  dlink_node *ptr;
  dlink_node *next_ptr;
  struct Ban *actualBan;

  for (ptr = list->head; ptr; ptr = next_ptr)
  {
    next_ptr = ptr->next;

    actualBan = ptr->data;
    MyFree(actualBan->banstr);
    MyFree(actualBan->who);
    BlockHeapFree(ban_heap, actualBan);

    free_dlink_node(ptr);
  }
}

/*
 * cleanup_channels
 *
 * inputs       - not used
 * output       - none
 * side effects - persistent channels... vchans get a long long timeout
 */
void
cleanup_channels(void *unused)
{
  struct Channel *chptr;
  struct Channel *next_chptr;

  if (uplink != NULL)
  {
    /* XXX The assert disapears when NDEBUG is set */
    assert(MyConnect(uplink) == 1);

    if (!MyConnect(uplink))
    {
      ilog(L_ERROR, "non-local uplink [%s]", uplink->name);
      uplink = NULL;
    }
  }

  for (chptr = GlobalChannelList; chptr; chptr = next_chptr)
  {
    next_chptr = chptr->nextch;

    if (IsVchan(chptr))
    {
      if (IsVchanTop(chptr))
      {
        chptr->users_last = CurrentTime;
      }
      else
      {
        if ((CurrentTime - chptr->users_last >= MAX_VCHAN_TIME))
        {
          if (chptr->users == 0)
          {
            if (uplink && IsCapable(uplink, CAP_LL))
            {
              sendto_one(uplink, ":%s DROP %s", me.name, chptr->chname);
            }
            destroy_channel(chptr);
          }
          else
            chptr->users_last = CurrentTime;
        }
      }
    }
    else
    {
      if(chptr->users == 0)
      {
        if((chptr->channelts + ConfigChannel.persist_time) > CurrentTime)
	{
	  if(uplink && IsCapable(uplink, CAP_LL))
	    sendto_one(uplink, ":%s DROP %s", me.name, chptr->chname);
	  destroy_channel(chptr);
	}
      }
      else
      {
        if ((CurrentTime - chptr->users_last >= CLEANUP_CHANNELS_TIME))
        {
          if (uplink
                 && IsCapable(uplink, CAP_LL) && (chptr->locusers == 0))
          {
            sendto_one(uplink, ":%s DROP %s", me.name, chptr->chname);
            destroy_channel(chptr);
          }
          else
            chptr->users_last = CurrentTime;
        }
        else
          chptr->users_last = CurrentTime;
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
  dlink_node *ptr;
  struct Channel *root_chptr;
  dlink_node *m;

  /* Don't ever delete the top of a chain of vchans! */
  if (IsVchanTop(chptr))
    return;

  if (IsVchan(chptr))
  {
    root_chptr = chptr->root_chptr;
    /* remove from vchan double link list */
    m = dlinkFind(&root_chptr->vchan_list, chptr);
    dlinkDelete(m, &root_chptr->vchan_list);
    free_dlink_node(m);
  }

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
  delete_members(chptr, &chptr->voiced);
  delete_members(chptr, &chptr->peons);
  delete_members(chptr, &chptr->halfops);

  delete_members(chptr, &chptr->locchanops);
  delete_members(chptr, &chptr->locvoiced);
  delete_members(chptr, &chptr->locpeons);
  delete_members(chptr, &chptr->lochalfops);

  while ((ptr = chptr->invites.head))
    del_invite(chptr, ptr->data);

  /* free all bans/exceptions/denies */
  free_channel_list(&chptr->banlist);
  free_channel_list(&chptr->exceptlist);
  free_channel_list(&chptr->invexlist);

  /* This should be redundant at this point but JIC */
  chptr->banlist.head = chptr->exceptlist.head = chptr->invexlist.head = NULL;

  chptr->banlist.tail = chptr->exceptlist.tail = chptr->invexlist.tail = NULL;

  if (chptr->prevch)
    chptr->prevch->nextch = chptr->nextch;
  else
    GlobalChannelList = chptr->nextch;
  if (chptr->nextch)
    chptr->nextch->prevch = chptr->prevch;

  MyFree(chptr->topic_info);

  del_from_channel_hash_table(chptr->chname, chptr);
  if (ServerInfo.hub == 1)
  {
    for (m = lazylink_channels.head; m; m = m->next)
    {
      if (m->data != chptr)
        continue;
      dlinkDelete(m, &lazylink_channels);
      free_dlink_node(m);
      break;
    }
  }
#if 0
  MyFree((char *)chptr);
#endif
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
  dlink_node *ptr_ch;
  dlink_node *next_ptr_ch;

  struct Client *who;

  for (ptr = list->head; ptr; ptr = next_ptr)
  {
    next_ptr = ptr->next;
    who = (struct Client *)ptr->data;

    /* remove reference to chptr from who */
    for (ptr_ch = who->user->channel.head; ptr_ch; ptr_ch = next_ptr_ch)
    {
      next_ptr_ch = ptr_ch->next;

      if (ptr_ch->data == chptr)
      {
        dlinkDelete(ptr_ch, &who->user->channel);
        free_dlink_node(ptr_ch);
        break;
      }
    }

    who->user->joined--;

    if (IsVchan(chptr))
      del_vchan_from_client_cache(who, chptr);

    /* remove reference to who from chptr */
    dlinkDelete(ptr, list);
    free_dlink_node(ptr);
  }
}

/*
 * set_channel_mode_flags
 *
 * inputs       - pointer to array of strings for chanops, voiced,
 *                halfops,peons
 *              - pointer to channel
 *              - pointer to source
 * output       - none
 * side effects -
 */
void
set_channel_mode_flags(char flags_ptr[4][2],
                       struct Channel *chptr, struct Client *source_p)
{
  if (chptr->mode.mode & MODE_HIDEOPS && !is_any_op(chptr, source_p))
  {
    flags_ptr[0][0] = '\0';
    flags_ptr[1][0] = '\0';
    flags_ptr[2][0] = '\0';
    flags_ptr[3][0] = '\0';
  }
  else
  {
    flags_ptr[0][0] = '@';
    flags_ptr[1][0] = '%';
    flags_ptr[2][0] = '+';
    flags_ptr[3][0] = '\0';

    flags_ptr[0][1] = '\0';
    flags_ptr[1][1] = '\0';
    flags_ptr[2][1] = '\0';
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
{
  int mlen;
  int sublists_done = 0;
  int tlen;
  int cur_len;
  char lbuf[BUFSIZE];
  char *t;
  int reply_to_send = NO;
  dlink_node *members_ptr[MAX_SUBLISTS];
  char show_flags[MAX_SUBLISTS][2];
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
    members_ptr[1] = chptr->halfops.head;
    members_ptr[2] = chptr->voiced.head;
    members_ptr[3] = chptr->peons.head;
    is_member = IsMember(source_p, chptr);

    /* Note: This code will show one chanop followed by one voiced followed
     *  by one halfop followed by one peon followed by one chanop...
     * XXX - this is very predictable, randomise it later.
     */

    while (sublists_done != (1 << MAX_SUBLISTS) - 1)
    {
      for (i = 0; i < MAX_SUBLISTS; i++)
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
  dlink_node *inv;

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
  inv = make_dlink_node();
  dlinkAdd(who, inv, &chptr->invites);

  /*
   * add channel to the end of the client invite list
   */
  inv = make_dlink_node();
  dlinkAdd(chptr, inv, &who->user->invited);
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
  dlink_node *ptr;

  for (ptr = chptr->invites.head; ptr; ptr = ptr->next)
  {
    if (ptr->data == who)
    {
      dlinkDelete(ptr, &chptr->invites);
      free_dlink_node(ptr);
      break;
    }
  }

  for (ptr = who->user->invited.head; ptr; ptr = ptr->next)
  {
    if (ptr->data == chptr)
    {
      dlinkDelete(ptr, &who->user->invited);
      free_dlink_node(ptr);
      break;
    }
  }
}

/*
 * channel_chanop_or_voice
 * inputs       - pointer to channel
 *              - pointer to client
 * output       - string either @,+% or"" depending on whether
 *                chanop, voiced, halfop or user
 * side effects -
 */
char *
channel_chanop_or_voice(struct Channel *chptr, struct Client *target_p)
{
  if (find_user_link(&chptr->chanops, target_p))
    return ("@");
  else if (find_user_link(&chptr->halfops, target_p))
    return ("%");
  else if (find_user_link(&chptr->voiced, target_p))
    return ("+");
  return ("");
}

/*
 * sync_oplists
 *
 * inputs       - pointer to channel
 *              - pointer to client
 * output       - none
 * side effects - Sends MODE +o/+h/+v list to user
 *                (for +a channels)
 */
void
sync_oplists(struct Channel *chptr, struct Client *target_p,
             int clear, const char *name)
{
  send_oplist(name, target_p, &chptr->chanops, "o", clear);
  send_oplist(name, target_p, &chptr->halfops, "h", clear);
  send_oplist(name, target_p, &chptr->voiced, "v", clear);
}

static void
send_oplist(const char *chname, struct Client *client_p,
            dlink_list * list, char *prefix, int clear)
{
  dlink_node *ptr;
  int cur_modes = 0;            /* no of chars in modebuf */
  struct Client *target_p;
  int data_to_send = 0;
  char mcbuf[6] = "";
  char opbuf[MODEBUFLEN];
  char *t;

  *mcbuf = *opbuf = '\0';
  t = opbuf;

  for (ptr = list->head; ptr && ptr->data; ptr = ptr->next)
  {
    if (cur_modes == 0)
    {
      mcbuf[cur_modes++] = (clear ? '-' : '+');
    }

    target_p = ptr->data;
    /* Don't include this client... */
    if (target_p == client_p)
      continue;

    mcbuf[cur_modes++] = *prefix;

    ircsprintf(t, "%s ", target_p->name);
    t += strlen(t);

    data_to_send = 1;

    if (cur_modes == (MAXMODEPARAMS + 1))       /* '+' and modes */
    {
      *t = '\0';
      mcbuf[cur_modes] = '\0';
      sendto_one(client_p, ":%s MODE %s %s %s", me.name,
                 chname, mcbuf, opbuf);

      cur_modes = 0;
      *mcbuf = *opbuf = '\0';
      t = opbuf;
      data_to_send = 0;
    }
  }

  if (data_to_send)
  {
    *t = '\0';
    mcbuf[cur_modes] = '\0';
    sendto_one(client_p, ":%s MODE %s %s %s", me.name, chname, mcbuf, opbuf);
  }
}

void
sync_channel_oplists(struct Channel *chptr, int clear)
{
  dlink_node *ptr;
  dlink_list *list;
  struct Client *target_p;

  list = &chptr->peons;
  for (ptr = list->head; ptr && ptr->data; ptr = ptr->next)
  {
    target_p = ptr->data;
    if (MyClient(target_p))
      sync_oplists(chptr, target_p, clear, RootChan(chptr)->chname);
  }
  list = &chptr->voiced;
  for (ptr = list->head; ptr && ptr->data; ptr = ptr->next)
  {
    target_p = ptr->data;
    if (MyClient(target_p))
      sync_oplists(chptr, target_p, clear, RootChan(chptr)->chname);
  }
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
        sendto_realops_flags(FLAGS_BOTS, L_ALL,
                             "User %s (%s@%s) trying to join %s is a possible spambot",
                             source_p->name, source_p->username,
                             source_p->host, name);
      else
        sendto_realops_flags(FLAGS_BOTS, L_ALL,
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
