/************************************************************************
*   IRC - Internet Relay Chat, src/whowas.c
*   Copyright (C) 1990 Markku Savela
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
#include "whowas.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "numeric.h"
#include "s_serv.h"
#include "s_user.h"
#include "send.h"
#include "s_conf.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>


/* internally defined function */
static void add_whowas_to_clist(struct Whowas **, struct Whowas *);
static void del_whowas_from_clist(struct Whowas **,struct Whowas *);
static void add_whowas_to_list(struct Whowas **, struct Whowas *);
static void del_whowas_from_list(struct Whowas **,struct Whowas *);

struct Whowas WHOWAS[NICKNAMEHISTORYLENGTH];
struct Whowas *WHOWASHASH[WW_MAX];

static int whowas_next = 0;

unsigned int hash_whowas_name(const char* name)
{
  unsigned int h = 0;

  while (*name)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*name++));
    }
  return(h & (WW_MAX - 1));
}

void add_history(struct Client* cptr, int online)
{
  struct Whowas* who = &WHOWAS[whowas_next];

  assert(0 != cptr);
  /*
   * XXX - can these be checked at compile time instead??
   */
  assert(sizeof(cptr->username) == sizeof(who->username));
  assert(sizeof(cptr->host)     == sizeof(who->hostname));
  assert(sizeof(cptr->info)     == sizeof(who->realname));

  if (who->hashv != -1)
    {
      if (who->online)
        del_whowas_from_clist(&(who->online->whowas),who);
      del_whowas_from_list(&WHOWASHASH[who->hashv], who);
    }
  who->hashv = hash_whowas_name(cptr->name);
  who->logoff = CurrentTime;
  /*
   * NOTE: strcpy ok here, the sizes in the client struct MUST
   * match the sizes in the whowas struct
   */
  strncpy_irc(who->name, cptr->name, NICKLEN);
  who->name[NICKLEN] = '\0';
  strcpy(who->username, cptr->username);
  strcpy(who->hostname, cptr->host);
  strcpy(who->realname, cptr->info);

  /* Its not string copied, a pointer to the scache hash is copied
     -Dianora
   */
  /*  strncpy_irc(who->servername, cptr->user->server, HOSTLEN); */
  who->servername = cptr->user->server;

  if (online)
    {
      who->online = cptr;
      add_whowas_to_clist(&(cptr->whowas), who);
    }
  else
    who->online = NULL;
  add_whowas_to_list(&WHOWASHASH[who->hashv], who);
  whowas_next++;
  if (whowas_next == NICKNAMEHISTORYLENGTH)
    whowas_next = 0;
}

void off_history(struct Client *cptr)
{
  struct Whowas *temp, *next;

  for(temp=cptr->whowas;temp;temp=next)
    {
      next = temp->cnext;
      temp->online = NULL;
      del_whowas_from_clist(&(cptr->whowas), temp);
    }
}

struct Client *get_history(char *nick,time_t timelimit)
{
  struct Whowas *temp;
  int blah;

  timelimit = CurrentTime - timelimit;
  blah = hash_whowas_name(nick);
  temp = WHOWASHASH[blah];
  for(;temp;temp=temp->next)
    {
      if (irccmp(nick, temp->name))
        continue;
      if (temp->logoff < timelimit)
        continue;
      return temp->online;
    }
  return NULL;
}

void    count_whowas_memory(int *wwu,
                            u_long *wwum)
{
  struct Whowas *tmp;
  int i;
  int     u = 0;
  u_long  um = 0;

  /* count the number of used whowas structs in 'u' */
  /* count up the memory used of whowas structs in um */

  for (i = 0, tmp = &WHOWAS[0]; i < NICKNAMEHISTORYLENGTH; i++, tmp++)
    if (tmp->hashv != -1)
      {
        u++;
        um += sizeof(struct Whowas);
      }
  *wwu = u;
  *wwum = um;
  return;
}

void    initwhowas()
{
  int i;

  for (i=0;i<NICKNAMEHISTORYLENGTH;i++)
    {
      memset((void *)&WHOWAS[i], 0, sizeof(struct Whowas));
      WHOWAS[i].hashv = -1;
    }
  for (i=0;i<WW_MAX;i++)
    WHOWASHASH[i] = NULL;        
}


static void add_whowas_to_clist(struct Whowas **bucket,struct Whowas *whowas)
{
  whowas->cprev = NULL;
  if ((whowas->cnext = *bucket) != NULL)
    whowas->cnext->cprev = whowas;
  *bucket = whowas;
}
 
static void    del_whowas_from_clist(struct Whowas **bucket,
				     struct Whowas *whowas)
{
  if (whowas->cprev)
    whowas->cprev->cnext = whowas->cnext;
  else
    *bucket = whowas->cnext;
  if (whowas->cnext)
    whowas->cnext->cprev = whowas->cprev;
}

static void add_whowas_to_list(struct Whowas **bucket, struct Whowas *whowas)
{
  whowas->prev = NULL;
  if ((whowas->next = *bucket) != NULL)
    whowas->next->prev = whowas;
  *bucket = whowas;
}
 
static void    del_whowas_from_list(struct Whowas **bucket,
				    struct Whowas *whowas)
{
  if (whowas->prev)
    whowas->prev->next = whowas->next;
  else
    *bucket = whowas->next;
  if (whowas->next)
    whowas->next->prev = whowas->prev;
}
