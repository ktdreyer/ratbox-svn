/*
 * tools.c
 * 
 * Useful stuff, ripped from places ..
 *
 * adrian chadd <adrian@creative.net.au>
 *
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "tools.h"


/* 
 * dlink_ routines are stolen from squid, except for dlinkAddBefore,
 * which is mine.
 *   -- adrian
 */
void
dlinkAdd(void *data, dlink_node * m, dlink_list * list)
{
    m->data = data;
    m->prev = NULL;
    m->next = list->head;
    if (list->head)
        list->head->prev = m;
    list->head = m;
    if (list->tail == NULL)
        list->tail = m;
}

void
dlinkAddBefore(dlink_node *b, void *data, dlink_node *m, dlink_list *list)
{
    /* Shortcut - if its the first one, call dlinkAdd only */
    if (b == list->head)
        dlinkAdd(data, m, list);
    else {
        m->data = data;
        b->prev->next = m;
        m->prev = b->prev;
        b->prev = m; 
        m->next = b;
    }
}

void
dlinkAddTail(void *data, dlink_node *m, dlink_list *list)
{
    m->data = data;
    m->next = NULL;
    m->prev = list->tail;
    if (list->tail)
        list->tail->next = m;
    list->tail = m;
    if (list->head == NULL)
        list->head = m;
}

void
dlinkDelete(dlink_node *m, dlink_list *list)
{
    if (m->next)
        m->next->prev = m->prev;
    if (m->prev)
        m->prev->next = m->next;

    if (m == list->head)
        list->head = m->next;
    if (m == list->tail)
        list->tail = m->prev;
        
    m->next = m->prev = NULL;
}


void
dlinkMoveList(dlink_list *from, dlink_list *to)
{
    if(from->tail != NULL)
      from->tail->next = to->head;
    if((from->head !=NULL) && (to->head != NULL))
      from->head->prev = to->head->prev;
    if(to->head != NULL)
      to->head->prev = from->tail;
    to->head = from->head;
    from->head = from->tail = NULL;
}
