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
#include "s_serv.h"       /* captab */
#include "s_user.h"
#include "send.h"
#include "whowas.h"
#include "s_conf.h" /* ConfigFileEntry */
#include "vchannel.h"
#include "event.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "memdebug.h"

#include "s_log.h"

struct Channel *GlobalChannelList = NullChn;

static  int     add_id (struct Client *, struct Channel *, char *, int);
static  int     del_id (struct Channel *, char *, int);
static  void    clear_channel_list(int type, struct Channel *chptr, 
				   struct Client *sptr,
				   dlink_list *ptr, char flag);

static  void    free_channel_list(dlink_list *list);
static  void    ban_free(dlink_node *ptr);
static  void    sub1_from_channel (struct Channel *);
static  void    destroy_channel(struct Channel *);

static void send_mode_list(struct Client *, char *, dlink_list *,
                           char, int);

static void sync_channel_oplists(struct Channel *,
                                 int);
static void sync_oplists(struct Channel *,
                         struct Client *, int);
static void send_oplist(char *, struct Client *,
                        dlink_list *, char *, int);

static void send_members(struct Client *cptr,
			 char *modebuf, char *parabuf,
			 struct Channel *chptr,
			 dlink_list *list,
			 char *op_flag );

void channel_member_list(struct Client *sptr,
			 dlink_list *list,
			 char *show_flag,
			 char *buf,
			 int mlen,
			 int *cur_len,
			 int *reply_to_send);

static void delete_members(dlink_list *list);

/* static functions used in set_mode */
static char* pretty_mask(char *);
static char *fix_key(char *);
static char *fix_key_old(char *);
static void collapse_signs(char *);
static int errsent(int,int *);

static int change_channel_membership(struct Channel *chptr,
				     dlink_list *to_list, struct Client *who);

/*
 * some buffers for rebuilding channel/nick lists with ,'s
 */

static  char    buf[BUFSIZE];
static  char    modebuf[MODEBUFLEN], modebuf2[MODEBUFLEN];
static  char    parabuf[MODEBUFLEN], parabuf2[MODEBUFLEN];

/*
 * check_string
 *
 * inputs	- string to check
 * output	- pointer to modified string
 * side effects - Fixes a string so that the first white space found
 *                becomes an end of string marker (`\0`).
 *                returns the 'fixed' string or "*" if the string
 *                was NULL length or a NULL pointer.
 */
