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

#define TH_MAX 0x1000

static struct HostMaskEntry *first_miscmask = NULL, *first_mask = NULL;
static struct HostMaskEntry *hmhash[TH_MAX-1];
static unsigned long precedence = 0xFFFFFFF;

static unsigned int
hash_text(const char* name)
{
  unsigned int h = 0;

  while (*name)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*name++));
    }

  return(h & (TH_MAX - 1));
}

/*
 * Note: A return value of 0 means the host cannot be hashed and must
 *       be placed on the misc list.
 */
static unsigned int
get_uhosthash(const char *uhost)
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
   return 0;
 return hash_text(lastdot ? lastdot : uhost);
}

void
add_hostmask(const char *mask, int type, void *data)
{
 struct HostMaskEntry *hme;
 unsigned int hash = get_uhosthash(mask);
 hme = MyMalloc(sizeof(*hme));
 hme->data = data;
 /* Just an ugly kludge so first entry in the conf file matches. 
  * Also so K-lines overrule I lines... */
 if (type == HOST_CONFITEM &&
     (((struct ConfItem*)data)->status & CONF_KILL))
   hme->precedence = 0xFFFFFFFF;
 else
   hme->precedence = precedence--;
 DupString(hme->hostmask, mask);
 hme->type = type;
 hme->next = first_mask;
 first_mask = hme;
 if (hash)
   {
    hme->nexthash = hmhash[hash];
    hmhash[hash] = hme;
   }
 else
   {
    hme->nexthash = first_miscmask;
    first_miscmask = hme;
   }
}

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

struct HostMaskEntry*
match_hostmask(const char *uhost, int type)
{
 struct HostMaskEntry *hme, *hmc = NULL;
 unsigned long prec = 0;
 unsigned int hash;
 const char *pos;
 for (hme = first_miscmask; hme; hme = hme->nexthash)
   if (hme->type == type && match(hme->hostmask, uhost) &&
       hme->precedence > prec)
     hmc = hme;
 for (pos = uhost; pos; pos = strcchr(pos, "@!."))
  {
   hash = hash_text(pos);
   for (hme = hmhash[hash]; hme; hme=hme->nexthash)
     if (hme->type == type && match(hme->hostmask, uhost) &&
         hme->precedence > prec)
       hmc = hme;
  }
 return hmc;
}

struct ConfItem *find_matching_conf(const char *host, const char *user,
                                    struct irc_inaddr *ip)
{
 char buffer[HOSTLEN+USERLEN+1];
 struct HostMaskEntry *hm;
 ircsprintf(buffer, "%s@%s", user, host);
 if ((hm = match_hostmask(buffer, HOST_CONFITEM)))
   return (struct ConfItem*)hm->data;
 return match_ip_Iline(ip,user);
}

void add_conf(struct ConfItem *aconf)
{
 char buffer[HOSTLEN+USERLEN+1];

 ircsprintf(buffer, "%s@%s",
	    aconf->user ? aconf->user : "",
	    aconf->host ? aconf->host : "");
 add_hostmask(buffer, HOST_CONFITEM, aconf);
}

void clear_conf(void)
{
 struct ConfItem *conf=NULL;
 struct HostMaskEntry *hme=NULL, *hmen;

 for (hme = first_mask; hme; hme = hmen)
   {
    hmen = hme->next;
    if (hme->type != HOST_CONFITEM)
      continue;
    conf = (struct ConfItem*)hme->data;
    if (conf->clients)
      conf->status |= CONF_ILLEGAL;
    else
      free_conf(conf);
    MyFree(hme);
   }
 first_mask = NULL;
 first_miscmask = NULL;
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

void
report_hostmask_conf_links(struct Client *sptr, int flags)
{
 struct HostMaskEntry *mask;
 struct ConfItem *aconf;
 char *name, *host, *pass, *user, *classname;
 int  port;
 if (flags & CONF_CLIENT) /* Show I-lines... */
   {
    for (mask = first_mask; mask; mask = mask->next)
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
    /* I-lines next... */
    report_ip_Ilines(sptr);
   }
 else /* Show K-lines... */
   {
    /* IP K-lines first... */
    report_ip_Klines(sptr);
    for (mask = first_mask; mask; mask = mask->next)
      {
       if (mask->type != HOST_CONFITEM)
         continue;
       aconf = (struct ConfItem*)mask->data;
       if (!(aconf->status & CONF_KILL))
         continue;
       get_printable_conf(aconf, &name, &host, &pass, &user, &port,
                          &classname);
       sendto_one(sptr, form_str(RPL_STATSKLINE), me.name, sptr->name,
                  'K', host, user, pass
                 );
      }
   }
}
