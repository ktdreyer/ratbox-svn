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
 *   $Id$
 */
#include "tools.h"
#include "m_kline.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "dline_conf.h"
#include "irc_string.h"
#include "ircd.h"
#include "hostmask.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "send.h"
#include "hash.h"
#include "handlers.h"
#include "s_serv.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static void mo_kline(struct Client *,struct Client *,int,char **);
static void ms_kline(struct Client *,struct Client *,int,char **);
static void mo_dline(struct Client *,struct Client *,int,char **);

struct Message kline_msgtab = {
  "KLINE", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, ms_kline, mo_kline}
};

struct Message dline_msgtab = {
  "DLINE", 0, 2, 0, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_error, mo_dline}
};

void
_modinit(void)
{
  mod_add_cmd(&kline_msgtab);
  mod_add_cmd(&dline_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(&kline_msgtab);
  mod_del_cmd(&dline_msgtab);
}

/* Local function prototypes */

static time_t  valid_tkline(struct Client *source_p, char *string);
static char *cluster(char *);
static int find_user_host(struct Client *source_p,
                          char *user_host_or_nick, char *user, char *host);

/* needed to remove unused definition warning */
#ifndef IPV6
static int valid_comment(struct Client *source_p, char *comment);
#endif
static int valid_user_host(char *user, char *host);
static int valid_wild_card(char *user, char *host);
static int already_placed_kline(struct Client *source_p, char *user, char *host,
                                time_t tkline_time, struct irc_inaddr *);

static int is_ip_kline(char *host,struct irc_inaddr *ip,
                       unsigned long *ip_mask);
static void apply_kline(struct Client *source_p, struct ConfItem *aconf,
                        const char *reason, const char *current_date,
                        int ip_kline, struct irc_inaddr *ip,
                        unsigned long ip_mask);

static void apply_tkline(struct Client *source_p, struct ConfItem *aconf,
                         const char *current_date, int temporary_kline_time,
                         int ip_kline, struct irc_inaddr *ip,
                         unsigned long ip_mask);

char *_version = "20001122";

char buffer[IRCD_BUFSIZE];
char user[USERLEN+2];
char host[HOSTLEN+2];

#define MAX_EXT_REASON 100

/*
 * mo_kline
 *
 * inputs	- pointer to server
 *		- pointer to client
 *		- parameter count
 *		- parameter list
 * output	-
 * side effects - k line is added
 *
 */
static void mo_kline(struct Client *client_p,
                    struct Client *source_p,
                    int parc,
                    char **parv)
{
  char *reason = "No Reason";
  const char* current_date;
  const char* target_server=NULL;
  int  ip_kline = NO;
  struct ConfItem *aconf;
  time_t tkline_time=0;
  struct irc_inaddr ip;
  unsigned long ip_mask;

  if(!IsSetOperK(source_p))
    {
      sendto_one(source_p,":%s NOTICE %s :You have no K flag",
		 me.name,source_p->name);
      return;
    }

  parv++;
  parc--;

  tkline_time = valid_tkline(source_p,*parv);

  if( tkline_time == -1 )
    return;
  else if( tkline_time > 0 )
    {
      parv++;
      parc--;
    }

  if(parc == 0)
    {
      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
		 me.name, source_p->name, "KLINE");
      return;
    }

  if ( find_user_host(source_p,*parv,user,host) == 0 )
    return;
  parc--;
  parv++;

  if(0 != parc)
    {
      if(0 == irccmp(*parv,"ON"))
	{
	  parc--;
	  parv++;
	  if(parc == 0)
	    {
	      sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
			 me.name, source_p->name, "KLINE");
	      return;
	    }
	  target_server = *parv;
	  parc--;
	  parv++;
	}
    }

  if(0 != parc)
    reason = *parv;

  if(valid_user_host(user,host))
    {
       sendto_one(source_p, ":%s NOTICE %s :Invalid character '#' in kline",
                   me.name, source_p->name);
       return;
    }

  if(valid_wild_card(user,host))
    {
       sendto_one(source_p, 
          ":%s NOTICE %s :Please include at least %d non-wildcard characters with the user@host",
           me.name,
           source_p->name,
           ConfigFileEntry.min_nonwildcard);
       return;
    }

  ip_kline = is_ip_kline(host,&ip,&ip_mask);
  current_date = smalldate((time_t) 0);

  aconf = make_conf();
  aconf->status = CONF_KILL;
  DupString(aconf->host, host);
  DupString(aconf->user, user);

  aconf->port = 0;

  if(target_server != NULL)
    {
      sendto_cap_serv_butone(CAP_KLN, &me,
			   ":%s KLINE %s %s %lu %s %s :%s",
			   me.name, source_p->name,
			   target_server,
			   tkline_time, user, host, reason);

      /* If we are sending it somewhere that doesnt include us, we stop
       * else we apply it locally too
       */
      if(!match(target_server,me.name))
	return;
    }

  if ( already_placed_kline(source_p, user, host, tkline_time, &ip))
    return;

  if (ip_kline)
   { 
     aconf->ip = (unsigned long) IN_ADDR(ip);
     aconf->ip_mask = ip_mask;
   }

  if(tkline_time)
    {
      ircsprintf(buffer,
		 "Temporary K-line %d min. - %s (%s)",
		 (int)(tkline_time/60),
		 reason,
		 current_date);
      DupString(aconf->passwd, buffer );
      apply_tkline(source_p, aconf, current_date, tkline_time,
		   ip_kline, &ip, ip_mask);
    }
  else
    {
      ircsprintf(buffer, "%s (%s)",
		 reason,
		 current_date);
      DupString(aconf->passwd, buffer );
      apply_kline(source_p, aconf, reason, current_date, ip_kline, &ip, ip_mask);
    }
} /* mo_kline() */

