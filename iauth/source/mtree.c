/************************************************************************
 *   IRC - Internet Relay Chat, iauth/mtree.c
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

/*
 *  This file contains routines to add and locate items
 * in a "modified" tree. These routines were specifically
 * designed for I: and K: line handling. In short, a
 * rather complicated solution is needed to solve a seemingly
 * trivial problem.
 *
 * The problem is this: Suppose you have 2 I: lines:
 *
 * I:NOMATCH::*@*::1
 * I:NOMATCH::*@*.varner.com::1
 *
 *  Now, if you place the *@* in front of the *@*.varner.com
 * I: line, it could be put in from of *@*.varner.com in
 * the I: line linked list. Now suppose a client connects
 * from koruna.varner.com. If you do a simple linear search
 * of the linked list, you will come across the *@* I: line
 * first, and it happens to be a good match - but it is not
 * the BEST match.
 *  A widely used feature of I: lines is so you can group
 * all the generic users under a common I: line, such as *@*
 * or *@*.com, but make separate I: lines for server operators
 * or administrators (that may conflict with the generic I: lines).
 *
 *  This is where the modified tree comes in. As I: and K: lines
 * are read in, they are added to a regular linked list, and also
 * a type of tree. In this special tree, each node contains
 * part of a hostname - specifically the part between dots.
 *
 *  So, if you make an I: line for *.koruna.varner.com, it would
 * be entered into the tree in the following manner:
 *
 *                       com
 *                        |
 *                      varner
 *                        |
 *                      koruna
 *                        |
 *                        *
 *
 *  This is convenient because when a client connects from
 * *.koruna.varner.com, you can take apart their hostname in
 * the same manner and compare each segment to it's corresponding
 * node in the tree.
 *
 *  Now suppose you wanted to add another I: line for
 * hyland.magenet.com.
 *
 *  The new tree would then look like this:
 *
 *                       com
 *                       /
 *                     /
 *                   /
 *                varner --> magenet
 *                  |           |
 *                koruna      hyland
 *                  |
 *                  *
 *
 *  Notice the TLD is always kept on top. If you wanted to add
 * a new TLD, for example: *.underworld.n?t, the tree
 * would look like this:
 *
 *                       com ---> n?t
 *                      /            \
 *                    /                \
 *                  /                    \
 *               varner -> magenet   underworld
 *                 |          |           |
 *               koruna     hyland        *
 *                 |
 *                 *
 *
 * and so on..
 *  The easy part is setting up the tree. The hard part is
 * making a search algorithm to handle every possibility.
 * I've designed the following search algorithm (SearchSubTree()).
 * When an ircd server requests an authorization, iauth takes
 * the client's hostname (uwns.underworld.net) and breaks it up
 * into segments, keeping pointers to each segment. In this
 * case, the array would look like this:
 *
 *   array[0] = "uwns"
 *   array[1] = "underworld"
 *   array[2] = "net"
 *
 *  Then it begins with the last segment (net) and searches the
 * very top level of the tree. First it searches for an exact
 * match, and if it finds none, it searches for a wildcard
 * match. The reason it takes exact matches over wildcard
 * matches, is because each level is searched only once. If it
 * took the first wildcard match it came to, there might be
 * a better (exact) match later down the line. And again, that
 * is the entire reason a tree is needed - because more exact
 * matches are better than generic matches.
 *  In this case, there is no exact match for the TLD, so it
 * takes the next best thing (n?t). It then hops down a level,
 * and searches for the next lower index of our array (underworld).
 * It will find an exact match right away and jump down to the next
 * (and final) level. It will again find an acceptable wildcard
 * match on the final level, and since there are no more pieces
 * to our hostname, declare that the client has an acceptable I:
 * line.
 *
 *  One more note about exact matches vs. wildcard matches. Although
 * exact matches are given preference over wildcard matches, all
 * wildcard matches are treated equally. This means if you have
 * an I: line for *.n?t and one for *.?nderw?rld.n?t, either could
 * be returned as a valid match, even though you may think one
 * is more specific than the other.
 *  Thus, it is up to the server administrator to make their I:
 * lines as specific as possible if they want to guarantee their
 * clients get assigned to the correct I: line.
 *
 * Patrick Alken <wnder@underworld.net>
 * 09/13/1999
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "conf.h"
#include "log.h"
#include "match.h"
#include "misc.h"
#include "mtree.h"
#include "sock.h"

/*
 * Beginning of our Iline tree
 */
