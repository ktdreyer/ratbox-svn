/************************************************************************
 *   IRC - Internet Relay Chat, src/s_misc.c
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
 *  $Id$
 */
#include "s_misc.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "res.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "memory.h"

#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <time.h>
#include <unistd.h>


static char* months[] = {
  "January",   "February", "March",   "April",
  "May",       "June",     "July",    "August",
  "September", "October",  "November","December"
};

static char* weekdays[] = {
  "Sunday",   "Monday", "Tuesday", "Wednesday",
  "Thursday", "Friday", "Saturday"
};

char* date(time_t lclock) 
{
  static        char        buf[80], plus;
  struct        tm *lt, *gm;
  struct        tm        gmbuf;
  int        minswest;

  if (!lclock) 
    lclock = CurrentTime;
  gm = gmtime(&lclock);
  memcpy((void *)&gmbuf, (void *)gm, sizeof(gmbuf));
  gm = &gmbuf;
  lt = localtime(&lclock);

  if (lt->tm_yday == gm->tm_yday)
    minswest = (gm->tm_hour - lt->tm_hour) * 60 +
      (gm->tm_min - lt->tm_min);
  else if (lt->tm_yday > gm->tm_yday)
    minswest = (gm->tm_hour - (lt->tm_hour + 24)) * 60;
  else
    minswest = ((gm->tm_hour + 24) - lt->tm_hour) * 60;

  plus = (minswest > 0) ? '-' : '+';
  if (minswest < 0)
    minswest = -minswest;
  
  ircsprintf(buf, "%s %s %d %d -- %02u:%02u:%02u %c%02u:%02u",
          weekdays[lt->tm_wday], months[lt->tm_mon],lt->tm_mday,
          lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec,
          plus, minswest/60, minswest%60);

  return buf;
}

const char* smalldate(time_t lclock)
{
  static  char    buf[MAX_DATE_STRING];
  struct  tm *lt, *gm;
  struct  tm      gmbuf;

  if (!lclock)
    lclock = CurrentTime;
  gm = gmtime(&lclock);
  memcpy((void *)&gmbuf, (void *)gm, sizeof(gmbuf));
  gm = &gmbuf; 
  lt = localtime(&lclock);
  
  ircsprintf(buf, "%d/%d/%d %02d.%02d",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min);
  
  return buf;
}


/*
 * small_file_date
 * Make a small YYYYMMDD formatted string suitable for a
 * dated file stamp. 
 */
char* small_file_date(time_t lclock)
{
  static  char    timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;

  if (!lclock)
    time(&lclock);
  tmptr = localtime(&lclock);
  strftime(timebuffer, MAX_DATE_STRING, "%Y%m%d", tmptr);
  return timebuffer;
}

