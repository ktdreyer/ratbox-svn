/************************************************************************
 *   IRC - Internet Relay Chat, src/m_kline.c
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
#include "mtrie_conf.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "send.h"
#include "hash.h"
#include "handlers.h"
#include "msg.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

struct Message kline_msgtab = {
  MSG_KLINE, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, ms_kline, mo_kline}
};

struct Message dline_msgtab = {
  MSG_DLINE, 0, 1, MFLG_SLOW, 0,
  {m_unregistered, m_not_oper, m_error, mo_dline}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_KLINE, &kline_msgtab);
  mod_add_cmd(MSG_DLINE, &dline_msgtab);
}

void
_moddeinit(void)
{
  mod_del_cmd(MSG_KLINE);
  mod_del_cmd(MSG_DLINE);
}

extern ConfigFileEntryType ConfigFileEntry; /* defined in ircd.c */

/* Local function prototypes */

time_t  valid_tkline(struct Client *sptr, int argc, char *string);
char *cluster(char *);
int find_user_host(struct Client *sptr,
                   char *user_host_or_nick, char *user, char *host);

int valid_comment(struct Client *sptr, char *comment);
int valid_user_host(struct Client *sptr, char *user, char *host);
int valid_wild_card(struct Client *sptr, char *user, char *host);
int already_placed_kline( struct Client *sptr, char *user, char *host,
			  unsigned long ip);

int is_ip_kline(char *host,unsigned long *ip, unsigned long *ip_mask);
void apply_kline(struct Client *sptr, struct ConfItem *aconf,
		 char *current_date,
		 int ip_kline, unsigned long ip, unsigned long ip_mask);

void WriteKline(const char *, struct Client *, struct Client *,
                       const char *, const char *, const char *, 
                       const char *);

void WriteDline(const char *filename, struct Client *sptr,
                const char *host, const char *reason, const char *when);


char *_version = "20001122";

char buffer[IRCD_BUFSIZE];
char user[USERLEN+2];
char host[HOSTLEN+2];

/*
 * mo_kline
 *
 * inputs	- pointer to server
 *		- pointer to client
 *		- parameter count
 *		- parameter list
 * output	-
 * side effects - D line is added
 *
 */
int mo_kline(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  char *p;
  char *reason = NULL;
  const char* current_date;
  int  ip_kline = NO;
  struct ConfItem *aconf;
  time_t temporary_kline_time=0;
  char *argv;
  unsigned long ip;
  unsigned long ip_mask;


  if(!IsSetOperK(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
      return 0;
    }

  if ( parc < 2 )
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "KLINE");
      return 0;
    }

  argv = parv[1];

  temporary_kline_time = valid_tkline(sptr,parc,argv);

  if( temporary_kline_time == -1 )
    return 0;
  else if( temporary_kline_time > 0 )
    {
      argv = parv[2];
      parc--;
    }

  if ( find_user_host(sptr,argv,user,host) == 0 )
    return 0;

  if(temporary_kline_time)
    argv = parv[3];
  else
    argv = parv[2];

  if ((parc > 2) && argv) 
    if ( valid_comment(sptr, argv) == 0 )
      return 0;

  if(argv && *argv)
    reason = argv;
  else
    reason = "No reason";

  if( valid_user_host(sptr,user,host) == 0 )
    return 0;

  if( valid_wild_card(sptr,user,host) == 0 )
    return 0;

  ip_kline = is_ip_kline(host,&ip,&ip_mask);

  if ( already_placed_kline(sptr, user, host, ip) )
    return 0;

  current_date = smalldate((time_t) 0);

  aconf = make_conf();
  aconf->status = CONF_KILL;
  DupString(aconf->host, host);
  DupString(aconf->user, user);

  aconf->port = 0;

  if(temporary_kline_time)
    {
      ircsprintf(buffer,
		 "Temporary K-line %d min. - %s (%s)",
		 temporary_kline_time,
		 reason,
		 current_date);
      DupString(aconf->passwd, buffer );
      aconf->hold = CurrentTime + temporary_kline_time;
      if (ip_kline)
	{
	  aconf->ip = ip;
	  aconf->ip_mask = ip_mask;
	}
      add_temp_kline(aconf);
      sendto_realops("%s added temporary %d min. K-Line for [%s@%s] [%s]",
        parv[0],
        temporary_kline_time/60,
        user,
        host,
        reason );
      check_klines();
      return 0;
    }
  else
    {
      ircsprintf(buffer, "%s (%s)",
		 reason,
		 current_date);
      DupString(aconf->passwd, buffer );
    }
  ClassPtr(aconf) = find_class(0);