static struct Level          *IlineTree = NULL;

/*
 * Beginning of our Kline tree
 */
static struct Level          *KlineTree = NULL;

/*
 * Local functions
 */

static void CreateSubTree(struct Level **level, void *typeptr,
                          int hostc, char **hostv);
static struct Level *SearchSubTree(struct Level **level, int hostc,
                                   char **hostv);

static void LinkStructPointer(void *ptr, struct Level **list);
static struct Level *IsOnLevel(struct Level *level, char *piece);

static int BreakupHost(char *hostname, char ***array);

/*
TreeAddIline()
 Add an Iline entry to our tree
*/

void
TreeAddIline(struct Iline *iptr)

{
	char **hostv;
	char hostname[HOSTLEN + 1];
	int hostpieces;

	assert(iptr != 0);

	/*
	 * So we don't destroy iptr->hostname, use another buffer
	 */
	strncpy_irc(hostname, iptr->hostname, HOSTLEN);
	hostname[HOSTLEN] = '\0';

	hostpieces = BreakupHost(hostname, &hostv);

	CreateSubTree(&IlineTree, iptr, hostpieces, hostv);

	MyFree(hostv);
} /* TreeAddIline() */

/*
TreeAddKline()
 Add a Kline entry to our tree
*/

void
TreeAddKline(struct ServerBan *kptr)

{
	char **hostv;
	char hostname[HOSTLEN + 1];
	int hostpieces;

	assert(kptr != 0);

	/*
	 * So we don't destroy kptr->hostname, use another buffer
	 */
	strncpy_irc(hostname, kptr->hostname, HOSTLEN);
	hostname[HOSTLEN] = '\0';

	hostpieces = BreakupHost(hostname, &hostv);

	CreateSubTree(&KlineTree, kptr, hostpieces, hostv);

	MyFree(hostv);
} /* TreeAddKline() */

/*
CreateSubTree()
 Create a sub-tree within 'level' containing the given information.

Inputs: level   - where to start building our tree
        typeptr - Iline or Kline structure that we're adding to
                  the tree
        hostc   - number of hostname pieces we're adding
        hostv   - array of pointers to each host piece
*/

static void
CreateSubTree(struct Level **level, void *typeptr, int hostc, char **hostv)