/*
 * ms_kline()
 *
 *
 */
static void ms_kline(struct Client *client_p,
                    struct Client *source_p,
                    int parc,
                    char *parv[])
{
  const char *current_date;
  struct Client *rclient_p=NULL;
  struct ConfItem *aconf=NULL;
  int    tkline_time;
  int ip_kline = NO;
  struct irc_inaddr ip;
  unsigned long ip_mask;
  if(parc < 7)
    return;

  /* parv[0] parv[1] parv[2]       parv[3]     parv[4] parv[5] parv[6] */
  /* server  oper    target_server tkline_time user    host    reason */
  sendto_cap_serv_butone (CAP_KLN, client_p, ":%s KLINE %s %s %s %s %s :%s",
			  parv[0], parv[1],
			  parv[2], parv[3],
			  parv[4], parv[5],parv[6]);


  if(!match(parv[2],me.name))
    return;

  if ((rclient_p = hash_find_client(parv[1],(struct Client *)NULL)))
    {
      if(!IsPerson(rclient_p))
	return;
    }
  else
    return;

  /* These should never happen but... */
  if( rclient_p->name == NULL )
    return;
  if( rclient_p->user == NULL )
    return;
  if( rclient_p->host == NULL )
    return;

  if(valid_user_host(parv[4],parv[5]))
    {
      sendto_realops_flags(FLAGS_ALL,
             "*** Received Invalid K-Line for %s@%s, from %s!%s@%s on %s",
             parv[4], parv[5], rclient_p->name, rclient_p->username,
             rclient_p->host, source_p->name);
      return;
    }

  if(valid_wild_card(parv[4],parv[5]))
    {
       sendto_realops_flags(FLAGS_ALL, 
             "*** Received Wildcard K-Line for %s@%s, from %s!%s@%s on %s",
             parv[4], parv[5], rclient_p->name, rclient_p->username,
             rclient_p->host, source_p->name);
       return;
     }

  ip_kline = is_ip_kline(parv[5],&ip,&ip_mask);
  tkline_time = atoi(parv[3]);

  if(find_u_conf(source_p->name,rclient_p->username,rclient_p->host))
    {
      sendto_realops_flags(FLAGS_ALL,
			   "*** Received K-Line for %s@%s, from %s!%s@%s on %s",
			   parv[4],
			   parv[5],
			   rclient_p->name,
			   rclient_p->username,
			   rclient_p->host,
			   source_p->name);

      /* We check if the kline already exists after we've announced its 
       * arrived, to avoid confusing opers - fl
       */
      if ( already_placed_kline(source_p, parv[4], parv[5], (int)parv[3], &ip) )
        return;

      aconf = make_conf();

      aconf->status = CONF_KILL;
      DupString(aconf->user, parv[4]);
      DupString(aconf->host, parv[5]);
      DupString(aconf->passwd, parv[6]);
      current_date = smalldate((time_t) 0);

      if(ip_kline)
        {
          aconf->ip = (unsigned long) IN_ADDR(ip);
          aconf->ip_mask = ip_mask;
        }

      if(tkline_time)
          apply_tkline(rclient_p, aconf, current_date, tkline_time,
                       ip_kline, &ip, ip_mask);
      else
	apply_kline(rclient_p, aconf, aconf->passwd, current_date,
                       ip_kline, &ip, ip_mask);	

      }
} /* ms_kline() */