#if 0
  sendto_cap_servs(chptr, cptr, CAP_KLN,
		   ":%s KLINE %s %s %s %s",
		   me.name, sptr->name, user, host, reason);
#endif

  apply_kline(sptr, aconf, (char *)current_date, ip_kline, ip, ip_mask);

  return 0;
} /* mo_kline() */

/*
 * ms_kline()
 *
 *
 */
int ms_kline(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  char *slave_oper;
  struct Client *rcptr=NULL;

  if(parc < 2)
    return 0;

  slave_oper = parv[1];

  if ((rcptr = hash_find_client(slave_oper,(struct Client *)NULL)))
    {
      if(!IsPerson(rcptr))
	return 0;
    }
  else
    return 0;

  if(!find_special_conf(sptr->name,CONF_ULINE))
    {
      sendto_realops("received Unauthorized kline from %s",sptr->name);
    }
  else
    {
      sendto_realops("received kline from %s", sptr->name);
      mo_kline(cptr,sptr,parc,parv);
    }

  return 0;
} /* ms_kline() */

/*
 * apply_kline
 *
 * inputs	-
 * output	- NONE
 * side effects	- kline as given, is added to apropriate tree
 *		  and conf file
 */
void apply_kline(struct Client *sptr, struct ConfItem *aconf,
		 char *current_date,
		 int ip_kline, unsigned long ip, unsigned long ip_mask)
{
  const char *kconf; /* kline conf file */

  if(ip_kline)
    {
      aconf->ip = ip;
      aconf->ip_mask = ip_mask;
      add_ip_Kline(aconf);
    }
  else
    add_mtrie_conf_entry(aconf,CONF_KILL);

  sendto_realops("%s added K-Line for [%s@%s] [%s]",
    sptr->name,
    aconf->user,
    aconf->host,
    aconf->passwd);

  log(L_TRACE, "%s added K-Line for [%s@%s] [%s]",
      sptr->name, aconf->user, aconf->host, aconf->passwd);

  kconf = get_conf_name(KLINE_TYPE);

  sendto_one(sptr,
    ":%s NOTICE %s :Added K-Line [%s@%s] to %s",
    me.name,
    sptr->name,
    aconf->user,
    aconf->host,
    kconf ? kconf : "configuration file");

  /*
   * Write kline to configuration file
   */
  WriteKline(kconf,
	     sptr,
	     (struct Client *) NULL,
	     aconf->user,
	     aconf->host,
	     aconf->passwd,
	     current_date);

  /* Now, activate kline against current online clients */
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
time_t valid_tkline(struct Client *sptr, int parc, char *p)
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
     i.e. they specifically use 0, I am going to return 1 instead
     as a return value of non-zero is used to flag it as a temporary kline
  */

  if(result == 0)
    result = 1;

  if(parc < 3)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		 me.name, sptr->name, "KLINE");
      return -1;
    }

  if(result > (24*60))
    result = (24*60); /* Max it at 24 hours */

  result = (time_t)result * (time_t)60;  /* turn it into minutes */

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
char *cluster(char *hostname)
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
int mo_dline(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char *host, *reason;
  char *p;
  struct Client *acptr;
  char cidr_form_host[HOSTLEN + 1];
  unsigned long ip_host;
  unsigned long ip_mask;
  struct ConfItem *aconf;
  char buffer[1024];
  const char* current_date;
  const char *dconf;

  if(!IsSetOperK(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
      return 0;
    }

  if ( parc < 2 )
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "DLINE");
      return 0;
    }

  host = parv[1];
  strncpy_irc(cidr_form_host, host, 32);
  cidr_form_host[32] = '\0';

  if((p = strchr(cidr_form_host,'*')))
    {
      *p++ = '0';
      *p++ = '/';
      *p++ = '2';
      *p++ = '4';
      *p++ = '\0';
      host = cidr_form_host;
    }

  if(!is_address(host,&ip_host,&ip_mask))
    {
      if (!(acptr = find_chasing(sptr, parv[1], NULL)))
        return 0;

      if(!acptr->user)
        return 0;

      if (IsServer(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :Can't DLINE a server silly",
                     me.name, parv[0]);
          return 0;
        }
              
      if(!MyConnect(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE :%s :Can't DLINE nick on another server",
                     me.name, parv[0]);
          return 0;
        }

      if(IsElined(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :%s is E-lined",me.name,parv[0],
                     acptr->name);
          return 0;
        }

      /*
       * XXX - this is always a fixed length output, we can get away
       * with strcpy here
       *
       * strncpy_irc(cidr_form_host, inetntoa((char *)&acptr->ip), 32);
       * cidr_form_host[32] = '\0';
       */
       strcpy(cidr_form_host, inetntoa((char*) &acptr->localClient->ip));
      
      p = strchr(cidr_form_host,'.');
      if(!p)
        return 0;
      /* 192. <- p */

      p++;
      p = strchr(p,'.');
      if(!p)
        return 0;
      /* 192.168. <- p */

      p++;
      p = strchr(p,'.');
      if(!p)
        return 0;
      /* 192.168.0. <- p */

      p++;
      *p++ = '0';
      *p++ = '/';
      *p++ = '2';
      *p++ = '4';
      *p++ = '\0';
      host = cidr_form_host;

      ip_mask = 0xFFFFFF00L;
      ip_host = ntohl(acptr->localClient->ip.s_addr);
    }


  if (parc > 2) /* host :reason */
    {
      if ( valid_comment(sptr,parv[2]) == 0 )
	return 0;

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
          sendto_one(sptr, ":%s NOTICE %s :Can't use a mask less than 24 with dline",
                     me.name,
                     parv[0]);
          return 0;
        }
    }

  if( ConfigFileEntry.non_redundant_klines && (aconf = match_Dline(ip_host)) )
     {
       char *reason;
       reason = aconf->passwd ? aconf->passwd : "<No Reason>";
       if(IsConfElined(aconf))
         sendto_one(sptr, ":%s NOTICE %s :[%s] is (E)d-lined by [%s] - %s",
                    me.name,
                    parv[0],
                    host,
                    aconf->host,reason);
         else
           sendto_one(sptr, ":%s NOTICE %s :[%s] already D-lined by [%s] - %s",
                      me.name,
                      parv[0],
                      host,
                      aconf->host,reason);
      return 0;
       
     }

  current_date = smalldate((time_t) 0);

  ircsprintf(buffer, "%s (%s)",reason,current_date);

  aconf = make_conf();
  aconf->status = CONF_DLINE;
  DupString(aconf->host,host);
  DupString(aconf->passwd,buffer);

  aconf->ip = ip_host;
  aconf->ip_mask = ip_mask;

  add_Dline(aconf);

  sendto_realops("%s added D-Line for [%s] [%s]",
		 sptr->name,
		 host,
		 reason);

  log(L_TRACE, "%s added D-Line for [%s] [%s]", 
      sptr->name, host, reason);

  dconf = get_conf_name(DLINE_TYPE);

  sendto_one(sptr,
	     ":%s NOTICE %s :Added D-Line [%s] to %s",
	     me.name,
	     sptr->name,
	     host,
	     dconf ? dconf : "configuration file");

  /*
   * Write dline to configuration file
   */
  WriteDline(dconf,
	     sptr,
	     host,
	     reason,
	     current_date);

  check_klines();
  return 0;
} /* m_dline() */

