/************************************************************************
 *   IRC - Internet Relay Chat, modules/m_kline.c
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
 *
 *   $Id$
 */
#include "tools.h"
#include "handlers.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "fileio.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "hostmask.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "send.h"
#include "msg.h"
#include "s_gline.h"
#include "parse.h"
#include "modules.h"

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

static void mo_unkline(struct Client*, struct Client*, int, char**);
static void mo_undline(struct Client*, struct Client*, int, char**);
static void mo_ungline(struct Client*, struct Client*, int, char**);

struct Message msgtabs[] = {
  {"UNKLINE", 0, 2, 0, MFLG_SLOW, 0,
   {m_unregistered, m_not_oper, m_error, mo_unkline}},
  {"UNDLINE", 0, 2, 0, MFLG_SLOW, 0,
   {m_unregistered, m_not_oper, m_error, mo_undline}}, 
  {"UNGLINE", 0, 2, 0, MFLG_SLOW, 0,
   {m_unregistered, m_not_oper, m_error, mo_ungline}}
};

#ifndef STATIC_MODULES
void
_modinit(void)
{
  mod_add_cmd(&msgtabs[0]);
  mod_add_cmd(&msgtabs[1]);
  mod_add_cmd(&msgtabs[2]);
}

void
_moddeinit(void)
{
  mod_del_cmd(&msgtabs[0]);
  mod_del_cmd(&msgtabs[1]);
  mod_del_cmd(&msgtabs[2]);
}
char *_version = "20001122";
#endif

static int flush_write(struct Client *, FBFILE* , char *, char *);
static int remove_tkline_match(char *,char *);


