/*
 * resv.c
 *
 * $Id$
 */
#include "tools.h"
#include "restart.h"
#include "common.h"
#include "fdlist.h"
#include "ircd.h"
#include "send.h"
#include "s_debug.h"
#include "s_log.h"
#include "client.h"   
#include "memory.h"
#include "resv.h"
#include "ircd_defs.h"

#include <unistd.h>
#include <stdlib.h>

struct Resv *ResvList;

struct Resv *create_resv(char *name, int type, int conf)
{
  struct Resv *resv_p;
  int len;

  len = strlen(name);
  
  if ((type == RESV_CHANNEL) && (len > CHANNELLEN))
  {
    len = CHANNELLEN;
    *(name + CHANNELLEN) = '\0';
  }
  else if ((type == RESV_NICK) && (len > NICKLEN))
  {
    len = NICKLEN;
    *(name + NICKLEN) = '\0';
  }

  if(resv_p = (struct Resv *)hash_find_resv(name, (struct Resv *)NULL, type))
    return NULL;

  resv_p = (struct Resv *)MyMalloc(sizeof(struct Resv) + len + 1);
  
  strcpy(resv_p->name, name);
  resv_p->type = type;
  resv_p->conf = conf;

  if(ResvList)
    ResvList->prev = resv_p;

  resv_p->next = ResvList;
  resv_p->prev = NULL;

  ResvList = resv_p;

  add_to_resv_hash_table(resv_p->name, resv_p);

  return resv_p;
}

int clear_conf_resv()
{
  struct Resv *resv_p;
  struct Resv *next_p;

  for(resv_p = ResvList; resv_p; resv_p = next_p)
  {
    next_p = resv_p->next;

    if(resv_p->conf)
      delete_resv(resv_p);
  }
}

int delete_resv(struct Resv *resv_p)
{
  if(!(resv_p))
    return 0;

  del_from_resv_hash_table(resv_p->name, resv_p, resv_p->type);
  
  if(resv_p->prev)
    resv_p->prev->next = resv_p->next;
  else
    ResvList = resv_p->next;

  if(resv_p->next)
    resv_p->next->prev = resv_p->prev;

  MyFree((char *)resv_p);

  return 1;
}    

int find_resv(char *name, int type)
{
  struct Resv *resv_p;

  if(!(resv_p = (struct Resv *)hash_find_resv(name, (struct Resv *)NULL, type)))
    return 0;

  return 1;
}  