/*
 * apply_kline
 *
 * inputs	-
 * output	- NONE
 * side effects	- kline as given, is added to apropriate tree
 *		  and conf file
 */
static void apply_kline(struct Client *source_p, struct ConfItem *aconf,
                        const char *reason, const char *current_date,
                        int ip_kline, struct irc_inaddr *ip,
			unsigned long ip_mask)
{
  if(ip_kline)
    {
      aconf->ip = (unsigned long) PIN_ADDR(ip);
      aconf->ip_mask = ip_mask;
      if(add_ip_Kline(aconf) != 0)
	{
	  sendto_one(source_p,":%s NOTICE %s :Invalid IP Kline not placed",
		     me.name,
		     source_p->name);
	  free_conf(aconf);
	  return;
	}
    }
  else
    add_conf(aconf);

  WriteKlineOrDline( KLINE_TYPE,
		     source_p,
		     aconf->user,
		     aconf->host,
		     reason,
		     current_date);

  /* Now, activate kline against current online clients */
  check_klines();
}

/*
 * apply_tkline
 *
 * inputs	-
 * output	- NONE
 * side effects	- tkline as given is placed
 */
static void apply_tkline(struct Client *source_p, struct ConfItem *aconf,
                         const char *current_date, int tkline_time,
                         int ip_kline, struct irc_inaddr * ip, unsigned long ip_mask)
{
  aconf->hold = CurrentTime + tkline_time;
  add_temp_kline(aconf);
  sendto_realops_flags(FLAGS_ALL,
                       "%s added temporary %d min. K-Line for [%s@%s] [%s]",
                       source_p->name, tkline_time/60, aconf->user, aconf->host,
                       aconf->passwd);
  sendto_one(source_p, ":%s NOTICE %s :Added temporary %d min. K-Line for [%s@%s]",
             me.name, source_p->name, tkline_time/60, aconf->user, aconf->host);
  log(L_TRACE, "%s added temporary %d min. K-Line for [%s@%s] [%s]",
      source_p->name, tkline_time/60, aconf->user, aconf->host, aconf->passwd);
  check_klines();
}

/*
 * valid_tkline()
 * 
 * inputs       - pointer to client requesting kline
 *              - argument count
 *              - pointer to ascii string in
 * output       - -1 not enough parameters
 *              - 0 if not an integer number, else the number
 * side effects - none
 */
static time_t valid_tkline(struct Client *source_p, char *p)
{
  time_t result = 0;

  while(*p)
    {
      if(IsDigit(*p))
        {
          result *= 10;
          result += ((*p) & 0xF);
          p++;
        }
      else
        return(0);
    }
  /* in the degenerate case where oper does a /quote kline 0 user@host :reason 
   * i.e. they specifically use 0, I am going to return 1 instead
   * as a return value of non-zero is used to flag it as a temporary kline
   */

  if(result == 0)
    result = 1;

  if(result > (24*60))
    result = (24*60); /* Max it at 24 hours */

  result = (time_t)result * (time_t)60;  /* turn it into seconds */

  return(result);
}