/*
** mo_unkline
** Added Aug 31, 1997 
** common (Keith Fralick) fralick@gate.net
**
**      parv[0] = sender
**      parv[1] = address to remove
*
*
*/
static void mo_unkline (struct Client *client_p,struct Client *source_p,
                       int parc,char *parv[])
{
 FBFILE *in, *out;
 int pairme = NO, error_on_write = NO, type, bits, kbits;
 struct irc_inaddr hostip, khostip;
 char buf[BUFSIZE], buff[BUFSIZE], temppath[256], *user, *host, *p;
 const char  *filename;                /* filename to use for unkline */
 mode_t oldumask;
 ircsprintf(temppath, "%s.tmp", ConfigFileEntry.klinefile);
  
 if (!IsSetOperUnkline(source_p))
 {
  sendto_one(source_p,":%s NOTICE %s :You have no U flag",me.name,parv[0]);
  return;
 }
 p = strchr(parv[1], '@');
 if (p)
 {
  user = parv[1];
  *p++ = 0;
  host = p;
 } else if (strchr(parv[1], '.')) {
  user = "*";
  host = parv[1];
 } else {
  /* Now I guess we give up... */
  sendto_one(source_p, ":%s NOTICE %s :Invalid mask to unkline.", me.name,
             parv[0]);
  return;
 }
 if (remove_tkline_match(host, user))
 {
  sendto_one(source_p,
             ":%s NOTICE %s :Un-klined [%s@%s] from temporary k-lines",
             me.name, parv[0],user, host);
  sendto_realops_flags(FLAGS_ALL,
                       "%s has removed the temporary K-Line for: [%s@%s]",
                       parv[0], user, host);
  log(L_NOTICE, "%s removed temporary K-Line for [%s@%s]", parv[0], user,
      host);
  return;
 }
 type = parse_netmask(host, &hostip, &bits);
 filename = get_conf_name(KLINE_TYPE);
 if ((in = fbopen(filename, "r")) == 0)
 {
  sendto_one(source_p, ":%s NOTICE %s :Cannot open %s", me.name, parv[0],
             filename);
  return;
 }
 oldumask = umask(0);
 if ((out = fbopen(temppath, "w")) == 0)
 {
  sendto_one(source_p, ":%s NOTICE %s :Cannot open %s", me.name, parv[0],
             temppath);
  fbclose(in);
  umask(oldumask);
  return;
 }
 umask(oldumask);
/*
#Dianora!db@ts2-11.ottawa.net K'd: foo@bar:No reason
K:bar:No reason (1997/08/30 14.56):foo
*/
 while (fbgets(buf, sizeof(buf), in)) 
 {
  if ((buf[1] == ':') && ((buf[0] == 'k') || (buf[0] == 'K')))
  {
   /* its a K: line */
   char *found_host, *found_user, *found_comment;
   strncpy_irc(buff, buf, BUFSIZE-1)[BUFSIZE-1] = 0;
   p = strchr(buff,'\n');
   if (p)
    *p = '\0';
   /* point past the K: */
   found_host = buff + 2;
   p = strchr(found_host,':');
   if (p == (char *)NULL)
   {
    sendto_one(source_p, ":%s NOTICE %s :K-Line file corrupted", me.name,
               parv[0]);
    sendto_one(source_p, ":%s NOTICE %s :Couldn't find host", me.name,
               parv[0]);
    log(L_ERROR, "K-Line file corrupted (couldn't find host)");
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
    /* This K line is corrupted ignore */      
    continue;
   }
   *p = '\0';
   p++;
   found_comment = p;
   p = strchr(found_comment,':');
   if (p == (char *)NULL)
   {
    sendto_one(source_p, ":%s NOTICE %s :K-Line file corrupted", me.name,
               parv[0]);
    sendto_one(source_p, ":%s NOTICE %s :Couldn't find comment", me.name,
               parv[0]);
    log(L_ERROR, "K-Line file corrupted (couldn't find comment)");
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
    /* This K line is corrupted ignore */
    continue;
   }
   *p = '\0';
   p++;
   found_user = p;
   if (irccmp(user,found_user) || type!=parse_netmask(found_host, &khostip,
       &kbits) || bits != kbits || !(
        (type==HM_HOST && !irccmp(host, found_host))
        || (type==HM_IPV4 && match_ipv4(&hostip, &khostip, bits))
#ifdef IPV6
        || (type==HM_IPV6 && match_ipv6(&hostip, &khostip, bits))
#endif
       )
      )
   {
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
   } else
    pairme++;
  } else if(buf[0] == '#') {
   char *userathost;
   char *found_user;
   char *found_host;
   strncpy_irc(buff, buf, BUFSIZE);
/*
#Dianora!db@ts2-11.ottawa.net K'd: foo@bar:No reason
K:bar:No reason (1997/08/30 14.56):foo
If its a comment coment line, i.e.
#ignore this line
Then just ignore the line
*/
   p = strchr(buff,':');
   if (p == (char *)NULL)
   {
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
    continue;
   }
   *p = '\0';
   p++;
   userathost = p;
   p = strchr(userathost,':');
   if (p == (char *)NULL)
   {
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
    continue;
   }
   *p = '\0';
   while (*userathost == ' ')
    userathost++;
   found_user = userathost;
   p = strchr(found_user,'@');
   if (p == (char *)NULL)
   {
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
    continue;
   }
   *p = '\0';
   found_host = p;
   found_host++;
   if (irccmp(user,found_user) || type!=parse_netmask(found_host, &khostip,
       &kbits) || bits != kbits || !(
        (type==HM_HOST && !irccmp(host, found_host))
        || (type==HM_IPV4 && match_ipv4(&hostip, &khostip, bits))
#ifdef IPV6
        || (type==HM_IPV6 && match_ipv6(&hostip, &khostip, bits))
#endif
       )
      )
   {
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
   }
  /* its the ircd.conf file, and not a K line or comment */   
  } else {
   if (!error_on_write)
    error_on_write = flush_write(source_p, out, buf, temppath);
  }
 }
 fbclose(in);
 /* The result of the rename should be checked too... oh well
  * If there was an error on a write above, then its been reported
  * and I am not going to trash the original kline /conf file
  * -Dianora
  */
 if (!error_on_write)
 {
  fbclose(out);
  (void)rename(temppath, filename);
  rehash(client_p,source_p,0);
 } else {
  sendto_one(source_p,
             ":%s NOTICE %s :Couldn't write temp kline file, aborted",
             me.name,parv[0]);
  return;
 }
 if (!pairme)
 {
  sendto_one(source_p, ":%s NOTICE %s :No K-Line for %s@%s", me.name,
             parv[0], user, host);
  return;
 }
 sendto_one(source_p, ":%s NOTICE %s :K-Line for [%s@%s] is removed",
            me.name, parv[0], user,host);
 sendto_realops_flags(FLAGS_ALL, "%s has removed the K-Line for: [%s@%s]",
		              parv[0], user, host);
 log(L_NOTICE, "%s removed K-Line for [%s@%s]", parv[0], user, host);
 return;
}

/*
 * flush_write()
 *
 * inputs       - pointer to client structure of oper requesting unkline
 *              - out is the file descriptor
 *              - buf is the buffer to write
 *              - ntowrite is the expected number of character to be written
 *              - temppath is the temporary file name to be written
 * output       - YES for error on write
 *              - NO for success
 * side effects - if successful, the buf is written to output file
 *                if a write failure happesn, and the file pointed to
 *                by temppath, if its non NULL, is removed.
 *
 * The idea here is, to be as robust as possible when writing to the 
 * kline file.
 *
 * -Dianora
 */