static char* check_string(char* s)
{
  char* str = s;

  if (s == NULL)
    return "*";

  for ( ; *s; ++s)
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
 * create a string of form "foo!bar@fubar" given foo, bar and fubar
 * as the parameters.  If NULL, they become "*".
 */
static char* make_nick_user_host(const char* nick, 
                                 const char* name, const char* host)
{
  static char namebuf[NICKLEN + USERLEN + HOSTLEN + 6];
  int   n;
  char* s;
  const char* p;

  s = namebuf;

  for (p = nick, n = NICKLEN; *p && n--; )
    *s++ = *p++;
  *s++ = '!';
  for(p = name, n = USERLEN; *p && n--; )
    *s++ = *p++;
  *s++ = '@';
  for(p = host, n = HOSTLEN; *p && n--; )
    *s++ = *p++;
  *s = '\0';
  return namebuf;
}

/*
 * Ban functions to work with mode +b/e/d/I
 */
/* add the specified ID to the channel.. 
 *   -is 8/9/00 
 */

static  int     add_id(struct Client *cptr, struct Channel *chptr, 
			  char *banid, int type)
{
  dlink_list *list;
  dlink_node *ban;
  struct Ban *actualBan;

  /* dont let local clients overflow the banlist */
  if ((!IsServer(cptr)) && (chptr->num_bed >= MAXBANS))
    {
      if (MyClient(cptr))
	{
	  sendto_one(cptr, form_str(ERR_BANLISTFULL),
		     me.name, cptr->name,
		     chptr->chname, banid);
	  return -1;
	}
    }

  if (MyClient(cptr))
    collapse(banid);

  switch(type) 
    {
    case CHFL_BAN:
      list = &chptr->banlist;
      break;
    case CHFL_EXCEPTION:
      list = &chptr->exceptlist;
      break;
    case CHFL_DENY:
      list = &chptr->denylist;
      break;
    case CHFL_INVEX:
      list = &chptr->invexlist;
      break;
    default:
      sendto_realops_flags(FLAGS_ALL,
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

  actualBan = (struct Ban *)MyMalloc(sizeof(struct Ban));
  DupString(actualBan->banstr,banid);

  if (IsPerson(cptr))
    {
      actualBan->who =
        (char *)MyMalloc(strlen(cptr->name)+
                         strlen(cptr->username)+
                         strlen(cptr->host)+3);
      ircsprintf(actualBan->who, "%s!%s@%s",
                 cptr->name, cptr->username, cptr->host);
    }
  else
    {
      DupString(actualBan->who,cptr->name);
    }

  actualBan->when = CurrentTime;

  dlinkAdd(actualBan, ban, list);

  chptr->num_bed++;
  return 0;
}

/*
 *
 * "del_id - delete an id belonging to cptr
 * if banid is null, deleteall banids belonging to cptr."
 *
 * from orabidoo
 * modified 8/9/00 by is: now we handle add ban types here
 * (invex/excemp/deny/etc)
 */
static int del_id(struct Channel *chptr, char *banid, int type)
{
  dlink_list *list;
  dlink_node *ban;
  struct Ban *banptr;

  if (!banid)
    return -1;

  switch(type)
    {
    case CHFL_BAN:
      list = &chptr->banlist;
      break;
    case CHFL_EXCEPTION:
      list = &chptr->exceptlist;
      break;
    case CHFL_DENY:
      list = &chptr->denylist;
      break;
    case CHFL_INVEX:
      list = &chptr->invexlist;
      break;
    default:
      sendto_realops_flags(FLAGS_ALL,
			   "del_id() called with unknown ban type %d!", type);
      return -1;
    }

  for (ban = list->head; ban; ban = ban->next)
    {
      banptr = ban->data;

      if (irccmp(banid, banptr->banstr)==0)
        {
          MyFree(banptr->banstr);
          MyFree(banptr->who);
          MyFree(banptr);

	  /* num_bed should never be < 0 */
	  if(chptr->num_bed > 0)
	    chptr->num_bed--;
	  else
	    chptr->num_bed = 0;

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
 * inputs	- pointer to channel block
 * 		- pointer to client to check access fo
 * output	- returns an int 0 if not banned,
 *                CHFL_BAN if banned (or +d'd)
 *
 * IP_BAN_ALL from comstud
 * always on...
 *
 * +e code from orabidoo
 */
int is_banned(struct Channel *chptr, struct Client *who)
{
  dlink_node *ban;
  dlink_node *except;
  struct Ban *actualBan=NULL;
  struct Ban *actualExcept=NULL;
  char  s[NICKLEN+USERLEN+HOSTLEN+6];
  char  *s2;

  if (!IsPerson(who))
    return (0);

  strcpy(s, make_nick_user_host(who->name, who->username, who->host));
  s2 = make_nick_user_host(who->name, who->username, who->localClient->sockhost);

  for (ban = chptr->banlist.head; ban; ban = ban->next)
    {
      actualBan = ban->data;
      if (match(actualBan->banstr, s) ||
	  match(actualBan->banstr, s2))
	break;
      else
	actualBan = NULL;
    }

  if (actualBan == NULL)
    {
      /* check +d list */
      for (ban = chptr->denylist.head; ban; ban = ban->next)
	{
	  actualBan = ban->data;
	  if (match(actualBan->banstr, who->info))
	    break;
	  else
	    actualBan = NULL;
	}
    }

  if (actualBan != NULL)
    {
      for (except = chptr->exceptlist.head; except; except = except->next)
	{
	  actualExcept = except->data;

	  if (match(actualExcept->banstr, s) ||
	      match(actualExcept->banstr, s2))
	    {
	      return CHFL_EXCEPTION;
	    }
	}
    }

  /* return CHFL_BAN for +b or +d match,
   * really dont need to be more specific
   */

  return ((actualBan?CHFL_BAN:0));
}

/*
 * add_user_to_channel
 * 
 * inputs	- pointer to channel to add client to
 *		- pointer to client (who) to add
 *		- flags for chanops etc
 * output	- none
 * side effects - adds a user to a channel by adding another link to the
 *		  channels member chain.
 */
void add_user_to_channel(struct Channel *chptr, struct Client *who, int flags)
{
  dlink_node *ptr;

  if (who->user)
    {
      ptr = make_dlink_node();

      switch(flags)
	{
	default:
	case MODE_PEON:
	  dlinkAdd(who, ptr, &chptr->peons);
	  break;

	case MODE_CHANOP:
	  chptr->opcount++;
	  dlinkAdd(who, ptr, &chptr->chanops);
	  break;

	case MODE_HALFOP:
	  dlinkAdd(who, ptr, &chptr->halfops);
	  break;

	case MODE_VOICE:
	  dlinkAdd(who, ptr, &chptr->voiced);
	  break;
	}

      chptr->users++;

      if(MyClient(who))
	 chptr->locusers++;

      chptr->users_last = CurrentTime;

      ptr = make_dlink_node();
      dlinkAdd(chptr,ptr,&who->user->channel);
      who->user->joined++;
    }
}

/*
 * remove_user_from_channel
 * 
 * inputs	- pointer to channel to remove client from
 *		- pointer to client (who) to remove
 * output	- none
 * side effects - deletes an user from a channel by removing a link in the
 *		  channels member chain.
 */
void remove_user_from_channel(struct Channel *chptr,struct Client *who)
{
  dlink_node *ptr;

  if( (ptr = find_user_link(&chptr->peons,who)) )
    dlinkDelete(ptr,&chptr->peons);
  else if( (ptr = find_user_link(&chptr->chanops,who)) )
    {
      chptr->opcount--;
      dlinkDelete(ptr,&chptr->chanops);
    }
  else if ((ptr = find_user_link(&chptr->voiced,who)) )
    dlinkDelete(ptr,&chptr->voiced);
  else if ((ptr = find_user_link(&chptr->halfops,who)) )
    dlinkDelete(ptr,&chptr->halfops);
  else 
    return;	/* oops */

  chptr->users_last = CurrentTime;
  free_dlink_node(ptr);

  for (ptr = who->user->channel.head; ptr; ptr = ptr->next)
    {
      if (ptr->data == chptr)
      {
	dlinkDelete(ptr,&who->user->channel);
        free_dlink_node(ptr);
        break;
      }
    }

  who->user->joined--;
  
  if (IsVchan(chptr))
    del_vchan_from_client_cache(who, chptr); 

  if(MyClient(who))
    {
      if(chptr->locusers > 0)
	chptr->locusers--;
    }
  sub1_from_channel(chptr);
}

/*
 * find_user_link
 * inputs	-
 *		- client pointer to find
 * output	- pointer to link or NULL if not found
 * side effects	- Look for ptr in the linked listed pointed to by link.
 */
dlink_node *find_user_link(dlink_list *list, struct Client *who)
{
  if (who)
    return(dlinkFind(list,who));
  return (NULL);
}

/*
 * change_channel_membership
 *
 * inputs	- pointer to channel
 *		- pointer to membership list of given channel to modify
 *		- pointer to client struct being modified
 * output	- int success 1 or 0 if failure
 * side effects - change given user "who" from whichever membership list
 *		  it is on, to the given membership list in to_list.
 *		  
 */
static int change_channel_membership(struct Channel *chptr,
				     dlink_list *to_list, struct Client *who)
{
  dlink_node *ptr;

  if ( (ptr = find_user_link(&chptr->peons, who)) )
    {
      if (to_list != &chptr->peons)
	{
	  dlinkDelete(ptr, &chptr->peons);
	  dlinkAdd(who, ptr, to_list);
	  return(1);
	}
    }

  if ( (ptr = find_user_link(&chptr->voiced, who)) )
    {
      if (to_list != &chptr->voiced)
	{
	  dlinkDelete(ptr, &chptr->voiced);
	  dlinkAdd(who, ptr, to_list);
	  return(1);
	}
    }

  if ( (ptr = find_user_link(&chptr->halfops, who)) )
    {
      if (to_list != &chptr->halfops)
	{
	  dlinkDelete(ptr, &chptr->halfops);
	  dlinkAdd(who, ptr, to_list);
	  return(1);
	}
    }

  if ( (ptr = find_user_link(&chptr->chanops, who)) )
    {
      if (to_list != &chptr->chanops)
	{
	  dlinkDelete(ptr, &chptr->chanops);
	  dlinkAdd(who, ptr, to_list);
	  return(1);
	}
    }

  return 0;
}

/* small series of "helper" functions */
/*
 * can_join
 *
 * inputs	- 
 * output	- 
 * side effects - NONE
 */
int can_join(struct Client *sptr, struct Channel *chptr, char *key)
{
  dlink_node *lp;
  dlink_node *ptr;
  struct Ban *invex = NULL;
  char  s[NICKLEN+USERLEN+HOSTLEN+6];
  char  *s2;

  strcpy(s, make_nick_user_host(sptr->name, sptr->username, sptr->host));
  s2 = make_nick_user_host(sptr->name, sptr->username, sptr->localClient->sockhost);

  if ((is_banned(chptr,sptr)) == CHFL_BAN)
    return (ERR_BANNEDFROMCHAN);

  if (chptr->mode.mode & MODE_INVITEONLY)
    {
      for (lp = sptr->user->invited.head; lp; lp = lp->next)
        if (lp->data == chptr)
         break;
      if (!lp)
        {
	  for (ptr = chptr->invexlist.head; ptr; ptr = ptr->next)
	    {
	      invex = ptr->data;
	      if (match(invex->banstr, s) ||
		  match(invex->banstr, s2))
		/* yes, i hate goto, if you can find a better way
		 * please tell me -is sure -db */
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
 * inputs	- pointer to channel to check chanop on
 *		- pointer to client struct being checked
 * output	- yes if chanop no if not
 * side effects -
 */
int is_chan_op(struct Channel *chptr, struct Client *who)
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
 * inputs	- pointer to channel to check for chanop or halfops on
 *		- pointer to client struct being checked
 * output	- yes if anyop no if not
 * side effects -
 */
int is_any_op(struct Channel *chptr, struct Client *who)
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
 * inputs	- pointer to channel to check for chanop or halfops on
 *		- pointer to client struct being checked
 * output	- yes if anyop no if not
 * side effects -
 */
int is_half_op(struct Channel *chptr, struct Client *who)
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
 * inputs	- pointer to channel to check voice on
 *		- pointer to client struct being checked
 * output	- yes if voiced no if not
 * side effects -
 */
int is_voiced(struct Channel *chptr, struct Client *who)
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
 * inputs	- pointer to channel
 *		- pointer to client 
 * outputs	- CAN_SEND_OPV if op or voiced on channel
 *		- CAN_SEND_NONOP if can send to channel but is not an op
 *		  CAN_SEND_NO if they cannot send to channel
 *		  Just means they can send to channel.
 * side effects	- NONE
 */
int can_send(struct Channel *chptr, struct Client *sptr)
{
  if(is_any_op(chptr,sptr))
    return CAN_SEND_OPV;
  else if(is_voiced(chptr,sptr))
    return CAN_SEND_OPV;

  if (chptr->mode.mode & MODE_MODERATED)
    return CAN_SEND_NO;
  
  if (chptr->mode.mode & MODE_NOPRIVMSGS && !IsMember(sptr,chptr))
    return (CAN_SEND_NO);

  return CAN_SEND_NONOP;
}

/*
 * channel_modes
 * inputs	- pointer to channel
 * 		- pointer to client
 *		- pointer to mode buf
 * 		- pointer to parameter buf
 * output	- NONE
 * side effects - write the "simple" list of channel modes for channel
 * chptr onto buffer mbuf with the parameters in pbuf.
 */
void channel_modes(struct Channel *chptr, struct Client *cptr,
		   char *mbuf, char *pbuf)
{
  *mbuf++ = '+';

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
    if (IsMember(cptr, chptr) || IsServer(cptr))
      ircsprintf(pbuf, "%d ", chptr->mode.limit);
  }
  if (*chptr->mode.key)
  {
    *mbuf++ = 'k';
    if (IsMember(cptr, chptr) || IsServer(cptr))
      (void)strcat(pbuf, chptr->mode.key);
  }

  *mbuf++ = '\0';
  return;
}

/*
 * send_mode_list
 * inputs	- client pointer to server
 * 		- pointer to channel
 *		- pointer to top of mode link list to send
 * 		- char flag flagging type of mode i.e. 'b' 'e' etc.
 * 		- clear (remove all current modes, for ophiding, etc)
 * output	- NONE
 * side effects - sends +b/+e/+d/+I
 *		  
 */
static void send_mode_list(struct Client *cptr,
			   char *chname,
			   dlink_list *top,
			   char flag,
                           int clear)
{
  dlink_node *lp;
  struct Ban *banptr;
  char  mbuf[MODEBUFLEN];
  char  pbuf[MODEBUFLEN];
  int   tlen;
  int   mlen;
  int   cur_len;
  char  *mp;
  char  *pp;
  int   count;

  ircsprintf(buf, ":%s MODE %s ", me.name, chname);
  cur_len = mlen = (strlen(buf) + 2);
  count = 0;
  mp = mbuf;
  *mp++ = (clear ? '-' : '+');
  *mp   = '\0';
  pp = pbuf;

  for (lp = top->head; lp; lp = lp->next)
    {
      banptr = lp->data;
      tlen = strlen(banptr->banstr);
      tlen++;

      if ((count >= MAXMODEPARAMS) || ((cur_len + tlen + 2) > BUFSIZE))
        {
          sendto_one(cptr, "%s%s %s", buf, mbuf, pbuf);
          mp = mbuf;
          *mp++ = (clear ? '-' : '+');
          *mp = '\0';
	  pp = pbuf;
	  cur_len = mlen;
	  count = 0;
	}

      *mp++ = flag;
      *mp = '\0';
      ircsprintf(pp,"%s ",banptr->banstr);
      pp += tlen;
      cur_len += tlen;
      count++;
    }

  if(count != 0)
    sendto_one(cptr, "%s%s %s", buf, mbuf, pbuf);
}

/*
 * send_channel_modes
 * 
 * inputs	- pointer to client cptr
 * 		- pointer to channel pointer
 * output	- NONE
 * side effects	- send "cptr" a full list of the modes for channel chptr.
 */
void send_channel_modes(struct Client *cptr, struct Channel *chptr)
{
  if (*chptr->chname != '#')
    return;

  *modebuf = *parabuf = '\0';
  channel_modes(chptr, cptr, modebuf, parabuf);

  send_members(cptr,modebuf,parabuf,chptr,&chptr->chanops,"@");
  if (IsCapable(cptr, CAP_HOPS))
	  send_members(cptr,modebuf,parabuf,chptr,&chptr->halfops,"%");
  else
  /* Ok, halfops can still generate a kick, they'll just looked opped */
  send_members(cptr,modebuf,parabuf,chptr,&chptr->halfops,"@");
  send_members(cptr,modebuf,parabuf,chptr,&chptr->voiced,"+");
  send_members(cptr,modebuf,parabuf,chptr,&chptr->peons,"");

  send_mode_list(cptr, chptr->chname, &chptr->banlist, 'b', 0);

  if(!IsCapable(cptr,CAP_EX))
    return;

  send_mode_list(cptr, chptr->chname, &chptr->exceptlist, 'e', 0);

  if(!IsCapable(cptr,CAP_DE))
      return;

  send_mode_list(cptr, chptr->chname, &chptr->denylist, 'd', 0);
  
  if (!IsCapable(cptr,CAP_IE))
    return;
  
  send_mode_list(cptr, chptr->chname, &chptr->invexlist, 'I', 0);
}

/*
 * inputs	-
 * output	- NONE
 * side effects	-
 */
static void send_members(struct Client *cptr,
			 char *modebuf,
			 char *parabuf,
			 struct Channel *chptr,
			 dlink_list *list,
			 char *op_flag )
{
  dlink_node *ptr;
  int tlen;		/* length of t (temp pointer) */
  int mlen;		/* minimum length */
  int cur_len=0;	/* current length */
  struct Client *acptr;
  int  data_to_send=0;
  char *t;		/* temp char pointer */

  ircsprintf(buf, ":%s SJOIN %lu %s %s %s:", me.name,
	     chptr->channelts, chptr->chname, modebuf, parabuf);

  cur_len = mlen = strlen(buf);
  t = buf + mlen;

  for (ptr = list->head; ptr && ptr->data; ptr = ptr->next)
    {
      acptr = ptr->data;
      ircsprintf(t,"%s%s ",op_flag, acptr->name);

      tlen = strlen(t);
      cur_len += tlen;
      t += tlen; 
      data_to_send = 1;

      if (cur_len > (BUFSIZE-80))
	{
	  data_to_send = 0;
          sendto_one(cptr, "%s", buf);
	  cur_len = mlen;
	  t = buf + mlen;
	}
    }

  if( data_to_send )
    {
      sendto_one(cptr, "%s", buf);
    }
}

/*
 * pretty_mask
 * 
 * inputs	- pointer string
 * output	- pointer to cleaned up mask
 * side effects	- NONE
 *
 * stolen from Undernet's ircd  -orabidoo
 */
static char* pretty_mask(char* mask)
{
  char* cp = mask;
  char* user;
  char* host;

  if ((user = strchr(cp, '!')))
    *user++ = '\0';
  if ((host = strrchr(user ? user : cp, '@')))
    {
      *host++ = '\0';
      if (!user)
        return make_nick_user_host("*", check_string(cp), check_string(host));
    }
  else if (!user && strchr(cp, '.'))
    return make_nick_user_host("*", "*", check_string(cp));
  return make_nick_user_host(check_string(cp), check_string(user), 
                             check_string(host));
}

/*
 * fix_key
 * 
 * inputs	- pointer to key to clean up
 * output	- pointer to cleaned up key
 * side effects	- input string is modified
 *
 * stolen from Undernet's ircd  -orabidoo
 */
static  char *fix_key(char *arg)
{
  u_char        *s, *t, c;

  for (s = t = (u_char *)arg; (c = *s); s++)
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
 * inputs	- pointer to key to clean up
 * output	- pointer to cleaned up key
 * side effects	- input string is modifed 
 *
 * Here we attempt to be compatible with older non-hybrid servers.
 * We can't back down from the ':' issue however.  --Rodder
 */
static  char    *fix_key_old(char *arg)
{
  u_char        *s, *t, c;

  for (s = t = (u_char *)arg; (c = *s); s++)
    { 
      c &= 0x7f;
      if ((c != 0x0a) && (c != ':'))
        *t++ = c;
    }
  *t = '\0';
  return arg;
}

/*
 * collapse_signs
 * 
 * inputs	- pointer to signs to collapse
 * output	- pointer to collapsed string
 * side effects	- 
 * like the name says...  take out the redundant signs in a modechange list
 */
static  void    collapse_signs(char *s)
{
  char  plus = '\0', *t = s, c;
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
static  int     errsent(int err, int *errs)
{
  if (err & *errs)
    return 1;
  *errs |= err;
  return 0;
}

/* bitmasks for various error returns that set_mode should only return
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
#define SM_ERR_RPL_I						0x00000100
#define SM_ERR_RPL_D            0x00000200

/*
** Apply the mode changes passed in parv to chptr, sending any error
** messages and MODE commands out.  Rewritten to do the whole thing in
** one pass, in a desperate attempt to keep the code sane.  -orabidoo
*/
void set_channel_mode(struct Client *cptr,
                      struct Client *sptr,
                      struct Channel *chptr,
                      int parc,
                      char *parv[],
		      char *chname)
{
  int   errors_sent = 0, opcnt = 0, len = 0, tmp, nusers;
  int   keychange = 0, limitset = 0;
  int   whatt = MODE_ADD, the_mode = 0;
  int   done_s = NO, done_p = NO;
  int   done_i = NO, done_m = NO, done_n = NO, done_t = NO;
  int   done_z = NO;
  
  struct Client *who;
  char  *curr = parv[0], c, *arg, plus = '+', *tmpc;
  char  numeric[16];
  /* mbufw gets the `simple' mode chars, which nonops can see
   * on a +a channel (+smntilk)
   * pbufw gets the params.
   * mbuf2w gets the other mode chars, always with their sign
   * pbuf2w gets the other params, no ID's
   */

  char  modebuf_ex[MODEBUFLEN];
  char  parabuf_ex[MODEBUFLEN];

  char  modebuf_de[MODEBUFLEN];
  char  parabuf_de[MODEBUFLEN];

  char  modebuf_invex[MODEBUFLEN];
  char  parabuf_invex[MODEBUFLEN];

  char modebuf_hops[MODEBUFLEN];
  char parabuf_hops[MODEBUFLEN];
  
  char  *mbufw = modebuf, *mbuf2w = modebuf2;
  char  *pbufw = parabuf, *pbuf2w = parabuf2;

  char  *mbufw_ex = modebuf_ex;
  char  *pbufw_ex = parabuf_ex;

  char  *mbufw_de = modebuf_de;
  char  *pbufw_de = parabuf_de;

  char  *mbufw_invex = modebuf_invex;
  char  *pbufw_invex = parabuf_invex;

  char *mbufw_hops = modebuf_hops;
  char *pbufw_hops = parabuf_hops;

  int   ischop;
  int   isok;
  int   isok_c;
  int   isdeop;
  int   chan_op;
  int   type;

  int   target_was_op;

  dlink_node *ptr;
  dlink_list *to_list=NULL;
  struct Ban *banptr;

  chan_op = is_chan_op(chptr,sptr);

  /* has ops or is a server */
  ischop = IsServer(sptr) || chan_op;

  isdeop = 0;

  /* is an op or server or remote user on a TS channel */
  isok = ischop || (!isdeop && IsServer(cptr) && chptr->channelts);
  isok_c = isok || is_half_op(chptr,sptr);

  /* parc is the number of _remaining_ args (where <0 means 0);
  ** parv points to the first remaining argument
  */
  parc--;
  parv++;

  for ( ; ; )
    {
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
        case '+' :
          whatt = MODE_ADD;
          plus = '+';
          continue;
          /* NOT REACHED */
          break;

        case '-' :
          whatt = MODE_DEL;
          plus = '-';
          continue;
          /* NOT REACHED */
          break;

        case '=' :
          whatt = MODE_QUERY;
          plus = '=';   
          continue;
          /* NOT REACHED */
          break;

	case 'h':
        case 'o' :
        case 'v' :
          if (MyClient(sptr))
            {
              if(!IsMember(sptr, chptr))
                {
                  if(!errsent(SM_ERR_NOTONCHANNEL, &errors_sent))
                    sendto_one(sptr, form_str(ERR_NOTONCHANNEL),
                               me.name, sptr->name, chname);
                  /* eat the parameter */
                  parc--;
                  parv++;
                  break;
                }
            }
          if (whatt == MODE_QUERY)
            break;
          if (parc-- <= 0)
            break;
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!(who = find_chasing(sptr, arg, NULL)))
            break;

          if (!who->user)
            break;

          /* no more of that mode bouncing crap */
          if (!IsMember(who, chptr))
            {
              if (MyClient(sptr))
                sendto_one(sptr, form_str(ERR_USERNOTINCHANNEL), me.name, 
                           sptr->name, arg, chname);
              break;
            }

          /* ignore server-generated MODE +-ovh */
	  /* naw, allow it but still flag it */
          if (IsServer(sptr))
            {
              ts_warn( "MODE %c%c on %s for %s from server %s", 
                       (whatt == MODE_ADD ? '+' : '-'), c, chname, 
                       who->name,sptr->name);
            }

          if (c == 'o')
	    {
	      the_mode = MODE_CHANOP;
	      to_list = &chptr->chanops;
	    }
          else if (c == 'v')
	    {
	      /* ignore attempts to voice/devoice if they are opped */
	      if(is_any_op(chptr,who))
		break;
	      the_mode = MODE_VOICE;
	      to_list = &chptr->voiced;
	    }
	  else if (c == 'h')
	    {
	      the_mode = MODE_HALFOP;
	      to_list = &chptr->halfops;
	    }

	  if(whatt == MODE_DEL)
	    to_list = &chptr->peons;

          if (isdeop && (c == 'o') && whatt == MODE_ADD)
            change_channel_membership(chptr,&chptr->peons, who);
	  
          if (!isok)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chname);
              break;
            }

	  if(chptr->mode.mode & MODE_HIDEOPS)
	    {
	      if(the_mode == MODE_CHANOP || the_mode == MODE_HALFOP
			&& whatt == MODE_DEL)
		if (MyClient(who))
		  sendto_one(who,":%s!%s@%s MODE %s -%c %s",
			     sptr->name,sptr->username, sptr->host,
			     chname,c,who->name);
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
		  }
		  else 
		  {
			  *mbuf2w++ = plus;
			  *mbuf2w++ = c;
			  strcpy(pbuf2w, who->name);
			  pbuf2w += strlen(pbuf2w);
			  *pbuf2w++ = ' ';
		  }

		  len += tmp + 1;
		  opcnt++;
		  
	  /* ignore attempts to "demote" a full op to halfop */
	  if((to_list == &chptr->halfops) && is_chan_op(chptr,who))
	    break;

          target_was_op = is_any_op(chptr,who);

          if(change_channel_membership(chptr,to_list, who))
          {
	    if((to_list == &chptr->chanops) && (whatt == MODE_ADD))
            {
              chptr->opcount++;
            }
          }

          if ((!target_was_op) &&
              ((to_list == &chptr->chanops) ||
               (to_list == &chptr->halfops))
              && (whatt == MODE_ADD) && MyClient(who)
              && (chptr->mode.mode & MODE_HIDEOPS))
            {
              sync_oplists(chptr, who, 0);
            }
          else if (target_was_op &
              ((to_list != &chptr->chanops) &&
               (to_list != &chptr->halfops))
              && (whatt == MODE_DEL) && MyClient(who)
              && (chptr->mode.mode & MODE_HIDEOPS))
            {
              sync_oplists(chptr, who, 1);
            }

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
                  if MyClient(sptr)
                    arg = fix_key(check_string(*parv++));
                  else
                    arg = fix_key_old(check_string(*parv++));
                }
            }

          if (keychange++)
            break;
          /*      if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;
            */
          if (!*arg)
            break;

          if (!isok)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chname);
              break;
            }

          if ( (tmp = strlen(arg)) > KEYLEN)
            {
              arg[KEYLEN] = '\0';
              tmp = KEYLEN;
            }

          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          /* if there is already a key, and the client is adding one
           * remove the old one, then add the new one
           */

          if((whatt == MODE_ADD) && *chptr->mode.key)
            {
              /* If the key is the same, don't do anything */

              if(!strcmp(chptr->mode.key,arg))
                break;

	      if(chptr->mode.mode & MODE_HIDEOPS)
		{
		  if (IsServer(sptr)) 
		  sendto_channel_local(ONLY_CHANOPS,
				       chptr,
				       ":%s!%s@%s MODE %s -k %s", 
				       me.name,
				       sptr->username,
				       sptr->host,
				       chname,
				       chptr->mode.key);
		  else
		     sendto_channel_local(ONLY_CHANOPS, 
                                       chptr,
                                       ":%s!%s@%s MODE %s -k %s",
                                       sptr->name,   
                                       sptr->username,
                                       sptr->host,
                                       chname,
                                       chptr->mode.key);
		}
	      else
		{
		  if (IsServer(sptr)) 
		  sendto_channel_local(ALL_MEMBERS,
				       chptr,
				       ":%s!%s@%s MODE %s -k %s", 
				       me.name,
				       sptr->username,
				       sptr->host,
				       chname,
				       chptr->mode.key);
		  else
     	 	     sendto_channel_local(ALL_MEMBERS,
                                       chptr,
                                       ":%s!%s@%s MODE %s -k %s",
                                       sptr->name,
                                       sptr->username,
                                       sptr->host,
                                       chname,
                                       chptr->mode.key);
		}

              sendto_channel_remote(chptr, cptr, ":%s MODE %s -k %s",
                                 sptr->name, chname,
                                 chptr->mode.key);
            }

          if (whatt == MODE_DEL)
            {
              if( (arg[0] == '*') && (arg[1] == '\0'))
                arg = chptr->mode.key;
              else
                {
                  if(strcmp(arg,chptr->mode.key))
                    break;
		}
	    }

          *mbufw++ = plus;
          *mbufw++ = 'k';
          strcpy(pbufw, arg);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          len += tmp + 1;
          /*      opcnt++; */

          if (whatt == MODE_DEL)
            {
              *chptr->mode.key = '\0';
            }
          else
            {
              /*
               * chptr was zeroed
               */
              strncpy_irc(chptr->mode.key, arg, KEYLEN);
            }

          break;

        case 'I':
          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;
              if (errsent(SM_ERR_RPL_I, &errors_sent))
                break;
              /* don't allow a non chanop to see the invex list
               */
              if(isok)
                {
                  for (ptr = chptr->invexlist.head; ptr; ptr = ptr->next)
		    {
		      banptr = ptr->data;

		      sendto_one(cptr, form_str(RPL_INVITELIST),
				 me.name, cptr->name,
				 chname,
				 banptr->banstr,
				 banptr->who,
				 banptr->when);
		    }

                  sendto_one(sptr, form_str(RPL_ENDOFINVITELIST),
                             me.name, sptr->name, 
                             chname);
                }
		  else
		{
		   sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
			     me.name, sptr->name, chname);
		}
              break;
            }
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!isok)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                           me.name, sptr->name, 
                           chname);
              break;
            }
          
          /* user-friendly ban mask generation, taken
          ** from Undernet's ircd  -orabidoo
          */
          if (MyClient(sptr))
            arg = collapse(pretty_mask(arg));

          if(*arg == ':')
            {
              parc--;
              parv++;
              break;
            }

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          if (!(((whatt & MODE_ADD) && !add_id(sptr, chptr, arg, CHFL_INVEX))
		||
                ((whatt & MODE_DEL) && !del_id(chptr, arg, CHFL_INVEX))))
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
              if (!MyClient(sptr))
                break;
              if (errsent(SM_ERR_RPL_E, &errors_sent))
                break;
              /* don't allow a non chanop to see the exception list
               * suggested by Matt on operlist nov 25 1998
               */
              if(isok)
                {
                  for (ptr = chptr->exceptlist.head; ptr; ptr = ptr->next)
		    {
		      banptr = ptr->data;
		      sendto_one(cptr, form_str(RPL_EXCEPTLIST),
				 me.name, cptr->name,
				 chname,
				 banptr->banstr,
				 banptr->who,
				 banptr->when);
		    }

                  sendto_one(sptr, form_str(RPL_ENDOFEXCEPTLIST),
                             me.name, sptr->name, 
                             chname);
                }
              else
                {
                  sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                               sptr->name, chname);
                }
              break;
            }
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!isok)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                           me.name, sptr->name, 
                           chname);
              break;
            }
          
          /* user-friendly ban mask generation, taken
          ** from Undernet's ircd  -orabidoo
          */
          if (MyClient(sptr))
            arg = collapse(pretty_mask(arg));

          if(*arg == ':')
            {
              parc--;
              parv++;
              break;
            }

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          if (!(((whatt & MODE_ADD) && !add_id(sptr, chptr, arg, CHFL_EXCEPTION)) ||
                ((whatt & MODE_DEL) && !del_id(chptr, arg, CHFL_EXCEPTION))))
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

        case 'd':
          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;
              if (errsent(SM_ERR_RPL_D, &errors_sent))
                break;

	      if(!(chptr->mode.mode & MODE_HIDEOPS) || isok_c)
		{
                  for (ptr = chptr->denylist.head; ptr; ptr = ptr->next)
		    {
		      banptr = ptr->data;
		      sendto_one(cptr, form_str(RPL_BANLIST),
				 me.name, cptr->name,
				 chname,
				 banptr->banstr,
				 banptr->who,
				 banptr->when);
		    }
		}
	      else
		{
		  for (ptr = chptr->denylist.head; ptr; ptr = ptr->next)
		    {
		      banptr = ptr->data;
		      sendto_one(cptr, form_str(RPL_BANLIST),
				 me.name, cptr->name,
				 chname,
				 banptr->banstr,
				 "<hidden>",
				 banptr->when);
		    }
		}
          sendto_one(sptr, form_str(RPL_ENDOFBANLIST),
                     me.name, sptr->name, 
                     chname);
          break;
            }
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!isok)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                           me.name, sptr->name, 
                           chname);
              break;
            }
          
          if(*arg == ':')
            {
              parc--;
              parv++;
              break;
            }

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          if (!(((whatt & MODE_ADD) && !add_id(sptr, chptr, arg, CHFL_DENY)) ||
                ((whatt & MODE_DEL) && !del_id(chptr, arg, CHFL_DENY))))
           break;

          len += tmp + 1;
          opcnt++;

          *mbufw_de++ = plus;
          *mbufw_de++ = 'd';
          strcpy(pbufw_de, arg);
          pbufw_de += strlen(pbufw_de);
          *pbufw_de++ = ' ';

          break;

        case 'b':
          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;

              if (errsent(SM_ERR_RPL_B, &errors_sent))
                break;

	      if(!(chptr->mode.mode & MODE_HIDEOPS) || isok_c)
		{
		  for (ptr = chptr->banlist.head; ptr; ptr = ptr->next)
		    {
		      banptr = ptr->data;
		      sendto_one(cptr, form_str(RPL_BANLIST),
				 me.name, cptr->name,
				 chname,
				 banptr->banstr,
				 banptr->who,
				 banptr->when);
		    }
		}
	      else
		{
		  for (ptr = chptr->banlist.head; ptr; ptr = ptr->next)
		    {
		      banptr = ptr->data;
		      sendto_one(cptr, form_str(RPL_BANLIST),
				 me.name, cptr->name,
					 chname,
				 banptr->banstr,
				 "<hidden>",
				 banptr->when);
		    }
		}

              sendto_one(sptr, form_str(RPL_ENDOFBANLIST),
                         me.name, sptr->name, 
                         chname);
              break;
            }

          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

	  /* allow ops and halfops to set bans */
          if (!isok_c)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                           me.name, sptr->name, 
                           chname);
              break;
            }

          /* user-friendly ban mask generation, taken
          ** from Undernet's ircd  -orabidoo
          */
          if (MyClient(sptr))
            {
              if( (*arg == ':') && (whatt & MODE_ADD) )
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

         if (!(((whatt & MODE_ADD) && !add_id(sptr, chptr, arg, CHFL_BAN)) ||
               ((whatt & MODE_DEL) && !del_id(chptr, arg, CHFL_BAN))))
          break;

          *mbuf2w++ = plus;
          *mbuf2w++ = 'b';
          strcpy(pbuf2w, arg);
          pbuf2w += strlen(pbuf2w);
          *pbuf2w++ = ' ';
          len += tmp + 1;
          opcnt++;

          break;

        case 'l':
          if (whatt == MODE_QUERY)
            break;
	  /* allow ops and halfops to set limits */
          if (!isok_c)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                           me.name, sptr->name, 
                           chname);

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
                  if (MyClient(sptr))
                    sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                               me.name, sptr->name, "MODE +l");
                  break;
                }
              
              arg = check_string(*parv++);
              /*              if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
                break; */
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
              /*              opcnt++;*/
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

        case 'i' :
          if (whatt == MODE_QUERY)      /* shouldn't happen. */
            break;
          if (!isok_c)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_i)
                break;
              else
                done_i = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                break; */
            }

          if(whatt == MODE_ADD)
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

              while ( (ptr = chptr->invites.head) )
                del_invite(chptr, ptr->data);

              chptr->mode.mode &= ~MODE_INVITEONLY;
              *mbufw++ = '-';
              *mbufw++ = 'i';
              len += 2;
            }
          break;

        case 'm' :
          if (!isok_c)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_m)
                break;
              else
                done_m = YES;
            }

          if(whatt == MODE_ADD)
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

        case 'n' :
          if (!isok_c)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_n)
                break;
              else
                done_n = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
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
	      if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
		sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name,
			   sptr->name, chname);
	      break;
	      
	    }

	  if(MyClient(sptr))
	    {
	      if(done_z)
		break;
	      else
		done_z = YES;
	      
	      /*              if ( opcnt >= MAXMODEPARAMS)
			      break; */
	    }
	  
	  if(whatt == MODE_ADD)
	    {
              if (len + 2 >= MODEBUFLEN)
		break;
              chptr->mode.mode |= MODE_HIDEOPS;
              *mbufw++ = '+';
              *mbufw++ = 'a';
              len += 2;
              sync_channel_oplists(chptr, 1);
	    }
	  else
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
	      
	      chptr->mode.mode &= ~MODE_HIDEOPS;
	      *mbufw++ = '-';
	      *mbufw++ = 'a';
	      len += 2;
              sync_channel_oplists(chptr, 0);
	    }
	  break;
				
        case 'p' :
          if (!isok_c)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_p)
                break;
              else
                done_p = YES;
            }

          if(whatt == MODE_ADD)
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

        case 's' :
          if (!isok_c)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_s)
                break;
              else
                done_s = YES;
            }

          if(whatt == MODE_ADD)
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

        case 't' :
          if (!isok_c)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_t)
                break;
              else
                done_t = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
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
          if (MyClient(sptr) && !errsent(SM_ERR_UNKNOWN, &errors_sent))
            sendto_one(sptr, form_str(ERR_UNKNOWNMODE), me.name, sptr->name, c);
          break;
        }
    }

  /*
  ** WHEW!!  now all that's left to do is put the various bufs
  ** together and send it along.
  */

  *mbufw = *mbuf2w = *pbufw = *pbuf2w = *mbufw_ex = *pbufw_ex = 
  *mbufw_de = *pbufw_de = *mbufw_invex = *pbufw_invex = 
  *mbufw_hops = *pbufw_hops = '\0';

  collapse_signs(modebuf);
  collapse_signs(modebuf2);
  collapse_signs(modebuf_ex);
  collapse_signs(modebuf_de);
  collapse_signs(modebuf_invex);
  collapse_signs(modebuf_hops);
  
  if(chptr->mode.mode & MODE_HIDEOPS)
    type = ONLY_CHANOPS;
  else
    type = ALL_MEMBERS;

  /* User generated prefixes */
  if(*modebuf)
    {
      if(IsServer(sptr))
	sendto_channel_local(ALL_MEMBERS,
			     chptr,
			     ":%s MODE %s %s %s", 
			     me.name,
			     chname,
			     modebuf, parabuf);
      else
	sendto_channel_local(ALL_MEMBERS,
			     chptr,
			     ":%s!%s@%s MODE %s %s %s", 
			     sptr->name,
			     sptr->username,
			     sptr->host,
			     chname,
			     modebuf, parabuf);

      sendto_channel_remote(chptr, cptr, ":%s MODE %s %s %s",
			    sptr->name, chptr->chname,
			    modebuf, parabuf);
    }

  if(*modebuf2)
    {
      if(IsServer(sptr)) 
        sendto_channel_local(type,
                             chptr,
                             ":%s MODE %s %s %s",
                             me.name,
                             chname,
                             modebuf2, parabuf2);
        sendto_channel_local(type,
                             chptr,
                             ":%s!%s@%s MODE %s %s %s",
                             sptr->name,
                             sptr->username,
                             sptr->host,
                             chname,
                             modebuf2, parabuf2);
        sendto_channel_remote(chptr, cptr, ":%s MODE %s %s %s",
                              sptr->name, chptr->chname,
                              modebuf2, parabuf2);
    }

  if(*modebuf_ex)
    {
      if(IsServer(sptr))
	sendto_channel_local(ONLY_CHANOPS,
			     chptr,
			     ":%s MODE %s %s %s", 
			     me.name,
			     chname,
			     modebuf_ex, parabuf_ex);
      else
	sendto_channel_local(ONLY_CHANOPS,
			     chptr,
			     ":%s!%s@%s MODE %s %s %s", 
			     sptr->name,
			     sptr->username,
			     sptr->host,
			     chname,
			     modebuf_ex, parabuf_ex);

      sendto_match_cap_servs(chptr, cptr, CAP_EX, ":%s MODE %s %s %s",
                             sptr->name, chptr->chname,
                             modebuf_ex, parabuf_ex);
    }
  if(*modebuf_de)
    {
      if(IsServer(sptr))
	sendto_channel_local(type,
			     chptr,
			     ":%s MODE %s %s %s",
			     me.name,
			     chname,
			     modebuf_de, parabuf_de);
      else
	sendto_channel_local(type,
			     chptr,
			     ":%s!%s@%s MODE %s %s %s",
			     sptr->name,
			     sptr->username,
			     sptr->host,
			     chname,
			     modebuf_de, parabuf_de);

      sendto_match_cap_servs(chptr, cptr, CAP_DE, ":%s MODE %s %s %s",
                             sptr->name, chptr->chname,
                             modebuf_de, parabuf_de);
    }
  if(*modebuf_invex)
    {
      if(IsServer(sptr))
	sendto_channel_local(ONLY_CHANOPS,
			     chptr,
			     ":%s MODE %s %s %s",
			     me.name,
			     chname,
			     modebuf_invex, parabuf_invex);
      else
	sendto_channel_local(ONLY_CHANOPS,
			     chptr,
			     ":%s!%s@%s MODE %s %s %s",
			     sptr->name,
			     sptr->username,
			     sptr->host,
			     chname,
			     modebuf_invex, parabuf_invex);

      sendto_match_cap_servs(chptr, cptr, CAP_IE, ":%s MODE %s %s %s",
			     sptr->name, chptr->chname,
			     modebuf_invex, parabuf_invex);
    }	
                     
  if(*modebuf_hops)
    {
      if(IsServer(sptr))
	sendto_channel_local(type,
			     chptr,
			     ":%s MODE %s %s %s",
			     me.name,
			     chname,
			     modebuf_hops, parabuf_hops);
      else
	sendto_channel_local(type,
			     chptr,
			     ":%s!%s@%s MODE %s %s %s",
			     sptr->name,
			     sptr->username,
			     sptr->host,
			     chname,
			     modebuf_hops, parabuf_hops);

      tmpc = modebuf_hops;
      while (*tmpc && *tmpc != ' ') {
       if (*tmpc == 'h') *tmpc = 'o';
       ++tmpc;
      }
      sendto_match_cap_servs(chptr, cptr, ~CAP_HOPS, ":%s MODE %s %s %s",
			     sptr->name, chptr->chname,
			     modebuf_hops, parabuf_hops);
    }	

  return;
}


