/************************************************************************
 *   IRC - Internet Relay Chat, iauth/mtree.h
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
 *   $Id$
 */

#ifndef INCLUDED_mtree_h
#define INCLUDED_mtree_h

struct Iline;

/*
 * The format for our modified tree is something like this:
 *
 * struct Level -----> IlineTree
 *                         |
 *                         |
 *       -------------------
 *       |
 *     "net" --> "com" --> "org" --> "edu"
 *                /
 *               /
 *              /
 *             /
 *          "varner" ---> "blackened" ---> ...
 *              |              |
 *          "koruna"       "scorched"
 *              |              |
 *             "*"            "*"
 */

struct Level
{
	struct Level *nextpiece; /* pointer to next piece (in this level) */
	struct Level *nextlevel; /* pointer to next level (below this one) */
	char *name;              /* name of piece on this level */

	/*
	 * This is an array of pointers to the corresponding
	 * I/K line structure(s). The reason there may be more than
	 * one, is if the hostnames are the same, but the usernames
	 * differ.
	 * It is only set for the very last piece of the I/K line -
	 * (ie: the very bottom of the tree). Every piece up until
	 * the last piece has a NULL typeptrs.
	 */
	void **typeptrs;
	int numptrs;             /* number of pointers in typeptrs */

	unsigned int flags;
};

/*
 * LV_xxx flags indicate the status of a particular level
 */

#define LV_WILDCARD   (1 << 0) /* level contains a wild host piece */

/*
 * Prototypes
 */

void TreeAddIline(struct Iline *iptr);
void TreeAddKline(struct ServerBan *kptr);
struct Iline *SearchIlineTree(char *username, char *hostname);
struct ServerBan *SearchKlineTree(char *username, char *hostname);

#endif /* INCLUDED_mtree_h */
