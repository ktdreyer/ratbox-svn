/***********************************************************************
 *   ircd-hybrid project - Internet Relay Chat
 *  hostmask.c: Find hosts for klines and auth{} etc...
 *  All parts of this program are Copyright(C) 2001(or later).
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * $Id$ 
 */
#include <unistd.h>
#include <string.h>
#include "irc_string.h"
#include "memory.h"
#include "s_conf.h"
#include "hostmask.h"
#include "sprintf_irc.h"
#include "send.h"
#include "numeric.h"
#include "dline_conf.h"
#include "ircd.h"
#include <assert.h>

#define TH_MAX 0x1000

static struct HostMaskEntry *first_miscmask = NULL;
static struct HostMaskEntry *hmhash[TH_MAX-1];
static unsigned long precedence = 0xFFFFFFF;

/*
 * hash_text
 *
 * inputs	- string to hash
 * output	- hash value
 * side effects	- none
 */
static int hash_text(const char* start)
{
  const char *p = start;
  unsigned long h = 0;

  while (*p)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*p++));
    }

  return(h & (TH_MAX - 1));
}

/*
 * get_uhosthash
 *
 * inputs	- pointer to host
 * output	- 
 * side effects	-
 *
 * Note: A return value of -1 means the host cannot be hashed and must
 *       be placed on the misc list.
 */

static int get_uhosthash(const char *uhost)
{
  const char *end, *lastdot = NULL;
  int sig = 1;
  char c = 0;
  if (!*uhost)
    return 0;

  end = uhost + strlen(uhost) - 1;
  while (sig && end >= uhost && (c = *end--))
    switch (c)
      {
      case '*':
      case '?':
	sig--;
	break;
      case '!':
      case '@':
      case '.':
	lastdot = end+2;
      }

  if ((c == '?' || c == '*') && (!lastdot || *lastdot == 0))
    return -1;

  if (end < uhost)
    {
      return hash_text(uhost);
    }

  return hash_text(lastdot ? lastdot : uhost);
}

/* 
 * add_hostmask
 *
 * inputs	- pointer to hostmask
 * 		- type KLINE etc.
 * 		- pointer to ConfItem
 * output	- NONE
 * side effects -
 */

void add_hostmask(const char *mask, int type, void *data)
{
  struct HostMaskEntry *hme;
  int hash = get_uhosthash(mask);

  hme = MyMalloc(sizeof(*hme));
  memset((void *)hme, 0, sizeof(*hme));

  hme->data = data;

  /* Just an ugly kludge so first entry in the conf file matches. 
   * Also so K-lines overrule I lines...
   */

  if (type == HOST_CONFITEM &&
      (((struct ConfItem*)data)->status & CONF_KILL))
    hme->precedence = 0xFFFFFFFF;
  else
    hme->precedence = precedence--;

  DupString(hme->hostmask, mask);
  hme->type = type;

  if (hash >= 0)
    {
      hme->next = hmhash[hash];
      hmhash[hash] = hme;
    }
  else
    {
      hme->next = first_miscmask;
      first_miscmask = hme;
    }
}

/*
 * strcchr
 *
 * inputs	-
 * output	-
 * side effects -
 */
static const char*
strcchr(const char *a, const char *b)
{
  const char *p = a, *q;
  char c, d;
  while ((c = *p++))
    {
      q = b;
      while ((d = *q++))
	if (c == d)
	  return p;
    }
  return NULL;
}

/*
 * match_hostmask
 *
 * inputs	- pointer to hostname
 * 		- type to match KLINE or CONF
 * output	- pointer to entry or NULL
 * side effects	-
 */