/*
 * cluster()
 *
 * inputs       - pointer to a hostname
 * output       - pointer to a static of the hostname masked
 *                for use in a kline.
 * side effects - NONE
 *
 */
static char *cluster(char *hostname)
{
  static char result[HOSTLEN + 1];      /* result to return */
  char        temphost[HOSTLEN + 1];    /* work place */
  char        *ipp;             /* used to find if host is ip # only */
  char        *host_mask;       /* used to find host mask portion to '*' */
  char        *zap_point = NULL; /* used to zap last nnn portion of an ip # */
  char        *tld;             /* Top Level Domain */
  int         is_ip_number;     /* flag if its an ip # */             
  int         number_of_dots;   /* count number of dots for both ip# and
                                   domain klines */
  if (!hostname)
    return (char *) NULL;       /* EEK! */

  /* If a '@' is found in the hostname, this is bogus
   * and must have been introduced by server that doesn't
   * check for bogus domains (dns spoof) very well. *sigh* just return it...
   * I could also legitimately return (char *)NULL as above.
   */

  if(strchr(hostname,'@'))      
    {
      strncpy_irc(result, hostname, HOSTLEN);      
      result[HOSTLEN] = '\0';
      return(result);
    }

  strncpy_irc(temphost, hostname, HOSTLEN);
  temphost[HOSTLEN] = '\0';

  is_ip_number = YES;   /* assume its an IP# */
  ipp = temphost;
  number_of_dots = 0;

  while (*ipp)
    {
      if( *ipp == '.' )
        {
          number_of_dots++;

          if(number_of_dots == 3)
            zap_point = ipp;
          ipp++;
        }
      else if(!IsDigit(*ipp))
        {
          is_ip_number = NO;
          break;
        }
      ipp++;
    }

  if(is_ip_number && (number_of_dots == 3))
    {
      zap_point++;
      *zap_point++ = '*';               /* turn 111.222.333.444 into */
      *zap_point = '\0';                /*      111.222.333.*        */
      strncpy_irc(result, temphost, HOSTLEN);
      result[HOSTLEN] = '\0';
      return(result);
    }
  else
    {
      tld = strrchr(temphost, '.');
      if(tld)
        {
          number_of_dots = 2;
          if(tld[3])                     /* its at least a 3 letter tld
                                            i.e. ".com" tld[3] = 'm' not 
                                            '\0' */
                                         /* 4 letter tld's are coming */
            number_of_dots = 1;

          if(tld != temphost)           /* in these days of dns spoofers ...*/
            host_mask = tld - 1;        /* Look for host portion to '*' */
          else
            host_mask = tld;            /* degenerate case hostname is
                                           '.com' etc. */

          while (host_mask != temphost)
            {
              if(*host_mask == '.')
                number_of_dots--;
              if(number_of_dots == 0)
                {
                  result[0] = '*';
                  strncpy_irc(result + 1, host_mask, HOSTLEN - 1);
                  result[HOSTLEN] = '\0';
                  return(result);
                }
              host_mask--;
            }
          result[0] = '*';                      /* foo.com => *foo.com */
          strncpy_irc(result + 1, temphost, HOSTLEN - 1);
          result[HOSTLEN] = '\0';
        }
      else      /* no tld found oops. just return it as is */
        {
          strncpy_irc(result, temphost, HOSTLEN);
          result[HOSTLEN] = '\0';
          return(result);
        }
    }

  return (result);
}

/*
 * mo_dline
 *
 * inputs	- pointer to server
 *		- pointer to client
 *		- parameter count
 *		- parameter list
 * output	-
 * side effects - D line is added
 *
 */
