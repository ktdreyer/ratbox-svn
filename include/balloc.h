/************************************************************************
 *  ircd hybrid - Internet Relay Chat Daemon, include/balloc.h
 *  balloc.h: The block allocator header.
 *  Copyright(C) 2001 by the past and present ircd-hybrid teams.
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * $Id$
 */

#ifndef _I_BALLOC_H
#define _I_BALLOC_H

typedef struct BlockHeap BlockHeap;

/* Only include the block allocator stuff if we use it... */
#ifndef NOBALLOC
/* The functions we need to provide... */
void initBlockHeap(void);
void BlockHeapGarbageCollect(BlockHeap *b);
BlockHeap *BlockHeapCreate(int elsize, int elperblock);
void BlockHeapDestroy(BlockHeap *b);

#else /* NOBALLOC */
#define initBlockHeap()
#define BlockHeapGarbageCollect(x)
/* This is really kludgy, passing ints as pointers is always bad. */
#define BlockHeapCreate(es, epb) ((BlockHeap*)(es))
#define BlockHeapDestroy(x)
#endif
#endif /* _I_BALLOC_H */