/*
 * check_channel_name
 * inputs	- channel name
 * output	- true (1) if name ok, false (0) otherwise
 * side effects	- check_channel_name - check channel name for
 *		  invalid characters
 */
int check_channel_name(const char* name)
{
  assert(0 != name);
  
  for ( ; *name; ++name) {
    if (!IsChanChar(*name))
      return 0;
  }
  return 1;
}

/*
 * get_channel
 * inputs	- client pointer
 *		- channel name
 *		- flag == CREATE if non existent
 * output	- returns channel block
 *
 *  Get Channel block for chname (and allocate a new channel
 *  block, if it didn't exist before).
 */
struct Channel* get_channel(struct Client *cptr, char *chname, int flag)
{
  struct Channel *chptr;
  int   len;

  if (BadPtr(chname))
    return NULL;

  len = strlen(chname);
  if (MyClient(cptr) && len > CHANNELLEN)
    {
      len = CHANNELLEN;
      *(chname + CHANNELLEN) = '\0';
    }
  if ((chptr = hash_find_channel(chname, NULL)))
    return (chptr);

  if (flag == CREATE)
    {
      chptr = (struct Channel*) MyMalloc(sizeof(struct Channel) + len + 1);
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
static  void    sub1_from_channel(struct Channel *chptr)
{
  struct Client *lastuser;

  if (--chptr->users <= 0)
    {
      chptr->users = 0; /* if chptr->users < 0, make sure it sticks at 0
                         * It should never happen but...
                         */
      /* persistent channel */

      /* XXX hard coded 30 minute limit here for now */
      /* channel has to exist for at least 30 minutes before 
       * being made persistent 
       */
      if((chptr->channelts + (30*60)) > CurrentTime)
        destroy_channel(chptr);
    }

  /* last user in the channel.. set a vchan_id incase we need it */
  if (chptr->users <= 1)
    {
      if (chptr->chanops.head)
        lastuser = chptr->chanops.head->data;
      else if (chptr->halfops.head)
        lastuser = chptr->halfops.head->data;
      else if (chptr->voiced.head)
        lastuser = chptr->voiced.head->data;
      else if (chptr->peons.head)
        lastuser = chptr->peons.head->data;

      ircsprintf(chptr->vchan_id, "!%s", lastuser->name);
    }
}

/*
 * clear_bans_exceptions_denies
 *
 * inputs	- pointer to client
 * 		- channel pointer
 * output	- NONE
 * side effects  -
 */
void clear_bans_exceptions_denies(struct Client *sptr, struct Channel *chptr)
{
  char *mp;
  int type;

  if(chptr->mode.mode & MODE_HIDEOPS)
    type = ONLY_CHANOPS;
  else
    type = ALL_MEMBERS;

  mp= modebuf;
  *mp++ = '-';
  *mp = '\0';

  /* clear bans/e/d/I as seen by user */
  clear_channel_list(type, chptr, sptr, &chptr->banlist, 'b');
  clear_channel_list(type, chptr, sptr, &chptr->exceptlist, 'e');
  clear_channel_list(type, chptr, sptr, &chptr->denylist, 'd');
  clear_channel_list(type, chptr, sptr, &chptr->invexlist, 'I');

  /* free all bans/exceptions/denies */
  free_channel_list(&chptr->banlist);
  free_channel_list(&chptr->exceptlist);
  free_channel_list(&chptr->denylist);
  free_channel_list(&chptr->invexlist);

  /* This should be redundant at this point but JIC */

  chptr->banlist.head = chptr->exceptlist.head =
    chptr->denylist.head = chptr->invexlist.head = NULL;

  chptr->banlist.tail = chptr->exceptlist.tail =
    chptr->denylist.tail = chptr->invexlist.tail = NULL;
    
  chptr->num_bed = 0;
}

/*
 * clear_channel_list
 * input	- 
 * output	- NONE
 * side effects	- clear out the bans/except etc. for this one list
 */
static void clear_channel_list(int type, struct Channel *chptr,
			       struct Client *sptr,
			       dlink_list *list, char flag)
{
  dlink_node *ptr;
  struct Ban *banptr;
  int   tlen;
  int   mlen;
  int   cur_len;
  char  *mp;
  char  *pp;
  int   count;
  char  *chname;
  struct Channel *root_chptr;

  if( (root_chptr = find_bchan(chptr)) != NULL )
    chname = root_chptr->chname;
  else
    chname = chptr->chname;

  mp = modebuf;
  *mp++ = '-';
  *mp   = '\0';

  pp = parabuf;

  ircsprintf(buf, ":%s MODE %s ", sptr->name, chptr->chname);
  mlen = strlen(buf);
  mlen += 3;		/* account for '+ ' */
  cur_len = mlen;
  count = 0;

  for (ptr = list->head; ptr; ptr = ptr->next)
    {
      banptr = ptr->data;
      tlen = strlen(banptr->banstr);
      tlen++;

      if ((count >= MAXMODEPARAMS) || ((cur_len + tlen) > BUFSIZE))
        {
	 if (IsServer(sptr)) 
	  sendto_channel_local(type,
			       chptr,
			       ":%s MODE %s %s %s",
			       me.name,
			       chname,
			       modebuf,parabuf);
	 else
	      sendto_channel_local(type, 
                               chptr,
                               ":%s MODE %s %s %s",
                               sptr->name,
                               chname,
                               modebuf,parabuf);
          mp = modebuf;
          *mp++ = '-';
          *mp = '\0';
	  pp = parabuf;
	  cur_len = mlen;
	  count = 0;
	}

      *mp++ = flag;
      *mp = '\0';
      ircsprintf(pp,"%s ",banptr);
      pp += tlen;
      cur_len += tlen;
      count++;
    }

  if(count != 0)
    {
      if (IsServer(sptr)) 
      sendto_channel_local(type, chptr,
			   ":%s MODE %s %s %s",
			   me.name,
			   chname,
			   modebuf,parabuf);
      else
	  sendto_channel_local(type, chptr,
                           ":%s MODE %s %s %s",
                           sptr->name,
                           chname,
                           modebuf,parabuf);
    }
}

/*
 * free_channel_list
 *
 * inputs	- pointer to dlink_list
 * output	- NONE
 * side effects	-
 */
static void free_channel_list(dlink_list *list)
{
  dlink_node *ptr;
  dlink_node *next_ptr;
  
  for (ptr = list->head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;
      ban_free(ptr);
    }
}

/*
 * ban_free
 *
 * input	- pointer to a dlink_node
 * output	- none
 * side effects	- 
 */
static void ban_free(dlink_node *ptr)
{
  struct Ban *actualBan;

  if(ptr == NULL)
     return;

  actualBan = ptr->data;

  MyFree(actualBan->banstr);
  MyFree(actualBan->who);
  MyFree(actualBan);

  free_dlink_node(ptr);
}

/*
 * cleanup_channels
 *
 * inputs	- not used
 * output	- none
 * side effects	- persistent channels... vchans get a long long timeout
 */
void cleanup_channels(void *unused)
{
   struct Channel *chptr;
   struct Channel *next_chptr;

   eventAdd("cleanup_channels", cleanup_channels, NULL,
	    CLEANUP_CHANNELS_TIME, 0 );

   for(chptr = GlobalChannelList; chptr; chptr = next_chptr)
     {
       next_chptr = chptr->nextch;

       if ( IsVchan(chptr) )
	 {
	   if ( IsVchanTop(chptr) )
	     {
	       chptr->users_last = CurrentTime;
	     }
	   else
	     {
	       if( (CurrentTime - chptr->users_last >= MAX_VCHAN_TIME) )
		 {
		   if(chptr->users == 0)
		     {
		       if (uplink
			   &&
			   IsCapable(uplink, CAP_LL))
			 {
			   sendto_one(uplink,":%s DROP %s",
				      me.name, chptr->chname);
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
	   if( (CurrentTime - chptr->users_last >= CLEANUP_CHANNELS_TIME) )
	     {
	       if(chptr->users == 0)
		 {
		   destroy_channel(chptr);
		 }
	       else if( uplink
			&&
			IsCapable(uplink,CAP_LL)
			&&
			(chptr->locusers == 0) )
		 {
		   sendto_one(uplink,":%s DROP %s",
			      me.name, chptr->chname);
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

/*
 * destroy_channel
 * inputs       - channel pointer
 * output       - none
 * side effects - walk through this channel, and destroy it.
 */

static void destroy_channel(struct Channel *chptr)
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
      m = dlinkFind(&root_chptr->vchan_list,chptr);
      dlinkDelete(m,&root_chptr->vchan_list);
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

  delete_members(&chptr->chanops);
  delete_members(&chptr->voiced);
  delete_members(&chptr->peons);
  delete_members(&chptr->halfops);

  while ((ptr = chptr->invites.head))
    del_invite(chptr, ptr->data);

  /* free all bans/exceptions/denies */
  free_channel_list(&chptr->banlist);
  free_channel_list(&chptr->exceptlist);
  free_channel_list(&chptr->denylist);
  free_channel_list(&chptr->invexlist);

  /* This should be redundant at this point but JIC */
  chptr->banlist.head = chptr->exceptlist.head =
    chptr->denylist.head = chptr->invexlist.head = NULL;

  chptr->banlist.tail = chptr->exceptlist.tail =
    chptr->denylist.tail = chptr->invexlist.tail = NULL;

  if (chptr->prevch)
    chptr->prevch->nextch = chptr->nextch;
  else
    GlobalChannelList = chptr->nextch;
  if (chptr->nextch)
    chptr->nextch->prevch = chptr->prevch;

  MyFree(chptr->topic_info);

  del_from_channel_hash_table(chptr->chname, chptr);
  MyFree((char*) chptr);
  Count.chan--;
}

/*
 * delete_members
 *
 * inputs	- pointer to list (on channel)
 * output	- none
 * side effects	- delete members of this list
 */
static void delete_members(dlink_list *list)
{
  dlink_node *ptr;
  dlink_node *next_ptr;

  for(ptr = list->head; ptr; ptr = next_ptr)
    {
      next_ptr = ptr->next;
      dlinkDelete(ptr,list);
      free_dlink_node(ptr);
    }
}

/*
 * channel_member_names
 *
 * inputs	- pointer to client struct requesting names
 *		- pointer to channel block
 *		- pointer to name of channel
 * output	- none
 * side effects	- lists all names on given channel
 */
void channel_member_names( struct Client *sptr,
			   struct Channel *chptr,
			   char *name_of_channel)
{
  int mlen;
  int cur_len;
  char buf[BUFSIZE];
  char *show_ops_flag;
  char *show_voiced_flag;
  char *show_halfops_flag;
  int reply_to_send = NO;

  /* Find users on same channel (defined by chptr) */

  ircsprintf(buf, form_str(RPL_NAMREPLY),
	     me.name, sptr->name,
	     channel_pub_or_secret(chptr));
  mlen = strlen(buf);
  ircsprintf(buf + mlen, " %s :", name_of_channel);
  mlen = strlen(buf);
  cur_len = mlen;

  if(chptr->mode.mode & MODE_HIDEOPS && !is_any_op(chptr,sptr))
    {
      show_ops_flag = "";
      show_halfops_flag = "";
      show_voiced_flag = "";
    }
  else
    {
      show_ops_flag = "@";
      show_halfops_flag = "%";
      show_voiced_flag = "+";
    }

  channel_member_list(sptr,
		      &chptr->chanops, show_ops_flag,
		      buf, mlen, &cur_len, &reply_to_send);

  channel_member_list(sptr,
		      &chptr->voiced, show_voiced_flag,
		      buf, mlen, &cur_len, &reply_to_send);

  channel_member_list(sptr,
		      &chptr->halfops, show_halfops_flag,
		      buf, mlen, &cur_len, &reply_to_send);

  channel_member_list(sptr, &chptr->peons, "",
		      buf, mlen, &cur_len, &reply_to_send);

  if(reply_to_send)
    sendto_one(sptr, "%s", buf);

  sendto_one(sptr, form_str(RPL_ENDOFNAMES),
             me.name, sptr->name, name_of_channel);
}

/*
 * channel_member_list
 *
 * inputs	- pointer to client struct requesting names
 *		- pointer to list on channel
 *		- pointer to show flag, i.e. what to show '@' etc.
 *		- buffer to use
 *		- minimum length
 *		- pointer to current length
 *		- pointer to flag denoting whether reply to send or not
 * output	- none
 * side effects	- lists all names on given list of channel
 */
void
channel_member_list(struct Client *sptr,
			 dlink_list *list,
			 char *show_flag,
			 char *buf,
			 int mlen,
			 int *cur_len,
			 int *reply_to_send)
{
  dlink_node *ptr;
  struct Client *who;
  char *t;
  int tlen;

  t = buf + *cur_len;

  for (ptr = list->head; ptr; ptr = ptr->next)
    {
      who = ptr->data;
      ircsprintf(t, "%s%s ", show_flag, who->name);
      tlen = strlen(t);
      *cur_len += tlen;
      t += tlen;
      *reply_to_send = YES;

      if ((*cur_len + NICKLEN) > (BUFSIZE - 3))
	{
	  sendto_one(sptr, "%s", buf);
	  *reply_to_send = NO;
	  *cur_len = mlen;
	  t = buf + mlen;
	}
    }
}

/*
 * channel_pub_or_secret
 *
 * inputs	- pointer to channel
 * output	- string pointer "=" if public, "@" if secret else "*"
 * side effects	- NONE
 */
char *channel_pub_or_secret(struct Channel *chptr)
{
  if(PubChannel(chptr))
    return("=");
  else if(SecretChannel(chptr))
    return("@");
  else
    return("*");
}

/*
 * add_invite
 *
 * inputs	- pointer to channel block
 * 		- pointer to client to add invite to
 * output	- none
 * side effects	- adds client to invite list
 *
 * This one is ONLY used by m_invite.c
 */
void add_invite(struct Channel *chptr, struct Client *who)
{
  dlink_node *inv;

  del_invite(chptr, who);
  /*
   * delete last link in chain if the list is max length
   */
  if (dlink_list_length(&who->user->invited) >= MAXCHANNELSPERUSER)
    {
      del_invite(chptr,who);
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
 * inputs	- pointer to dlink_list
 * 		- pointer to client to remove invites from
 * output	- none
 * side effects	- Delete Invite block from channel invite list
 *		  and client invite list
 *
 */
void del_invite(struct Channel *chptr, struct Client *who)
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
 * inputs	- pointer to channel
 * 		- pointer to client
 * output	- string either @,+% or"" depending on whether
 *		  chanop, voiced, halfop or user
 * side effects	-
 */
char *channel_chanop_or_voice(struct Channel *chptr, struct Client *acptr)
{
  if(find_user_link(&chptr->chanops,acptr))
    return("@");
  else if(find_user_link(&chptr->voiced,acptr))
    return("+");
  else if(find_user_link(&chptr->halfops,acptr))
    return("%");
  else return("");
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
static void sync_oplists(struct Channel *chptr, struct Client *acptr,
                         int clear)
{
  send_oplist(chptr->chname, acptr, &chptr->chanops, "o", clear);
  send_oplist(chptr->chname, acptr, &chptr->halfops, "h", clear);
  send_oplist(chptr->chname, acptr, &chptr->voiced,  "v", clear);
}

static void send_oplist(char *chname, struct Client *cptr,
                        dlink_list *list, char *prefix,
                        int clear)
{
  dlink_node *ptr;
  int cur_modes=0;      /* no of chars in modebuf */
  struct Client *acptr;
  int  data_to_send=0;
  char mcbuf[6] = "";
  char opbuf[MODEBUFLEN];
  char *t;
  
  *mcbuf = *opbuf = '\0';
  t = opbuf;

  for (ptr = list->head; ptr && ptr->data; ptr = ptr->next)
    {
      if ( cur_modes == 0 )
      {
        mcbuf[cur_modes++] = (clear ? '-' : '+');
      }
      
      acptr = ptr->data;
      
      mcbuf[cur_modes++] = *prefix;
     
      ircsprintf(t,"%s ", acptr->name);
      t += strlen(t);

      data_to_send = 1;

      if ( cur_modes == (MAXMODEPARAMS + 1) ) /* '+' and modes */
      {
        *t = '\0';
        mcbuf[cur_modes] = '\0';
        sendto_one(cptr, ":%s MODE %s %s %s", me.name,
                   chname, mcbuf, opbuf);

        cur_modes = 0;
        *mcbuf = *opbuf = '\0';
        t = opbuf;
        data_to_send = 0;
      }
    }

  if( data_to_send )
    {
      *t = '\0';
      mcbuf[cur_modes] = '\0';
      sendto_one(cptr, ":%s MODE %s %s %s", me.name,
                 chname, mcbuf, opbuf);
    }
}

static void sync_channel_oplists(struct Channel *chptr,
                                 int clear)
{
  dlink_node *ptr;
  dlink_list *list;
  struct Client *acptr;

  list = &chptr->peons;
  for (ptr = list->head; ptr && ptr->data; ptr = ptr->next)
    {
      acptr = ptr->data;
      if(MyClient(acptr))
        sync_oplists(chptr, acptr, clear);
    }
  list = &chptr->voiced;
  for (ptr = list->head; ptr && ptr->data; ptr = ptr->next)
    {
      acptr = ptr->data;
      if(MyClient(acptr))
        sync_oplists(chptr, acptr, clear);
    }
}
