/************************************************************************
 *   IRC - Internet Relay Chat, include/md5.h
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
 **************************************************************************/

#ifndef __INCLUDE_MD5_H_INCLUDED
#define __INCLUDE_MD5_H_INCLUDED

#include "setup.h"

#define MD5_HASH_SIZE   4
#define	MD5_BLOCK_SIZE	16

#define rotl(x,n) ((((x)<<(n))&(-(1<<(n))))|(((x)>>(32-(n)))&((1<<(n))-1)))
#define F(x,y,z) (((x)&(y))|((~x)&(z)))
#define G(x,y,z) (((x)&(z))|((y)&(~z)))
#define H(x,y,z) ((x)^(y)^(z))
#define I(x,y,z) ((y)^((x)|(~z)))

/* Length of IDs to generate; at most IDLEN (12) */
#define ID_GEN_LEN 9

void id_init(void);
void save_random(void);
void md5_block (u_int32_t *, u_int32_t *, u_int32_t *);
void id_reseed(char *, int);
char *id_get(void);
char *cookie_get(void);

#endif
