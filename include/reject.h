/*
 * reject.h
 *
 * $Id$
 */
#ifndef INCLUDED_reject_h
#define INCLUDED_reject_h

void init_reject(void);
int check_reject(struct Client *);
void add_reject(struct Client *);
#endif

