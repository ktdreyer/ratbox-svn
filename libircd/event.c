/*
 * Event Processing
 *
 * This code was borrowed from the squid web cache by Adrian Chadd.
 *
 * $Id$
 *
 * Original header follows:
 *
 * Id: event.c,v 1.30 2000/04/16 21:55:10 wessels Exp
 *
 * DEBUG: section 41    Event Processing
 * AUTHOR: Henrik Nordstrom
 *
 * SQUID Internet Object Cache  http://squid.nlanr.net/Squid/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from the
 *  Internet community.  Development is led by Duane Wessels of the
 *  National Laboratory for Applied Network Research and funded by the
 *  National Science Foundation.  Squid is Copyrighted (C) 1998 by
 *  the Regents of the University of California.  Please see the
 *  COPYRIGHT file for full details.  Squid incorporates software
 *  developed and/or copyrighted by other sources.  Please see the
 *  CREDITS file for full details.
 *
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

/*
 * How its used:
 *
 * Should be pretty self-explanatory. Events are one-shot, so once your
 * callback is called, you have to re-add the event through eventAdd().
 * Note that the event text is used by last_event_ran to point to the
 * last event run, so you should make sure its a static string. (Don't
 * ask me why, its how it is in the squid code..)
 *   -- Adrian
 */

#include <stdlib.h>

#include "ircd.h"
#include "event.h"
#include "client.h"
#include "send.h"
#include "memory.h"

/* The list of event processes */
struct ev_entry
{
  EVH *func;
  void *arg;
  const char *name;
  time_t when;
  struct ev_entry *next;
  int weight;
  int id;
};

static struct ev_entry *tasks = NULL;
static int run_id = 0;
static const char *last_event_ran = NULL;

void
eventAdd(const char *name, EVH * func, void *arg, time_t when, int weight)
{
  struct ev_entry *new_event = (struct ev_entry *)MyMalloc(sizeof(struct ev_entry));
  struct ev_entry *cur_event;
  struct ev_entry *last_event = NULL;

  new_event->func = func;
  new_event->arg = arg;
  new_event->name = name;
  new_event->when = CurrentTime + when;
  new_event->weight = weight;
  new_event->id = run_id;

  /* Insert after the last event with the same or earlier time */

  for (cur_event = tasks; cur_event; cur_event = cur_event->next)
    {
      if (cur_event->when > new_event->when)
        {
          new_event->next = cur_event;
          if (last_event == NULL)
            tasks = new_event;
          else
            last_event->next = new_event;
          return;
        }
      last_event = cur_event;
    }
  if (last_event == NULL)
    tasks = new_event;
  else
    last_event->next = new_event;
}

/* same as eventAdd but adds a random offset within +-1/3 of delta_ish */
void
eventAddIsh(const char *name, EVH * func, void *arg, time_t delta_ish, int weight)
{
  if (delta_ish >= 3.0)
    {
      const time_t two_third = (2 * delta_ish) / 3;
      delta_ish = two_third + ((random() % 1000) * two_third) / 1000;
      /*
       * XXX I hate the above magic, I don't even know if its right.
       * Grr. -- adrian
       */
    }
  eventAdd(name, func, arg, delta_ish, weight);
}

void
eventDelete(EVH * func, void *arg)
{
  struct ev_entry *event;
  struct ev_entry *last_event = NULL;

  for (event = tasks; event; event = event->next)
    {
      if ((event->func == func) && (event->arg == arg))
        {
          if (last_event != NULL)
            {
              last_event->next = event->next;
              MyFree(event);
              return;          
            }
          else
            {
              tasks = event->next;
              MyFree(event);
              return;
            }
        }
      last_event = event;
    }
}

void
eventRun(void)
{
  struct ev_entry *event = NULL;
  EVH *func;
  void *arg;
  int weight = 0;

  if (tasks == NULL)
    return;
  if (tasks->when > CurrentTime)
    return;
  run_id++;

  while ((event = tasks))
    {
      int valid = 1;
      if (event->when > CurrentTime)
        break;
      if (event->id == run_id)        /* was added during this run */
        break;
      if (weight)
        break;
      func = event->func;
      arg = event->arg;
      event->func = NULL;
      event->arg = NULL;
      tasks = event->next;
      if (valid)
        {
          weight += event->weight;
          /* XXX assumes ->name is static memory! */
          last_event_ran = event->name;
          func(arg);
        }
      MyFree(event);
    }
}

time_t
eventNextTime(void)
{
  if (!tasks)
    return (time_t) 100000;
  return (time_t) (tasks->when - CurrentTime);
}

void
eventInit(void)
{
 
}

void
eventFreeMemory(void)
{
  struct ev_entry *event;
  while ((event = tasks))
    {
      tasks = event->next;
      MyFree(event);
    }
}

int
eventFind(EVH * func, void *arg)
{
  struct ev_entry *event;
  for (event = tasks; event; event = event->next)
    {
      if (event->func == func && event->arg == arg)
        return 1;
    }
  return 0;
}

int
show_events(struct Client *source_p)
{
  struct ev_entry *e = tasks;
  if (last_event_ran)
    sendto_one(source_p, ":%s NOTICE %s :*** Last event to run: %s",
               me.name, source_p->name,
               last_event_ran);

  sendto_one(source_p,
     ":%s NOTICE %s :*** Operation            Next Execution  Weight",
     me.name, source_p->name);

  while (e != NULL)
    {
      sendto_one(source_p,
                 ":%s NOTICE %s :*** %-20s %-3d seconds     %d",
                 me.name, source_p->name,
                 e->name, (int)(e->when - CurrentTime), e->weight);
      e = e->next;
    }
  sendto_one(source_p, ":%s NOTICE %s :*** Finished", me.name, source_p->name);
  return 0;
}

/* void set_back_events(time_t by)
 * Input: Time to set back events by.
 * Output: None.
 * Side-effects: Sets back all events by "by" seconds.
 */
void
set_back_events(time_t by)
{
  struct ev_entry *e;
  for (e = tasks; e; e = e->next)
    if (e->when > by)
      e->when -= by;
    else
      e->when = 0;
}