/*
 * WriteKline()
 * inputs	- filename
 * 		- client to report to
 * 		- actual client doing kline
 *		- username being klined
 *		- hostname being klined
 *		- reasons for kline
 *		- when (date)
 * output	- NONE
 * side effects	- Write out a kline to the kline configuration file
 */
void WriteKline(const char *filename, struct Client *sptr,
		struct Client *rcptr,
		const char *user, const char *host, const char *reason, 
		const char *when)
{
  char buffer[1024];
  FBFILE *out;

  if (filename == NULL)
    {
      sendto_realops("*** No kline file!");
      return;
    }

  if ((out = fbopen(filename, "a")) == NULL)
    {
      sendto_realops("Error opening %s: %s",
		     filename,
		     strerror(errno));
      return;
    }

  ircsprintf(buffer,
	     "#%s!%s@%s K'd: %s@%s:%s\n",
	     sptr->name,
	     sptr->username,
	     sptr->host,
	     user,
	     host,
	     reason);

  if (safe_write(sptr, filename, out, buffer) < 0)
    {
      fbclose(out);
      return;
    }

  ircsprintf(buffer, "K:%s:%s (%s):%s\n",
    host,
    reason,
    when,
    user);

  if (safe_write(sptr, filename, out, buffer) < 0)
    {
      fbclose(out);
      return;
    }

  fbclose(out);
} /* WriteKline() */