{
	struct Level *tmplev;
	char *ch;

	if (hostc == 0)
		return;

	/*
	 * First search the current level for an exact match
	 * of hostv[hostc - 1]. - if found proceed to the next
	 * level down from there.
	 * We can't use IsOnLevel() here because we need an
	 * exact match (strcasecmp) - we don't want stuff like
	 * "c?m" and "com" being put in the same node.
	 */
	for (tmplev = *level; tmplev; tmplev = tmplev->nextpiece)
	{
		if (!strcasecmp(tmplev->name, hostv[hostc - 1]))
		{
			/*
			 * We have found a matching host piece on this
			 * level - no need to allocate a new level
			 * structure. Now we will recursively call
			 * CreateSubTree() again using tmplev->nextlevel
			 * as the level pointer, indicating that we want
			 * to see if the next lower index of hostv
			 * (hostv[hostc - 2]) is in the next level. If so,
			 * again recursively call CreateSubTree(). Eventually
			 * we might reach a level that does not contain
			 * the corresponding index of hostv[]. When that
			 * happens, the loop will fail and we will
			 * drop below to allocate a new level structure.
			 * If that does not happen, we have an exact duplicate
			 * of a previous I/K line.
			 */

			if (hostc == 1)
				LinkStructPointer(typeptr, &tmplev);

			CreateSubTree(&tmplev->nextlevel, typeptr, hostc - 1, hostv);

			return;
		}
	}

	/*
	 * If we reach this point, one of two conditions must
	 * be true.
	 *  1) *level is NULL, which means we must initialize it
	 *     and then add our hostv[] index to it.
	 *  2) The host piece hostv[hostc - 1] was not found
	 *     on this level - allocate a new structure for it.
	 */

	tmplev = (struct Level *) MyMalloc(sizeof(struct Level));
	memset(tmplev, 0, sizeof(struct Level));
	tmplev->name = MyStrdup(hostv[hostc - 1]);

	for (ch = tmplev->name; *ch; ++ch)
	{
		if (IsWild(*ch))
		{
			tmplev->flags |= LV_WILDCARD;
			break;
		}
	}

	if (hostc == 1)
	{
		/*
		 * Since hostc is 1, this is the very last hostname piece
		 * we need to add to the sub tree. This is the piece that
		 * will contain a pointer to the corresponding Iline or
		 * Kline structure, so SearchSubTree() will know when to
		 * stop.
		 * Now, it is quite possible that later on we will need to
		 * add more host pieces past this current piece. For example,
		 * suppose our tree looks like this after this call:
		 *
		 *     com
		 *      |
		 *    varner -> [struct Iline *iptr (for @varner.com)]
		 *
		 * Then, suppose later we wish to add an Iline for
		 * @koruna.varner.com. Our tree should then look like this:
		 *
		 *     com
		 *      |
		 *    varner -> [struct Iline (for @varner.com)]
		 *      |
		 *    koruna -> [struct Iline (for @koruna.varner.com)]
		 *
		 * SearchSubTree() will then know that both levels are a
		 * complete I/K line, and depending on how big the hostname
		 * it is looking for, will know how deep to go.
		 */

		LinkStructPointer(typeptr, &tmplev);
	}

	if (*level == NULL)
	{
		/*
		 * Set the level to our newly allocated structure
		 */
		*level = tmplev;
	}
	else
	{
		/*
		 * The level already exists, and possibly has some
		 * host pieces on it - add our new piece after
		 * *level. For example, if the level originally looked
		 * like:
		 *
		 *  ...
		 *   |
		 * "com" --> "net" --> "org" --> NULL
		 *
		 * It will now look like:
		 *
		 *  ...
		 *   |
		 * "com" --> tmplev->name --> "net" --> "org" --> NULL
		 */
		tmplev->nextpiece = (*level)->nextpiece;
		(*level)->nextpiece = tmplev;
	}

	/*
	 * We've just added hostv[hostc - 1] to the correct level,
	 * but as long as hostc != 0, there are more host pieces
	 * to add. Recursively call CreateSubTree() until there
	 * are no more pieces to add.
	 */
	CreateSubTree(&tmplev->nextlevel, typeptr, hostc - 1, hostv);
} /* CreateSubTree() */

/*
SearchIlineTree()
 Search the Iline tree for the given hostname and username.
Return a pointer to the Iline structure if found.
*/

struct Iline *
SearchIlineTree(char *username, char *hostname)

{
	char host[HOSTLEN + 1];
	char **hostv;
	int hostc,
	    ii;
	struct Level *ret;
	struct Iline *tmp;

	strncpy_irc(host, hostname, HOSTLEN);
	host[HOSTLEN] = '\0';

	hostc = BreakupHost(host, &hostv);

	ret = SearchSubTree(&IlineTree, hostc, hostv);

	MyFree(hostv);

	if (ret)
	{
		/*
		 * Now for the username check
		 */
		for (ii = 0; ii < ret->numptrs; ++ii)
		{
			tmp = (struct Iline *) ret->typeptrs[ii];
			if (match(tmp->username, username))
				return (tmp);
		}
	}

	return (NULL);
} /* SearchIlineTree() */

/*
SearchKlineTree()
 Search the Kline tree for the given hostname.
Return a pointer to the Kline structure if found.
*/

struct ServerBan *
SearchKlineTree(char *username, char *hostname)

{
	char host[HOSTLEN + 1];
	char **hostv;
	int hostc,
	    ii;
	struct Level *ret;
	struct ServerBan *tmp;

	strncpy_irc(host, hostname, HOSTLEN);
	host[HOSTLEN] = '\0';

	hostc = BreakupHost(host, &hostv);

	ret = SearchSubTree(&KlineTree, hostc, hostv);

	MyFree(hostv);

	if (ret)
	{
		/*
		 * Check the username
		 */
		for (ii = 0; ii < ret->numptrs; ++ii)
		{
			tmp = (struct ServerBan *) ret->typeptrs[ii];
			if (match(tmp->username, username))
				return (tmp);
		}
	}

	return (NULL);
} /* SearchKlineTree() */

