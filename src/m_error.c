/************************************************************************
 *   IRC - Internet Relay Chat, src/m_error.c
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
#include "handlers.h"
#include "client.h"
#include "common.h"   /* FALSE */
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_debug.h"
#include "msg.h"

#if 0
struct Message error_msgtab = {
  MSG_ERROR, 0, 0, MFLG_SLOW | MFLG_UNREG, 0,
  {mr_error, m_ignore, ms_error, m_ignore}
};

void
_modinit(void)
{
  mod_add_cmd(MSG_ERROR, &error_msgtab);
}
#endif

/*
 * Note: At least at protocol level ERROR has only one parameter,
 * although this is called internally from other functions
 * --msa
 *
 *      parv[0] = sender prefix
 *      parv[*] = parameters
 */
int m_error(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char* para;

  para = (parc > 1 && *parv[1] != '\0') ? parv[1] : "<>";
  
  Debug((DEBUG_ERROR,"Received ERROR message from %s: %s",
         sptr->name, para));
  /*
   * Ignore error messages generated by normal user clients
   * (because ill-behaving user clients would flood opers
   * screen otherwise). Pass ERROR's from other sources to
   * the local operator...
   */
  if (IsPerson(cptr) || IsUnknown(cptr))
    return 0;
  if (cptr == sptr)
    sendto_ops("ERROR :from %s -- %s",
               get_client_name(cptr, FALSE), para);
  else
    sendto_ops("ERROR :from %s via %s -- %s", sptr->name,
               get_client_name(cptr,FALSE), para);
  return 0;
}

int mr_error(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char* para;

  para = (parc > 1 && *parv[1] != '\0') ? parv[1] : "<>";
  
  Debug((DEBUG_ERROR,"Received ERROR message from %s: %s",
         sptr->name, para));
  /*
   * Ignore error messages generated by normal user clients
   * (because ill-behaving user clients would flood opers
   * screen otherwise). Pass ERROR's from other sources to
   * the local operator...
   */
  if (IsPerson(cptr) || IsUnknown(cptr))
    return 0;
  if (cptr == sptr)
    sendto_ops("ERROR :from %s -- %s",
               get_client_name(cptr, FALSE), para);
  else
    sendto_ops("ERROR :from %s via %s -- %s", sptr->name,
               get_client_name(cptr,FALSE), para);
  return 0;
}

int ms_error(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char* para;

  para = (parc > 1 && *parv[1] != '\0') ? parv[1] : "<>";
  
  Debug((DEBUG_ERROR,"Received ERROR message from %s: %s",
         sptr->name, para));
  /*
   * Ignore error messages generated by normal user clients
   * (because ill-behaving user clients would flood opers
   * screen otherwise). Pass ERROR's from other sources to
   * the local operator...
   */
  if (IsPerson(cptr) || IsUnknown(cptr))
    return 0;
  if (cptr == sptr)
    sendto_ops("ERROR :from %s -- %s",
               get_client_name(cptr, FALSE), para);
  else
    sendto_ops("ERROR :from %s via %s -- %s", sptr->name,
               get_client_name(cptr,FALSE), para);
  return 0;
}