static void mo_dline(struct Client *client_p, struct Client *source_p,
                    int parc, char *parv[])
{
#ifdef IPV6
  sendto_one(source_p, ":%s NOTICE %s :Sorry, DLINE is currently not implemented for IPv6",
             me.name, parv[0]);
  return;
#else
  char *dlhost, *reason;
  char *p;
  struct Client *target_p;
  char cidr_form_host[HOSTLEN + 1];
  unsigned long ip_host;
  unsigned long ip_mask;
  struct irc_inaddr ipn;
  struct ConfItem *aconf;
  char dlbuffer[1024];
  const char* current_date;

  if(!IsSetOperK(source_p))
    {
      sendto_one(source_p,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
      return;
    }

  dlhost = parv[1];
  strncpy_irc(cidr_form_host, dlhost, 32);
  cidr_form_host[32] = '\0';

  if((p = strchr(cidr_form_host,'*')))
    {
      *p++ = '0';
      *p++ = '/';
      *p++ = '2';
      *p++ = '4';
      *p++ = '\0';
      dlhost = cidr_form_host;
    }

  if(!is_address(dlhost,&ip_host,&ip_mask))
    {
      if (!(target_p = find_chasing(source_p, parv[1], NULL)))
        return;

      if(!target_p->user)
        return;

      if (IsServer(target_p))
        {
          sendto_one(source_p,
                     ":%s NOTICE %s :Can't DLINE a server silly",
                     me.name, parv[0]);
          return;
        }
              
      if(!MyConnect(target_p))
        {
          sendto_one(source_p,
                     ":%s NOTICE :%s :Can't DLINE nick on another server",
                     me.name, parv[0]);
          return;
        }

      if(IsElined(target_p))
        {
          sendto_one(source_p,
                     ":%s NOTICE %s :%s is E-lined",me.name,parv[0],
                     target_p->name);
          return;
        }

      /*
       * XXX - this is always a fixed length output, we can get away
       * with strcpy here
       *
       * strncpy_irc(cidr_form_host, inetntoa((char *)&target_p->ip), 32);
       * cidr_form_host[32] = '\0';
       */
       strcpy(cidr_form_host, inetntoa((char*) &target_p->localClient->ip));
      
      p = strchr(cidr_form_host,'.');
      if(!p)
        return;
      /* 192. <- p */

      p++;
      p = strchr(p,'.');
      if(!p)
        return;
      /* 192.168. <- p */

      p++;
      p = strchr(p,'.');
      if(!p)
        return;
      /* 192.168.0. <- p */

      p++;
      *p++ = '0';
      *p++ = '/';
      *p++ = '2';
      *p++ = '4';
      *p++ = '\0';
      dlhost = cidr_form_host;

      ip_mask = 0xFFFFFF00L;
/* XXX: Fix me for IPV6 */
      ip_host = ntohl(IN_ADDR(target_p->localClient->ip));
    }


  if (parc > 2) /* host :reason */
    {
      if ( valid_comment(source_p,parv[2]) == 0 )
	return;

      if(*parv[2])
        reason = parv[2];
      else
        reason = "No reason";
    }
  else
    reason = "No reason";


  if((ip_mask & 0xFFFFFF00) ^ 0xFFFFFF00)
    {
      if(ip_mask != 0xFFFFFFFF)
        {
          sendto_one(source_p, ":%s NOTICE %s :Can't use a mask less than 24 with dline",
                     me.name,
                     parv[0]);
          return;
        }
    }
  IN_ADDR(ipn) = ip_host;
  if( ConfigFileEntry.non_redundant_klines && (aconf = match_Dline(&ipn)) )
     {
       char *creason;
       creason = aconf->passwd ? aconf->passwd : "<No Reason>";
       if(IsConfElined(aconf))
         sendto_one(source_p, ":%s NOTICE %s :[%s] is (E)d-lined by [%s] - %s",
                    me.name,
                    parv[0],
                    dlhost,
                    aconf->host,creason);
         else
           sendto_one(source_p, ":%s NOTICE %s :[%s] already D-lined by [%s] - %s",
                      me.name,
                      parv[0],
                      dlhost,
                      aconf->host,creason);
      return;
     }

  current_date = smalldate((time_t) 0);

  ircsprintf(dlbuffer, "%s (%s)",reason,current_date);

  aconf = make_conf();
  aconf->status = CONF_DLINE;
  DupString(aconf->host,dlhost);
  DupString(aconf->passwd,dlbuffer);

  aconf->ip = ip_host;
  aconf->ip_mask = ip_mask;

  if(add_Dline(aconf) == 0)
    {
      /*
       * Write dline to configuration file
       */
      WriteKlineOrDline(DLINE_TYPE,
			source_p,
			NULL,
			dlhost,
			reason,
			current_date);

      check_klines();
    }
  else
    {
      sendto_one(source_p, ":%s NOTICE %s :Invalid Dline not placed",
		 me.name,
		 parv[0]);
      free_conf(aconf);
    }
#endif
} /* m_dline() */

/*
 * find_user_host
 * inputs	- pointer to client placing kline
 *              - pointer to user_host_or_nick
 *              - pointer to user buffer
 *              - pointer to host buffer
 * output	- 0 if not ok to kline, 1 to kline i.e. if valid user host
 * side effects -
 */
static int find_user_host(struct Client *source_p,
                          char *user_host_or_nick, char *luser, char *lhost)
{
  struct Client *target_p;
  char *hostp;

  if ( (hostp = strchr(user_host_or_nick, '@')) || *user_host_or_nick == '*' )
    {
      /* Explicit user@host mask given */

      if(hostp)                                    /* I'm a little user@host */
        {
          *(hostp++) = '\0';                       /* short and squat */
          strncpy(luser,user_host_or_nick,USERLEN); /* here is my user */
          strncpy(lhost,hostp,HOSTLEN);             /* here is my host */
        }
      else
        {
          luser[0] = '*';             /* no @ found, assume its *@somehost */
          luser[1] = '\0';	  
          strncpy(lhost,user_host_or_nick,HOSTLEN);
        }

      return 1;
    }
  else
    {
      /* Try to find user@host mask from nick */

      if (!(target_p = find_chasing(source_p, user_host_or_nick, NULL)))
        return 0;

      if(!target_p->user)
        return 0;

      if (IsServer(target_p))
        {
	  sendto_one(source_p,
	     ":%s NOTICE %s :Can't KLINE a server, use @'s where appropriate",
		     me.name, source_p->name);
          return 0;
        }

      if(IsElined(target_p))
        {
          if(!IsServer(source_p))
            sendto_one(source_p,
                       ":%s NOTICE %s :%s is E-lined",me.name,source_p->name,
                       target_p->name);
          return 0;
        }

      /* turn the "user" bit into "*user", blow away '~'
       * if found in original user name (non-idented)
       */

      strncpy_irc(luser, target_p->username, USERLEN);
      if (*target_p->username == '~')
        luser[0] = '*';

      strncpy_irc(lhost,cluster(target_p->host),HOSTLEN);
    }

  return 1;
}

/*
 * valid_user_host
 * inputs       - pointer to user buffer
 *              - pointer to host buffer
 * output	- 1 if not valid user or host, 0 if valid
 * side effects -
 */
static int valid_user_host(char *luser, char *lhost)
{
  /*
   * Check for # in user@host
   */

  if(strchr(lhost, '#') || strchr(luser, '#'))
      return 1;
  else
      return 0;
}

/*
 * valid_wild_card
 * input        - pointer to client
 *              - pointer to user to check
 *              - pointer to host to check
 * output       - 0 if not valid, 1 if valid
 * side effects -
 */
static int valid_wild_card(char *luser, char *lhost)
{
  char *p;
  char tmpch;
  int nonwild;

  /*
   * Now we must check the user and host to make sure there
   * are at least NONWILDCHARS non-wildcard characters in
   * them, otherwise assume they are attempting to kline
   * *@* or some variant of that. This code will also catch
   * people attempting to kline *@*.tld, as long as NONWILDCHARS
   * is greater than 3. In that case, there are only 3 non-wild
   * characters (tld), so if NONWILDCHARS is 4, the kline will
   * be disallowed.
   * -wnder
   */

  nonwild = 0;
  p = luser;
  while ((tmpch = *p++))
  {
    if (!IsKWildChar(tmpch))
    {
      /*
       * If we find enough non-wild characters, we can
       * break - no point in searching further.
       */
      if (++nonwild >= ConfigFileEntry.min_nonwildcard)
        break;
    }
  }

  if (nonwild < ConfigFileEntry.min_nonwildcard)
  {
    /*
     * The user portion did not contain enough non-wild
     * characters, try the host.
     */
    p = host;
    while ((tmpch = *p++))
    {
      if (!IsKWildChar(tmpch))
        if (++nonwild >= ConfigFileEntry.min_nonwildcard)
          break;
    }
  }

  if (nonwild < ConfigFileEntry.min_nonwildcard)
    return 1;
  else
    return 0;
}

/*
 * valid_comment
 * inputs	- pointer to client
 *              - pointer to comment
 * output       - 0 if no valid comment, 1 if valid
 * side effects - NONE
 */
#ifndef IPV6
static int valid_comment(struct Client *source_p, char *comment)
{
  if(strchr(comment, ':'))
    {
      if(!IsServer(source_p))
	sendto_one(source_p,
		   ":%s NOTICE %s :Invalid character ':' in comment",
		   me.name, source_p->name);
      return 0;
    }

  if(strchr(comment, '#'))
    {
      if(!IsServer(source_p))
	sendto_one(source_p,
		   ":%s NOTICE %s :Invalid character '#' in comment",
		   me.name, source_p->name);
      return 0;
    }
  return 1;
}
#endif

/*
 * already_placed_kline
 * inputs	- pointer to client placing kline
 *		- user
 *		- host
 *              - tkline_time
 *		- ip 
 * output	- 1 if already placed, 0 if not
 * side effects - NONE
 */
static int already_placed_kline(struct Client *source_p, char *luser, char *lhost,
                                time_t tkline_time, struct irc_inaddr *ip)
{
  char *reason;
  struct ConfItem *aconf;
  if(ConfigFileEntry.non_redundant_klines) 
    {
      if ((aconf = find_matching_conf(lhost,luser,ip)) && 
         (aconf->status & CONF_KILL))
        {
          reason = aconf->passwd ? aconf->passwd : "<No Reason>";

          /* Remote servers can set klines, so if its a dupe we warn all 
           * local opers and leave it at that
           */
          if(IsServer(source_p))
            sendto_realops_flags(FLAGS_ALL, 
                     "*** Remote K-Line [%s@%s] already K-Lined by [%s@%s] - %s",
                     luser, lhost, aconf->user, aconf->host, reason);
          else
             sendto_one(source_p,
                     ":%s NOTICE %s :[%s@%s] already K-Lined by [%s@%s] - %s",
                     me.name, source_p->name, luser, lhost, aconf->user,
                     aconf->host, reason);
          return 1;
        }

      if (tkline_time && (aconf = find_tkline(lhost,luser,ip)))
        {
          reason = aconf->passwd ? aconf->passwd : "<No Reason>";
          if(IsServer(source_p))
            sendto_realops_flags(FLAGS_ALL,
                    "*** Remote K-Line [%s@%s] already temp K-Lined by [%s@%s] - %s",
                    luser, lhost, aconf->user, aconf->host, reason);
          else
            sendto_one(source_p,
                    ":%s NOTICE %s :[%s@%s] already temp K-Lined by [%s@%s] - %s",
                     me.name, source_p->name, luser, lhost, aconf->user,
                     aconf->host, reason);
          return 1;
        }
    }

  return 0;
}

/*
 * is_ip_kline
 * inputs	- hostname (ip)
 * 		- pointer to where to put ip
 * 		- pointer to where to put ip_mask
 * output	- YES if valid ip_kline NO if not
 * side effects	- NONE
 */
static int is_ip_kline(char *lhost,struct irc_inaddr *ip, unsigned long *ip_mask)
{
  char *p;

  /* 
  ** At this point, I know the user and the host to place the k-line on
  ** I also know whether its supposed to be a temporary kline or not
  ** I also know the reason field is clean
  ** Now what I want to do, is find out if its a kline of the form
  **
  ** /quote kline *@192.168.0.*
  **
  */

  if((is_address(lhost, (unsigned long *)&PIN_ADDR(ip), ip_mask)))
     {
       if( (p = strchr(lhost,'*')) )
         {
           *p++ = '0';
           *p++ = '/';
           *p++ = '2';
           *p++ = '4';
           *p++ = '\0';
         }
       return(YES);
    }

  return NO;
}