/*
SearchSubTree()
 Backend for Search*lineTree() - Recurse through the levels of
'level' looking for the best match with the given hostname
pieces.
 Return a pointer to the level containing the I/K line structure
if found.
*/

static struct Level *
SearchSubTree(struct Level **level, int hostc, char **hostv)

{
	struct Level *tmplev,
	             *tmplev2;

	if (hostc == 0)
		return (NULL);

	/*
	 * Search the current level for hostv[hostc - 1]
	 */
	if (!(tmplev = IsOnLevel(*level, hostv[hostc - 1])))
		return (NULL);

	if (hostc == 1)
	{
		/*
		 * Since hostc is 1, there are no more pieces to
		 * search for - we have found a match
		 */
		if (!tmplev->typeptrs)
		{
			log(L_ERROR,
				"SearchSubTree(): hostc is 1, but the corresponding level does not have a typeptr");
		}

		return (tmplev);
	}

	/*
	 * We found a match on a particular host piece, but
	 * there are still more pieces to check - continue
	 * searching
	 */

	if (tmplev->flags & LV_WILDCARD)
	{
		/*
		 * We want to try to take this wildcard piece as
		 * far as we can before moving on to the next level.
		 * In other words, move to the next level only when:
		 *  a) we find a match on the next level for the
		 *     corresponding host piece
		 *  b) the wildcard on this level fails to match
		 *     the next host piece
		 */

		if ((tmplev2 = IsOnLevel(tmplev->nextlevel, hostv[hostc - 2])))
		{
			/*
			 * Since the next index of hostv[] was found
			 * on the level below tmplev, give up on the
			 * current wildcard (tmplev->name), and go
			 * down to the next level to continue the search.
			 */
			tmplev = tmplev2;
			--hostc;
		}

		while (hostc)
		{
			if (!match(tmplev->name, hostv[hostc - 2]))
				break;
			if (--hostc == 1)
			{
				if (!tmplev->typeptrs)
				{
					log(L_ERROR,
						"SearchSubTree(): typeptrs for piece [%s] is NULL which it should not be",
						tmplev->name);
				}

				return (tmplev);
			}

			/*
			 * Since we just decremented hostc, we need to check
			 * if the corresponding index of hostv[] is on the
			 * next level - if so, drop to the next level to
			 * continue checking.
			 * For example, suppose the tree looked like this:
			 *
			 *   *
			 *   |
			 *  irc
			 *
			 * And we're checking the host: irc.wnder.com.
			 * "com" and "wnder" should both be matched against
			 * the "*", but once we see that "irc" matches
			 * something on the next level, drop down and continue
			 * searching - we wouldn't want to match "irc" against
			 * "*" (even though its a good match) because although
			 * we're at the end of our hostv[] array, the "*"
			 * structure will NOT contain a pointer to the
			 * corresponding I/K line structure - only the very
			 * last branch of the tree does that.
			 */
			if (IsOnLevel(tmplev->nextlevel, hostv[hostc - 2]))
				break;
		} /* while (hostc) */
	} /* if (tmplev->flags & LV_WILDCARD) */

	/*
	 * Continue our search on the next level
	 */
	return (SearchSubTree(&tmplev->nextlevel, hostc - 1, hostv));

#if 0
	for (tmplev = *level; tmplev; tmplev = tmplev->nextpiece)
	{
		if (match(tmplev->name, hostv[hostc - 1]))
		{
			if (hostc == 1)
			{
				/*
				 * Since hostc is 1, there are no more pieces to
				 * search for - we have found a match
				 */
				if (!tmplev->typeptr)
				{
					log(L_ERROR,
						"SearchSubTree(): hostc is 1, but the corresponding level does not have a typeptr");
				}

				return (tmplev->typeptr);
			}

			/*
			 * We found a match on a particular host piece, but
			 * there are still more pieces to check - continue
			 * searching
			 */
			
			if (tmplev->flags & LV_WILDCARD)
			{
				/*
				 * We want to try to take this wildcard piece as
				 * far as we can before moving on to the next level.
				 * In other words, move to the next level only when:
				 *  a) we find a match on the next level for the
				 *     corresponding host piece
				 *  b) the wildcard on this level fails to match
				 *     the next host piece
				 */

				if ((tmplev2 = IsOnLevel(tmplev->nextlevel, hostv[hostc - 2])))
				{
					/*
					 * Since the next index of hostv[] was found
					 * on the level below tmplev, give up on the
					 * current wildcard (tmplev->name), and go
					 * down to the next level to continue the search.
					 */
					tmplev = tmplev2;
					--hostc;
				}

				while (hostc)
				{
					if (!match(tmplev->name, hostv[hostc - 2]))
						break;
					if (--hostc == 1)
					{
						if (!tmplev->typeptr)
						{
							log(L_ERROR,
								"SearchSubTree(): typeptr for piece [%s] is NULL which it should not be",
								tmplev->name);
						}

						return (tmplev->typeptr);
					}

					/*
					 * Since we just decremented hostc, we need to check
					 * if the corresponding index of hostv[] is on the
					 * next level - if so, drop to the next level to
					 * continue checking.
					 * For example, suppose the tree looked like this:
					 *
					 *   *
					 *   |
					 *  irc
					 *
					 * And we're checking the host: irc.wnder.com.
					 * "com" and "wnder" should both be matched against
					 * the "*", but once we see that "irc" matches
					 * something on the next level, drop down and continue
					 * searching - we wouldn't want to match "irc" against
					 * "*" (even though its a good match) because although
					 * we're at the end of our hostv[] array, the "*"
					 * structure will NOT contain a pointer to the
					 * corresponding I/K line structure - only the very
					 * last branch of the tree does that.
					 */
					if (IsOnLevel(tmplev->nextlevel, hostv[hostc - 2]))
						break;
				} /* while (hostc) */
			}

			return (SearchSubTree(&tmplev->nextlevel, hostc - 1, hostv));
		}
	}
#endif /* 0 */

	/*
	 * If we get here, we've hit a level that does not contain
	 * the corresponding host piece in our list - the hostname
	 * is not in our tree.
	 */
	return (NULL);
} /* SearchSubTree() */

