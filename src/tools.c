/*
 *  ircd-ratbox: A slightly useful ircd.
 *  tools.c: Various functions needed here and there.
 *
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 *
 *  Here is the original header:
 *
 *  Useful stuff, ripped from places ..
 *  adrian chadd <adrian@creative.net.au>
 *
 * When you update these functions make sure you update the ones in tools.h
 * as well!!!
 */

#include "stdinc.h"
#include "tools.h"
#include "balloc.h"

#ifndef NDEBUG
/*
 * frob some memory. debugging time.
 * -- adrian
 */
void
mem_frob(void *data, int len)
{
	/* correct for Intel only! little endian */
	unsigned char b[4] = { 0xef, 0xbe, 0xad, 0xde };
	int i;
	char *cdata = data;
	for (i = 0; i < len; i++)
	{
		*cdata = b[i % 4];
		cdata++;
	}
}
#endif

/*
 * init_dlink_nodes
 *
 */
static BlockHeap *dnode_heap;
void
init_dlink_nodes(void)
{
	dnode_heap = BlockHeapCreate(sizeof(dlink_node), DNODE_HEAP_SIZE);
	if(dnode_heap == NULL)
		outofmemory();
}

/*
 * make_dlink_node
 *
 * inputs	- NONE
 * output	- pointer to new dlink_node
 * side effects	- NONE
 */
dlink_node *
make_dlink_node(void)
{
	dlink_node *lp;

	lp = (dlink_node *) BlockHeapAlloc(dnode_heap);

	lp->next = NULL;
	lp->prev = NULL;
	return lp;
}

/*
 * free_dlink_node
 *
 * inputs	- pointer to dlink_node
 * output	- NONE
 * side effects	- free given dlink_node 
 */
void
free_dlink_node(dlink_node * ptr)
{
	assert(ptr != NULL);

	BlockHeapFree(dnode_heap, ptr);
}

/*
** Should not include any 'flag' characters like @ and %, or special chars
** like : * and #, but 8bit accented stuff is quite ok  -orabidoo
*/
static char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

static char base64_values[] = {
/* 00-15   */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 16-31   */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 32-47   */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1,
	-1, 63,
/* 48-63   */ 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, 0,
	-1, -1,
/* 64-79   */ -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
/* 80-95   */ 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1,
	-1, -1,
/* 96-111  */ -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
	39, 40,
/* 112-127 */ 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1,
	-1, -1,
/* 128-143 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 144-159 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 160-175 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 186-191 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 192-207 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 208-223 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 224-239 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1,
/* 240-255 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1
};


/*
 * base64_block will allocate and return a new block of memory
 * using MyMalloc().  It should be freed after use.
 */
int
base64_block(char **output, const char *data, int len)
{
	unsigned char *out;
	unsigned char *in = (unsigned char *) data;
	unsigned long int q_in;
	int i;
	int count = 0;

	out = MyMalloc(((((len + 2) - ((len + 2) % 3)) / 3) * 4) + 1);

	/* process 24 bits at a time */
	for (i = 0; i < len; i += 3)
	{
		q_in = 0;

		if(i + 2 < len)
		{
			q_in = (in[i + 2] & 0xc0) << 2;
			q_in |= in[i + 2];
		}

		if(i + 1 < len)
		{
			q_in |= (in[i + 1] & 0x0f) << 10;
			q_in |= (in[i + 1] & 0xf0) << 12;
		}

		q_in |= (in[i] & 0x03) << 20;
		q_in |= in[i] << 22;

		q_in &= 0x3f3f3f3f;

		out[count++] = base64_chars[((q_in >> 24))];
		out[count++] = base64_chars[((q_in >> 16) & 0xff)];
		out[count++] = base64_chars[((q_in >> 8) & 0xff)];
		out[count++] = base64_chars[((q_in) & 0xff)];
	}
	if((i - len) > 0)
	{
		out[count - 1] = '=';
		if((i - len) > 1)
			out[count - 2] = '=';
	}

	out[count] = '\0';
	*output = (char *) out;
	return count;
}

/*
 * unbase64_block will allocate and return a new block of memory
 * using MyMalloc().  It should be freed after use.
 */
int
unbase64_block(char **output, const char *data, int len)
{
	unsigned char *out;
	unsigned char *in = (unsigned char *) data;
	unsigned long int q_in;
	int i;
	int count = 0;

	if((len % 4) != 0)
		return 0;

	out = MyMalloc(((len / 4) * 3) + 1);

	/* process 32 bits at a time */
	for (i = 0; (i + 3) < len; i += 4)
	{
		/* compress input (chars a, b, c and d) as follows:
		 * (after converting ascii -> base64 value)
		 *
		 * |00000000aaaaaabbbbbbccccccdddddd|
		 * |  765432  107654  321076  543210|
		 */

		q_in = 0;

		if(base64_values[in[i + 3]] > -1)
			q_in |= base64_values[in[i + 3]];
		if(base64_values[in[i + 2]] > -1)
			q_in |= base64_values[in[i + 2]] << 6;
		if(base64_values[in[i + 1]] > -1)
			q_in |= base64_values[in[i + 1]] << 12;
		if(base64_values[in[i]] > -1)
			q_in |= base64_values[in[i]] << 18;

		out[count++] = (q_in >> 16) & 0xff;
		out[count++] = (q_in >> 8) & 0xff;
		out[count++] = (q_in) & 0xff;
	}

	if(in[i - 1] == '=')
		count--;
	if(in[i - 2] == '=')
		count--;

	out[count] = '\0';
	*output = (char *) out;
	return count;
}

