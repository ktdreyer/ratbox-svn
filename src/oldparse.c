/************************************************************************
 *   IRC - Internet Relay Chat, src/oldparse.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
 *  (C) 1988 University of Oulu,Computing Center and Jarkko Oikarinen"
 *
 *  $Id$
 */
#include "tools.h"
#include "s_log.h"
#include "s_conf.h"
#include "client.h"
#include "irc_string.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "memdebug.h"

static char *getfield(char *newline);
static  int  oper_privs_from_string(int,char *);
static  int  oper_flags_from_string(char *);

/*
 * oldParseOneLine
 * Inputs       - pointer to line to parse
 *              - pointer to conf item to add
 * Output       - pointer to aconf if aconf to be added
 *                to link list or NULL if not
 * Side Effects - Parse one old style conf line.
 *
 * Ok, a bit of justification here:
 * There were some of us on the h7 project who felt K/D lines
 * should be in a new format... However... the parser to handle that
 * is expensive CPU wise for something that is rarely read or needs
 * to be handled by humans. So... h7 will support and write K/D lines
 * out in "old style" but will not understand any ircd.conf written in
 * old style.
 *
 */

void oldParseOneLine(char* line,struct ConfItem* aconf,
                            int* pccount,int* pncount)
{
  char conf_letter;
  char* tmp;
  char* user_field=(char *)NULL;
  char* pass_field=(char *)NULL;
  char* host_field=(char *)NULL;
  char* port_field=(char *)NULL;
  char* class_field=(char *)NULL;
  int   sendq = 0;

  tmp = getfield(line);
  if (!tmp)
    return;

  conf_letter = *tmp;

  for (;;) /* Fake loop, that I can use break here --msa */
    {
      /* host field */
      if ((host_field = getfield(NULL)) == NULL)
	break;
      
      /* pass field */
      if ((pass_field = getfield(NULL)) == NULL)
	break;

      /* user field */
      if ((user_field = getfield(NULL)) == NULL)
	break;

      /* port field */
      if ((port_field = getfield(NULL)) == NULL)
	break;

      /* class field */
      if ((class_field = getfield(NULL)) == NULL)
	break;
      
      break;
      /* NOTREACHED */
    }

  aconf->flags = 0;

  switch( conf_letter )
    {
    case 'd':
      aconf->status = CONF_DLINE;
      aconf->flags = CONF_FLAGS_E_LINED;
      conf_add_fields(aconf,host_field,pass_field,user_field,
		      port_field,class_field);
      conf_add_d_line(aconf);
      break;

    case 'D': /* Deny lines (immediate refusal) */
      aconf->status = CONF_DLINE;
      conf_add_fields(aconf,host_field,pass_field,user_field,
		      port_field,class_field);
      conf_add_d_line(aconf);
      break;

    case 'K': /* Kill user line on irc.conf           */
    case 'k':
      aconf->status = CONF_KILL;
      conf_add_fields(aconf,host_field,pass_field,user_field,
		      port_field,class_field);
      conf_add_k_line(aconf);
      break;
      
    default:
      free_conf(aconf);
      log(L_ERROR, "Error in config file: %s", line);
      break;
    }
}

/*
 * field breakup for ircd.conf file.
 */
static char *getfield(char *newline)
{
  static char *line = (char *)NULL;
  char  *end, *field;
        
  if (newline)
    line = newline;

  if (line == (char *)NULL)
    return((char *)NULL);

  field = line;
  if ((end = strchr(line,':')) == NULL)
    {
      line = (char *)NULL;
      if ((end = strchr(field,'\n')) == (char *)NULL)
        end = field + strlen(field);
    }
  else
    line = end + 1;
  *end = '\0';
  return(field);
}

/* oper_privs_from_string
 *
 * inputs        - default privs
 *               - privs as string
 * output        - default privs as modified by privs string
 * side effects -
 *
 */

static int oper_privs_from_string(int int_privs,char *privs)
{
  while(*privs)
    {
      if(*privs == 'O')                     /* allow global kill */
        int_privs |= CONF_OPER_GLOBAL_KILL;
      else if(*privs == 'o')                /* disallow global kill */
        int_privs &= ~CONF_OPER_GLOBAL_KILL;
      else if(*privs == 'U')                /* allow unkline */
        int_privs |= CONF_OPER_UNKLINE;
      else if(*privs == 'u')                /* disallow unkline */
        int_privs &= ~CONF_OPER_UNKLINE;
      else if(*privs == 'R')                /* allow remote squit/connect etc.*/
        int_privs |= CONF_OPER_REMOTE;        
      else if(*privs == 'r')                /* disallow remote squit/connect etc.*/
        int_privs &= ~CONF_OPER_REMOTE;
      else if(*privs == 'N')                /* allow +n see nick changes */
        int_privs |= CONF_OPER_N;
      else if(*privs == 'n')                /* disallow +n see nick changes */
        int_privs &= ~CONF_OPER_N;
      else if(*privs == 'K')                /* allow kill and kline privs */
        int_privs |= CONF_OPER_K;
      else if(*privs == 'k')                /* disallow kill and kline privs */
        int_privs &= ~CONF_OPER_K;
      else if(ConfigFileEntry.glines && *privs == 'G')                /* allow gline */
        int_privs |= CONF_OPER_GLINE;
      else if(ConfigFileEntry.glines && *privs == 'g')                /* disallow gline */
        int_privs &= ~CONF_OPER_GLINE;
      else if(*privs == 'H')                /* allow rehash */
        int_privs |= CONF_OPER_REHASH;
      else if(*privs == 'h')                /* disallow rehash */
        int_privs &= ~CONF_OPER_REHASH;
      else if(*privs == 'D')
        int_privs |= CONF_OPER_DIE;         /* allow die */
      else if(*privs == 'd')
        int_privs &= ~CONF_OPER_DIE;        /* disallow die */
      else if(*privs == 'A')
        int_privs |= CONF_OPER_ADMIN;       
      else if(*privs == 'a')
	int_privs &= ~CONF_OPER_ADMIN;
      privs++;
    }
  return(int_privs);
}

/* oper_flags_from_string
 *
 * inputs        - flags as string
 * output        - flags as bit mask
 * side effects -
 */

static int oper_flags_from_string(char *flags)
{
  int int_flags=0;

  while(*flags)
    {
      if(*flags == 'i')                        /* invisible */
        int_flags |= FLAGS_INVISIBLE;
      else if(*flags == 'w')                /* see wallops */
        int_flags |= FLAGS_WALLOP;
      else if(*flags == 's')
        int_flags |= FLAGS_SERVNOTICE;
      else if(*flags == 'c')
        int_flags |= FLAGS_CCONN;
      else if(*flags == 'r')
        int_flags |= FLAGS_REJ;
      else if(*flags == 'k')
        int_flags |= FLAGS_SKILL;
      else if(*flags == 'f')
        int_flags |= FLAGS_FULL;
      else if(*flags == 'y')
        int_flags |= FLAGS_SPY;
      else if(*flags == 'd')
        int_flags |= FLAGS_DEBUG;
      else if(*flags == 'n')
        int_flags |= FLAGS_NCHANGE;
      flags++;
    }

  return(int_flags);
}