/*
 * WriteDline()
 * inputs	- filename
 * 		- client to report to
 * 		- actual client doing dline
 *		- hostname being dlined
 *		- reasons for dline
 *		- when (date)
 * output	- NONE
 * side effects	- Write out a kline to the kline configuration file
 */
void WriteDline(const char *filename, struct Client *sptr,
		const char *host, const char *reason, const char *when)
{
  char buffer[1024];
  FBFILE *out;

  if (filename == NULL)
    {
      sendto_realops("*** No kline file!");
      return;
    }

  if ((out = fbopen(filename, "a")) == NULL)
    {
      sendto_realops("Error opening %s: %s",
		     filename,
		     strerror(errno));
      return;
    }

  ircsprintf(buffer,
	     "#%s!%s@%s D'd: %s:%s (%s)\n",
	     sptr->name,
	     sptr->username,
	     sptr->host,
	     host,
	     reason,
	     when);

  if (safe_write(sptr, filename, out, buffer) < 0)
    return;

  ircsprintf(buffer, "D:%s:%s (%s)\n",
	     host,
	     reason,
	     when);

  if (safe_write(sptr, filename, out, buffer) < 0)
    {
      fbclose(out);
      return;
    }

  fbclose(out);
} /* WriteDline() */


/*
 * find_user_host
 * inputs	- pointer to client placing kline
 *              - pointer to user_host_or_nick
 *              - pointer to user buffer
 *              - pointer to host buffer
 * output	- 0 if not ok to kline, 1 to kline i.e. if valid user host
 * side effects -
 */
int find_user_host(struct Client *sptr,
		   char *user_host_or_nick, char *user, char *host)
{
  struct Client *acptr;
  char *hostp;

  if ( (hostp = strchr(user_host_or_nick, '@')) || *user_host_or_nick == '*' )
    {
      /* Explicit user@host mask given */

      if(hostp)                                    /* I'm a little user@host */
        {
          *(hostp++) = '\0';                       /* short and squat */
          strncpy(user,user_host_or_nick,USERLEN); /* here is my user */
          strncpy(host,hostp,HOSTLEN);             /* here is my host */
        }
      else
        {
          user[0] = '*';             /* no @ found, assume its *@somehost */
          user[1] = '\0';	  
          strncpy(host,user_host_or_nick,HOSTLEN);
        }

      return 1;
    }
  else
    {
      /* Try to find user@host mask from nick */

      if (!(acptr = find_chasing(sptr, user_host_or_nick, NULL)))
        return 0;

      if(!acptr->user)
        return 0;

      if (IsServer(acptr))
        {
	  sendto_one(sptr,
	     ":%s NOTICE %s :Can't KLINE a server, use @'s where appropriate",
		     me.name, sptr->name);
          return 0;
        }

      if(IsElined(acptr))
        {
          if(!IsServer(sptr))
            sendto_one(sptr,
                       ":%s NOTICE %s :%s is E-lined",me.name,sptr->name,
                       acptr->name);
          return 0;
        }

      /* turn the "user" bit into "*user", blow away '~'
       * if found in original user name (non-idented)
       */

      strncpy_irc(user, acptr->username, USERLEN);
      if (*acptr->username == '~')
        user[0] = '*';

      strncpy_irc(host,cluster(acptr->host),HOSTLEN);
    }

  return 1;
}

