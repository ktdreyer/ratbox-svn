/*
 * balloc.h - Based roughly on Wohali's old block allocator.
 *
 * Mangled by Aaron Sethman <androsyn@ratbox.org>
 * Below is the original header found on this file
 *
 * File:   blalloc.h
 * Owner:   Wohali (Joan Touzet)
 *
 *
 * $Id$
 */
#ifndef INCLUDED_blalloc_h
#define INCLUDED_blalloc_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>       /* size_t */
#define INCLUDED_sys_types_h
#endif

#ifndef INCLUDED_stddef_h
#include <stddef.h>
#define INCLUDED_stddef_h
#endif
#include "tools.h"
#include "memory.h"
#include "ircd_defs.h"


/* 
 * Block contains status information for an allocated block in our
 * heap.
 */


struct Block {
	int		freeElems;		/* Number of available elems */
	size_t		alloc_size;
	struct Block*	next;			/* Next in our chain of blocks */
	void*		elems;			/* Points to allocated memory */
	dlink_list	free_list;
	dlink_list	used_list;					
};

typedef struct Block Block;

struct MemBlock {
	dlink_node self;		
	Block *block;				/* Which block we belong to */
	void *data;				/* Maybe pointless? :P */
};
typedef struct MemBlock MemBlock;

/* 
 * BlockHeap contains the information for the root node of the
 * memory heap.
 */
struct BlockHeap {
   size_t  elemSize;                    /* Size of each element to be stored */
   int     elemsPerBlock;               /* Number of elements per block */
   int     blocksAllocated;             /* Number of blocks allocated */
   int     freeElems;                   /* Number of free elements */
   Block*  base;                        /* Pointer to first block */
};

typedef struct BlockHeap BlockHeap;


extern BlockHeap* BlockHeapCreate(size_t elemsize, int elemsperblock);
extern int        BlockHeapDestroy(BlockHeap *bh);
extern int        BlockHeapFree(BlockHeap *bh, void *ptr);
extern void *	  BlockHeapAlloc(BlockHeap *bh);
extern int        BlockHeapGarbageCollect(BlockHeap *);

extern void       BlockHeapCountMemory(BlockHeap *bh,int *, int *);
extern void       BlockHeapDump(BlockHeap *bh,int fd);


#endif /* INCLUDED_blalloc_h */