struct HostMaskEntry*
match_hostmask(const char *uhost, int type)
{
  struct HostMaskEntry *hme, *hmk = NULL, *hmc = NULL;
  unsigned long prec = 0;
  unsigned int hash;
  const char *pos;

  for (hme = first_miscmask; hme; hme = hme->next)
    {
      if (hme->type == type && match(hme->hostmask, uhost) &&
	  hme->precedence > prec)
	{
	  ((hme->type == HOST_CONFITEM) &&
	   ((struct ConfItem*)hme->data)->status & CONF_KILL)
	    ? hmk : hmc = hme;
	  prec = hme->precedence;
	}
    }

  for (pos = uhost; pos; pos = strcchr(pos, "@!."))
    {
      hash = hash_text(pos);
      for (hme = hmhash[hash]; hme; hme=hme->next)
	if (hme->type == type && match(hme->hostmask, uhost) &&
	    hme->precedence > prec)
	  {
	    ((hme->type == HOST_CONFITEM) &&
	     ((struct ConfItem*)hme->data)->status & CONF_KILL)
	      ? hmk : hmc = hme;
	    prec = hme->precedence;
	  }
    }

  return (hmk && (!hmc || !IsConfElined((struct ConfItem*)hmc->data))) ?
    hmk : hmc;
}

/*
 * find_matching_conf
 *
 * inputs	- pointer to hostname
 * 		- pointer to username
 * 		- pointer to IP
 * output	- matching ConfItem or NULL
 * side effects	- None
 */

struct ConfItem *find_matching_conf(const char *host, const char *user,
                                    struct irc_inaddr *ip)
{
  struct HostMaskEntry *hm;
  struct ConfItem *aconf = NULL, *aconf_k = NULL;
  char buffer[HOSTLEN+USERLEN+1];
  if (!host || !user)
    return NULL;

  ircsprintf(buffer, "%s@%s", user, host);

  if ((hm = match_hostmask(buffer, HOST_CONFITEM)))
    aconf = (struct ConfItem*)hm->data;

  if (aconf->status == CONF_KILL)
    {
      aconf_k = aconf;
      aconf = NULL;
    }

  if (aconf == NULL)
    {
      aconf = match_ip_Iline(ip, user);
    }

  if (aconf_k == NULL)
    {
      aconf_k = match_ip_Kline(ip, user);
    }
  return (aconf_k && (!aconf || !IsConfElined(aconf))) ? aconf_k : aconf;
}

/*
 * add_conf
 *
 * inputs	- pointer to ConfItem to add
 * output	- NONE
 * side effects	- NONE
 */
void add_conf(struct ConfItem *aconf)
{
  char buffer[HOSTLEN+USERLEN+1];
  
  ircsprintf(buffer, "%s@%s",
	     aconf->user ? aconf->user : "",
	     aconf->host ? aconf->host : "");
  add_hostmask(buffer, HOST_CONFITEM, aconf);
}

/* 
 * clear_conf
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	- clear out all conf items 
 */

void clear_conf(void)
{
  struct ConfItem *conf=NULL;
  struct HostMaskEntry *hme=NULL;
  struct HostMaskEntry *next_hme;
  int i;

  for (hme = first_miscmask; hme; hme=next_hme)
    {
      next_hme = hme->next;

      conf = ((struct ConfItem*)hme->data);
      free_conf(conf);
      MyFree(hme->hostmask);
      MyFree(hme);
    }

  first_miscmask = (struct HostMaskEntry *)NULL;

  for (i = 0; i < TH_MAX; i++)
    {
      for (hme = hmhash[i]; hme; hme=next_hme)
	{
	  next_hme = hme->next;
	  
	  conf = ((struct ConfItem*)hme->data);
	  free_conf(conf);
	  MyFree(hme->hostmask);
	  MyFree(hme);
	}
    }

  memset((void *)hmhash,0,sizeof(hmhash));
}

/*
 * show_iline_prefix()
 *
 * inputs       - pointer to struct Client requesting output
 *              - pointer to struct ConfItem 
 *              - name to which iline prefix will be prefixed to
 * output       - pointer to static string with prefixes listed in ascii form
 * side effects - NONE
 */