/*
LinkStructPointer()
 Link I:/K: line structure to list->typeptrs
*/

static void
LinkStructPointer(void *ptr, struct Level **list)

{
	++(*list)->numptrs;

	if (!(*list)->typeptrs)
		(*list)->typeptrs = (void **) MyMalloc(sizeof(void *));
	else
		(*list)->typeptrs = (void **) MyRealloc((*list)->typeptrs, sizeof(void *) * (*list)->numptrs);

	(*list)->typeptrs[(*list)->numptrs - 1] = ptr;
} /* LinkStructPointer() */

/*
IsOnLevel()
 Determine if 'piece' is on the sub-level 'level'.
Return a pointer to the level struct containing 'piece'
*/

static struct Level *
IsOnLevel(struct Level *level, char *piece)

{
	struct Level *tmplev;

	/*
	 * First try to do strcasecmp's because exact matches
	 * should be taken over wildcard matches
	 */
	for (tmplev = level; tmplev; tmplev = tmplev->nextpiece)
	{
		if (!strcasecmp(tmplev->name, piece))
			return (tmplev);
	}

	for (tmplev = level; tmplev; tmplev = tmplev->nextpiece)
	{
		if (match(tmplev->name, piece))
			return (tmplev);
	}

	return (NULL);
} /* IsOnLevel() */

/*
BreakupHost()
 Break up host pieces (separated by dots ".") and store pointers
to each piece in 'array'.

Return: number of host pieces in 'hostname'

NOTE: Memory is allocated for array - so free it when you're done.
*/

static int
BreakupHost(char *hostname, char ***array)

{
	int argnum = 4; /* initial number of slots in our array */
	int pieces;     /* number of "pieces" in hostname */
	char *tmp,
	     *founddot;

	*array = (char **) MyMalloc(sizeof(char *) * argnum);
	pieces = 0;

	tmp = hostname;
	while (*tmp)
	{
		if (pieces == argnum)
		{
			/*
			 * We've filled up all the slots allocated so far,
			 * allocate some more
			 */
			argnum += 4;
			*array = (char **) MyRealloc(*array, sizeof(char *) * argnum);
		}

		founddot = strstr(tmp, ".");
		if (founddot)
			*founddot++ = '\0';
		else
			founddot = tmp + strlen(tmp);

		(*array)[pieces++] = tmp;
		tmp = founddot;
	}

	return (pieces);
} /* BreakupHost() */