static int flush_write(struct Client *source_p, FBFILE* out, char *buf, char *temppath)
{
  int error_on_write = (fbputs(buf, out) < 0) ? YES : NO;

  if (error_on_write)
    {
      sendto_one(source_p,":%s NOTICE %s :Unable to write to %s",
        me.name, source_p->name, temppath );
      fbclose(out);
      if(temppath != (char *)NULL)
        (void)unlink(temppath);
    }
  return(error_on_write);
}

/* static int remove_tkline_match(char *host, char *user)
 * Input: A hostname, a username to unkline.
 * Output: returns YES on success, NO if no tkline removed.
 * Side effects: Any matching tklines are removed.
 */
static int
remove_tkline_match(char *host, char *user)
{
 struct ConfItem *tk_c;
 dlink_node *tk_n;
 struct irc_inaddr addr, caddr;
 int nm_t, cnm_t, bits, cbits;
 nm_t = parse_netmask(host, &addr, &bits);
 for (tk_n=temporary_klines.head; tk_n; tk_n=tk_n->next)
 {
  tk_c = (struct ConfItem*)tk_n->data;
  cnm_t = parse_netmask(tk_c->host, &caddr, &cbits);
  if (cnm_t != nm_t || irccmp(user, tk_c->user))
   continue;
  if ((nm_t==HM_HOST && !irccmp(tk_c->host, host)) ||
      (nm_t==HM_IPV4 && bits==cbits && match_ipv4(&addr, &caddr, bits))
#ifdef IPV6
      || (nm_t==HM_IPV6 && bits==cbits && match_ipv6(&addr, &caddr, bits))
#endif
     )
  {
   dlinkDelete(tk_n, &temporary_klines);
   free_dlink_node(tk_n);
   delete_one_address_conf(tk_c->host, tk_c);
   return YES;
  }
 }
 return NO;
}

