/************************************************************************
 *  ircd hybrid - Internet Relay Chat Daemon, src/balloc.c
 *  balloc.c: The block allocator.
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

/* A note on the algorithm:
 *  - Unlike past block allocators, this one uses a single linked list
 *    for block segments. This makes garbage collection much slower but
 *    should drastically increase allocation and de-allocation times.
 *    This is okay, because garbage collection occurs much less
 *    frequently than allocation and de-allocation.
 * The following table is a rough guideline only...
 * n = number of blocks, m = elements per block.
 *|     Action      |  Estimated rel. freq. | Worst case bit complexity
 *|   Heap setup    |             1         |       O(1) + block alloc
 *|Block allocation |            10^3       |       O(n)
 *|Element allocate |            10^6       |       O(1)
 *|  Element free   |            10^6       |       O(1)
 *| Garbage collect |            10^3       |       O(nm)
 * - A1kmm
 */

#include "tools.h"
#include "memory.h"
#include <assert.h>

#ifndef NOBALLOC
/* First some structures. I put them here and not in balloc.h because
 * I didn't want them to be exposed outside this file, and I also don't
 * really want to add another header... -A1kmm
 */
struct BlockHeap
{
 dlink_list f_elements, blocks;
 int elsize, elsperblock, blocksize;
};

/*
psuedo-struct BHElement
{
 dlink_node elementn;
 char data[elsize];
};
*/

struct BHBlock
{
 dlink_node blockn;
 int usedcount;
 /*
 pseudo-struct BHElement elements[elsperblock];
 */
};

void BlockHeapAddBlock(BlockHeap *bh);

/* Called once to setup the blockheap code... */
void
initBlockHeap(void)
{
 /* The old block-heap did weird stuff with /dev/zero here which I think
  * we could and probably should avoid, and just use MyMalloc to get
  * blocks(it shouldn't happen too often anyway). -A1kmm */
}

/* Add a block to the blockheap... */
void
BlockHeapAddBlock(BlockHeap *bh)
{
 char *d;
 int i;
 struct BHBlock *bhb = MyMalloc(bh->blocksize);
 dlinkAdd(bhb, &bhb->blockn, &bh->blocks);
 d = ((char*)bhb) + sizeof(struct BHBlock);
 bhb->usedcount = 0;
 /* On the front is the best because of memory caches/swap/paging. */
 for (i=0; i<bh->elsperblock; i++)
 {
  dlinkAdd(bhb,
           (dlink_node*)d, &bh->f_elements);
  d += sizeof(dlink_node) + bh->elsize;
 }
}

/* Create a blockheap... */
BlockHeap*
BlockHeapCreate(int elsize, int elsperblock)
{
 BlockHeap *bh = MyMalloc(sizeof(*bh));
#ifdef MEMDEBUG
 /* Squeeze in the memory header too... -A1kmm */
 elsize += sizeof(MemoryEntry);
#endif
 memset(bh, 0, 2*sizeof(dlink_list));
 bh->elsize = elsize;
 bh->elsperblock = elsperblock;
 bh->blocksize = elsperblock * (elsize + sizeof(dlink_node)) +
                 sizeof(struct BHBlock);
 return bh;
}

/* Allocate an element from the free pool, making new blocks if needed.
 */
void*
_BlockHeapAlloc(BlockHeap *bh)
{
 char *d;
 if (bh->f_elements.head == NULL)
   BlockHeapAddBlock(bh);
 d = (char*)(bh->f_elements.head + 1);
 ((struct BHBlock*)bh->f_elements.head->data)->usedcount++;
 bh->f_elements.head = bh->f_elements.head->next;
 if (bh->f_elements.head == NULL)
  bh->f_elements.tail = NULL;
 else 
  bh->f_elements.head->prev = NULL;
 /* No need to "frob" when debugging here, it is done on initial
  * MyMalloc and after each free. -A1kmm */
 return d;
}

/* Release an element back into the pool... */
void
_BlockHeapFree(BlockHeap *bh, void *el)
{
 dlink_node *dln = (el-sizeof(dlink_node));
#ifdef MEMDEBUG
 mem_frob(el, bh->elsize);
#endif
 ((struct BHBlock*)dln->data)->usedcount--;
 /* On the front is the best because of memory caches/swap/paging. 
  * It also should make garbage collection work better... -A1kmm */
 dlinkAdd(dln->data, dln, &bh->f_elements);
}

/* Destroy a blockheap... */
void
BlockHeapDestroy(BlockHeap *bh)
{
 struct BHBlock *bhb;
 for (bhb = (struct BHBlock*)bh->blocks.head; bhb;
      bhb = (struct BHBlock*)bhb->blockn.next)
   MyFree(bhb);
}

/* Destroy empty blocks. Note that this is slow because we put off all
 * processing until this late to save speed in the frequently called
 * routines.
 * Okay, that is not really the case any more, so now the garbage
 * collector doesn't take 10s...
 **/
void
BlockHeapGarbageCollect(BlockHeap *bh)
{
 struct BHBlock *bhb, *bhbn;
 char *d;
 int i;
 for (bhb=(struct BHBlock*)bh->blocks.head; bhb; bhb=bhbn)
 {
   bhbn = (struct BHBlock*)bhb->blockn.next;
   if (bhb->usedcount != 0)
     continue;
   d = (char*)(bhb+1);
   for (i=0; i<bh->elsperblock; i++)
   {
    dlinkDelete((dlink_node*)d, &bh->f_elements);
    d += sizeof(dlink_node) + bh->elsize;
   }
   dlinkDelete(&bhb->blockn, &bh->blocks);
   MyFree(bhb);
 }
}
#endif