/*
 * valid_user_host
 * inputs	- pointer to client placing kline
 *              - pointer to user buffer
 *              - pointer to host buffer
 * output	- 0 if not valid user or host, 1 if valid
 * side effects -
 */
int valid_user_host( struct Client *sptr, char *user, char *host)
{
  /*
   * Check for # in user@host
   */

  if(strchr(host, '#'))
    {
      if(!IsServer(sptr))
        sendto_one(sptr, ":%s NOTICE %s :Invalid character '#' in hostname",
                   me.name, sptr->name);
      return 0;
    }
  if(strchr(user, '#'))
    { 
      if(!IsServer(sptr))
        sendto_one(sptr, ":%s NOTICE %s :Invalid character '#' in username",
                   me.name, sptr->name);
      return 0;
    }   

  return 1;
}

/*
 * valid_wild_card
 * input        - pointer to client
 *              - pointer to user to check
 *              - pointer to host to check
 * output       - 0 if not valid, 1 if valid
 * side effects -
 */
int valid_wild_card(struct Client *sptr, char *user, char *host)
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
  p = user;
  while ((tmpch = *p++))
  {
    if (!IsKWildChar(tmpch))
    {
      /*
       * If we find enough non-wild characters, we can
       * break - no point in searching further.
       */
      if (++nonwild >= NONWILDCHARS)
        break;
    }
  }

  if (nonwild < NONWILDCHARS)
  {
    /*
     * The user portion did not contain enough non-wild
     * characters, try the host.
     */
    p = host;
    while ((tmpch = *p++))
    {
      if (!IsKWildChar(tmpch))
        if (++nonwild >= NONWILDCHARS)
          break;
    }
  }

  if (nonwild < NONWILDCHARS)
  {
    /*
     * Not enough non-wild characters were found, assume
     * they are trying to kline *@*.
     */
    if (!IsServer(sptr))
      sendto_one(sptr,
        ":%s NOTICE %s :Please include at least %d non-wildcard characters with the user@host",
        me.name,
        sptr->name,
        NONWILDCHARS);

    return 0;
  }

  return 1;
}

/*
 * valid_comment
 * inputs	- pointer to client
 *              - pointer to comment
 * output       - 0 if no valid comment, 1 if valid
 * side effects - NONE
 */
int valid_comment(struct Client *sptr, char *comment)
{
  if(strchr(comment, ':'))
    {
      if(!IsServer(sptr))
	sendto_one(sptr,
		   ":%s NOTICE %s :Invalid character ':' in comment",
		   me.name, sptr->name);
      return 0;
    }

  if(strchr(comment, '#'))
    {
      if(!IsServer(sptr))
	sendto_one(sptr,
		   ":%s NOTICE %s :Invalid character '#' in comment",
		   me.name, sptr->name);
      return 0;
    }
  return 1;
}

/*
 * already_placed_kline
 * inputs	- pointer to client placing kline
 *		- user
 *		- host
 *		- ip 
 * output	- 1 if already placed, 0 if not
 * side effects - NONE
 */
int already_placed_kline( struct Client *sptr, char *user, char *host,
			  unsigned long ip)
{
  char *reason;
  struct ConfItem *aconf;

  if( ConfigFileEntry.non_redundant_klines && 
      (aconf = find_matching_mtrie_conf(host,user,ip)) )
     {
       if( aconf->status & CONF_KILL )
         {
           reason = aconf->passwd ? aconf->passwd : "<No Reason>";
           if(!IsServer(sptr))
             sendto_one(sptr,
                        ":%s NOTICE %s :[%s@%s] already K-lined by [%s@%s] - %s",
                        me.name,
                        sptr->name,
                        user,host,
                        aconf->user,aconf->host,reason);
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
int is_ip_kline(char *host,unsigned long *ip, unsigned long *ip_mask)
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

  if((is_address(host, ip, ip_mask)))
     {
       if( (p = strchr(host,'*')) )
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
