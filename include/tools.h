/*
 * tools.h
 *
 * Definitions/prototypes for src/tools.c
 *
 * Adrian Chadd <adrian@creative.net.au>
 *
 * $Id$
 */
#ifndef __TOOLS_H__
#define __TOOLS_H__

/*
 * double-linked-list stuff
 */
typedef struct _dlink_node dlink_node;
typedef struct _dlink_list dlink_list;

struct _dlink_node {
    void *data;
    dlink_node *prev;
    dlink_node *next;
};
  
struct _dlink_list {
    dlink_node *head;
    dlink_node *tail;
};

#endif