/*
** m_undline
** added May 28th 2000 by Toby Verrall <toot@melnet.co.uk>
** based totally on m_unkline
** added to hybrid-7 7/11/2000 --is
**
**      parv[0] = sender nick
**      parv[1] = dline to remove
*/
static void
mo_undline (struct Client *client_p, struct Client *source_p,
            int parc,char *parv[])
{
 FBFILE* in;
 FBFILE* out;
 char  buf[BUFSIZE], buff[BUFSIZE], temppath[256], *cidr, *p;
 const char  *filename;
 struct irc_inaddr ip_host, cip_host;
 int ip_mask, cip_mask, type, pairme = NO, error_on_write = NO;
 mode_t oldumask;

 ircsprintf(temppath, "%s.tmp", ConfigFileEntry.dlinefile);

 if (!IsSetOperUnkline(source_p))
 {
  sendto_one(source_p,":%s NOTICE %s :You have no U flag",me.name,
             parv[0]);
  return;
 }
 cidr = parv[1];
 if ((type=parse_netmask(cidr,&ip_host,&ip_mask)) == HM_HOST)
 {
  sendto_one(source_p, ":%s NOTICE %s :Invalid parameters",
             me.name, parv[0]);
  return;
 }
 filename = get_conf_name(DLINE_TYPE);
 if ((in = fbopen(filename, "r")) == 0)
 {
  sendto_one(source_p, ":%s NOTICE %s :Cannot open %s",
             me.name,parv[0],filename);
  return;
 }
 oldumask = umask(0);                  /* ircd is normally too paranoid */
 if ( (out = fbopen(temppath, "w")) == 0)
 {
  sendto_one(source_p, ":%s NOTICE %s :Cannot open %s",
  me.name,parv[0],temppath);
  fbclose(in);
  umask(oldumask);                  /* Restore the old umask */
  return;
 }
 umask(oldumask);                    /* Restore the old umask */
/*
#toot!~toot@127.0.0.1 D'd: 123.4.5.0/24:test (2000/05/28 12.48)
D:123.4.5.0/24:test (2000/05/28 12.48)
*/
 while(fbgets(buf, sizeof(buf), in))
 {
  if ((buf[1] == ':') && ((buf[0] == 'd') || (buf[0] == 'D')))
  {
   /* its a D: line */
   char *found_cidr;
   strncpy_irc(buff, buf, BUFSIZE-1)[BUFSIZE-1] = 0;
   p = strchr(buff,'\n');
   if (p)
    *p = '\0';
   found_cidr = buff + 2;        /* point past the D: */
   p = strchr(found_cidr,':');
   if (p == (char *)NULL)
   {
    sendto_one(source_p, ":%s NOTICE %s :D-Line file corrupted", me.name,
               parv[0]);
    sendto_one(source_p, ":%s NOTICE %s :Couldn't find hostmask.",
               me.name, parv[0]);
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
    continue;         /* This D line is corrupted ignore */
   }
  } else if(buf[0] == '#') {
   char *found_cidr;
   strncpy_irc(buff, buf, BUFSIZE);
/*
#toot!~toot@127.0.0.1 D'd: 123.4.5.0/24:test (2000/05/28 12.48)
D:123.4.5.0/24:test (2000/05/28 12.48)

If its a comment coment line, i.e.
#ignore this line
Then just ignore the line
*/
   p = strchr(buff,':');
   if (p == (char *)NULL)
   {
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
    continue;
   }
   *p = '\0';
   p++;
   found_cidr = p;
   p = strchr(found_cidr,':');
   if (p == (char *)NULL)
   {
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
    continue;
   }
   *p = '\0';
   while (*found_cidr == ' ')
    found_cidr++;
   if (!(parse_netmask(found_cidr, &cip_host, &cip_mask) == type &&
       ip_mask == cip_mask &&
       ((type==HM_IPV4 && match_ipv4(&ip_host, &cip_host, ip_mask))
#ifdef IPV6
        || (type==HM_IPV6 && match_ipv6(&ip_host, &cip_host, ip_mask))
#endif
      )))
   {
    if (!error_on_write)
     error_on_write = flush_write(source_p, out, buf, temppath);
   } else
    pairme = YES;
  } else {
   /* Not a D-line or comment, probably another oldconf line(e.g. K:) */      
   if (!error_on_write)
    error_on_write = flush_write(source_p, out, buf, temppath);
  }
 }
 fbclose(in);
 if (!error_on_write)
 {
  fbclose(out);
  (void)rename(temppath, filename);
  rehash(client_p,source_p,0);
 } else {
  sendto_one(source_p,
             ":%s NOTICE %s :Couldn't write D-line file, aborted",
             me.name, parv[0]);
  return;
 }
 if (!pairme)
 {
  sendto_one(source_p, ":%s NOTICE %s :No D-Line for %s", me.name,
             parv[0],cidr);
  return;
 }
 sendto_one(source_p, ":%s NOTICE %s :D-Line for [%s] is removed",
            me.name, parv[0], cidr);
 sendto_realops_flags(FLAGS_ALL, "%s has removed the D-Line for: [%s]",
		              parv[0], cidr);
 log(L_NOTICE, "%s removed D-Line for [%s]", parv[0], cidr);
}

/*
** m_ungline
** added May 29th 2000 by Toby Verrall <toot@melnet.co.uk>
** added to hybrid-7 7/11/2000 --is
**
**      parv[0] = sender nick
**      parv[1] = gline to remove
*/

static void mo_ungline(struct Client *client_p, struct Client *source_p,
                      int parc,char *parv[])
{
  char  *user,*host;

  if (!ConfigFileEntry.glines)
    {
      sendto_one(source_p,":%s NOTICE %s :UNGLINE disabled",me.name,parv[0]);
      return;
    }

  if (!IsSetOperUnkline(source_p) || !IsSetOperGline(source_p))
    {
      sendto_one(source_p,":%s NOTICE %s :You have no U and G flags",
                 me.name,parv[0]);
      return;
    }

  if ( (host = strchr(parv[1], '@')) || *parv[1] == '*' )
    {
      /* Explicit user@host mask given */

      if(host)                  /* Found user@host */
        {
          user = parv[1];       /* here is user part */
          *(host++) = '\0';     /* and now here is host */
        }
      else
        {
          user = "*";           /* no @ found, assume its *@somehost */
          host = parv[1];
        }
    }
  else
    {
      sendto_one(source_p, ":%s NOTICE %s :Invalid parameters",
                 me.name, parv[0]);
      return;
    }

  if(remove_gline_match(user, host))
    {
      sendto_one(source_p, ":%s NOTICE %s :Un-glined [%s@%s]",
                 me.name, parv[0],user, host);
      sendto_realops_flags(FLAGS_ALL,
			   "%s has removed the G-Line for: [%s@%s]",
			   parv[0], user, host );
      log(L_NOTICE, "%s removed G-Line for [%s@%s]",
          parv[0], user, host);
      return;
    }
  else
    {
      sendto_one(source_p, ":%s NOTICE %s :No G-Line for %s@%s",
                 me.name, parv[0],user,host);
      return;
    }
}