char *show_iline_prefix(struct Client *sptr,struct ConfItem *aconf,char *name)
{
  static char prefix_of_host[MAXPREFIX];
  char *prefix_ptr;

  prefix_ptr = prefix_of_host;
 
  if (IsNoTilde(aconf))
    *prefix_ptr++ = '-';
  if (IsLimitIp(aconf))
    *prefix_ptr++ = '!';
  if (IsNeedIdentd(aconf))
    *prefix_ptr++ = '+';
  if (IsPassIdentd(aconf))
    *prefix_ptr++ = '$';
  if (IsNoMatchIp(aconf))
    *prefix_ptr++ = '%';
  if (IsConfDoSpoofIp(aconf))
    *prefix_ptr++ = '=';

  if(IsOper(sptr))
    if (IsConfElined(aconf))
      *prefix_ptr++ = '^';

  if(IsOper(sptr))
    if (IsConfFlined(aconf))
      *prefix_ptr++ = '>';

  if(IsOper(sptr)) 
    if (IsConfIdlelined(aconf))
      *prefix_ptr++ = '<';

  *prefix_ptr = '\0';

  strncat(prefix_of_host,name,MAXPREFIX);
  return(prefix_of_host);
}

/*
 * report_hostmask_conf_links
 *
 * inputs	- pointer to client to report to
 * 		- flags type of conf to show
 * output	- NONE
 * side effects -
 */

void report_hostmask_conf_links(struct Client *sptr, int flags)
{
  struct HostMaskEntry *mask;
  int i;
  struct ConfItem *aconf;
  char *name, *host, *pass, *user, *classname;
  int  port;

 if (flags & CONF_CLIENT) /* Show I-lines... */
   {
    for (mask = first_miscmask; mask; mask = mask->next)
      {
       if (mask->type != HOST_CONFITEM)
         continue;
       aconf = (struct ConfItem*)mask->data;
       if (!(aconf->status & CONF_CLIENT))
         continue;

       if (!(MyConnect(sptr) && IsOper(sptr)) &&
           IsConfDoSpoofIp(aconf))
         continue;

       get_printable_conf(aconf, &name, &host, &pass, &user, &port,
                          &classname);
       sendto_one(sptr, form_str(RPL_STATSILINE), me.name, sptr->name,
                  'I', name,
                  show_iline_prefix(sptr,aconf,user),
                  host, port, classname
                 );
      }

     for (i = 0; i < TH_MAX; i++)
       {
	 for (mask = hmhash[i]; mask; mask = mask->next)
	   {
	     if (mask->type != HOST_CONFITEM)
	       continue;
	     aconf = (struct ConfItem*)mask->data;
	     if (!(aconf->status & CONF_CLIENT))
	       continue;

	     if (!(MyConnect(sptr) && IsOper(sptr)) &&
		 IsConfDoSpoofIp(aconf))
	       continue;

	     get_printable_conf(aconf, &name, &host, &pass, &user, &port,
				&classname);
	     sendto_one(sptr, form_str(RPL_STATSILINE), me.name, sptr->name,
			'I', name,
			show_iline_prefix(sptr,aconf,user),
			host, port, classname
			);
	   }
       }


    /* I-lines next... */
    report_ip_Ilines(sptr);
   }
 else /* Show K-lines... */
   {
    /* IP K-lines first... */
    report_ip_Klines(sptr);

    for (mask = first_miscmask; mask; mask = mask->next)
      {
	if (mask->type != HOST_CONFITEM)
	  continue;
	aconf = (struct ConfItem*)mask->data;
	if (!(aconf->status & CONF_KILL))
	  continue;
	get_printable_conf(aconf, &name, &host, &pass, &user, &port,
			   &classname);
	sendto_one(sptr, form_str(RPL_STATSKLINE), me.name, sptr->name,
		   'K', host, user, pass );
      }


     for (i = 0; i < TH_MAX; i++)
       {
	 for (mask = hmhash[i]; mask; mask = mask->next)
	   {
	     if (mask->type != HOST_CONFITEM)
	       continue;
	     aconf = (struct ConfItem*)mask->data;
	     if (!(aconf->status & CONF_KILL))
	       continue;

	     if (!(MyConnect(sptr) && IsOper(sptr)) &&
		 IsConfDoSpoofIp(aconf))
	       continue;

	     get_printable_conf(aconf, &name, &host, &pass, &user, &port,
				&classname);
	     sendto_one(sptr, form_str(RPL_STATSKLINE), me.name, sptr->name,
			'K', host, user, pass );
	   }
       }
   }
}
