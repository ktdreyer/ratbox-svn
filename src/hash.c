/************************************************************************
 *   IRC - Internet Relay Chat, src/hash.c
 *   Copyright (C) 1991 Darren Reed
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
 *  $Id$
 */
#include "hash.h"
#include "channel.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"

#include <assert.h>
#include <string.h>

/* 
 * New hash code
 * Contributed by James L. Davis
 * Hacked by Dianora and Bleep
 */

/*
 * Hashing.
 *
 *   The server uses a chained hash table to provide quick and efficient
 * hash table mantainence (providing the hash function works evenly over
 * the input range).  The hash table is thus not susceptible to problems
 * of filling all the buckets or the need to rehash.
 *    It is expected that the hash table would look somehting like this
 * during use:
 *                   +-----+    +-----+    +-----+   +-----+
 *                ---| 224 |----| 225 |----| 226 |---| 227 |---
 *                   +-----+    +-----+    +-----+   +-----+
 *                      |          |          |
 *                   +-----+    +-----+    +-----+
 *                   |  A  |    |  C  |    |  D  |
 *                   +-----+    +-----+    +-----+
 *                      |
 *                   +-----+
 *                   |  B  |
 *                   +-----+
 *
 * A - GOPbot, B - chang, C - hanuaway, D - *.mu.OZ.AU
 *
 */

static struct HashEntry clientTable[U_MAX];
static struct HashEntry channelTable[CH_MAX];


struct HashEntry hash_get_channel_block(int i)
{
  return channelTable[i];
}

size_t hash_get_channel_table_size(void)
{
  return sizeof(struct HashEntry) * CH_MAX;
}

size_t hash_get_client_table_size(void)
{
  return sizeof(struct HashEntry) * U_MAX;
}

unsigned int hash_nick_name(const char* name)
{
  unsigned int h = 0;

  while (*name)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*name++));
    }

  return(h & (U_MAX - 1));
}

/*
 * hash_channel_name
 *
 * calculate a hash value on at most the first 30 characters of the channel
 * name. Most names are short than this or dissimilar in this range. There
 * is little or no point hashing on a full channel name which maybe 255 chars
 * long.
 */
unsigned int hash_channel_name(const char* name)
{
  register int i = 30;
  unsigned int h = 0;

  while (*name && --i)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*name++));
    }

  return (h & (CH_MAX - 1));
}

/*
 * clear_client_hash_table
 *
 * Nullify the hashtable and its contents so it is completely empty.
 */
static void clear_client_hash_table()
{
  memset(clientTable, 0, sizeof(struct HashEntry) * U_MAX);
}

static void clear_channel_hash_table()
{
  memset(channelTable, 0, sizeof(struct HashEntry) * CH_MAX);
}

void init_hash(void)
{
  clear_client_hash_table();
  clear_channel_hash_table();
}

/*
 * add_to_client_hash_table
 */
void add_to_client_hash_table(const char* name, struct Client* cptr)
{
  unsigned int hashv;
  assert(0 != name);
  assert(0 != cptr);

  hashv = hash_nick_name(name);
  cptr->hnext = (struct Client*) clientTable[hashv].list;
  clientTable[hashv].list = (void*) cptr;
  ++clientTable[hashv].links;
}

/*
 * add_to_channel_hash_table
 */
void add_to_channel_hash_table(const char* name, struct Channel* chptr)
{
  unsigned int hashv;
  assert(0 != name);
  assert(0 != chptr);

  hashv = hash_channel_name(name);
  chptr->hnextch = (struct Channel*) channelTable[hashv].list;
  channelTable[hashv].list = (void*) chptr;
  ++channelTable[hashv].links;
}

/*
 * del_from_client_hash_table - remove a client/server from the client
 * hash table
 */
void del_from_client_hash_table(const char* name, struct Client* cptr)
{
  struct Client* tmp;
  struct Client* prev = NULL;
  unsigned int   hashv;
  assert(0 != name);
  assert(0 != cptr);

  hashv = hash_nick_name(name);
  tmp = (struct Client*) clientTable[hashv].list;

  for ( ; tmp; tmp = tmp->hnext)
    {
      if (tmp == cptr)
        {
          if (prev)
            prev->hnext = tmp->hnext;
          else
            clientTable[hashv].list = (void*) tmp->hnext;
          tmp->hnext = NULL;

          assert(clientTable[hashv].links > 0);
          if (clientTable[hashv].links > 0)
            --clientTable[hashv].links;
          return;
        }
      prev = tmp;
    }
}

/*
 * del_from_channel_hash_table
 */
void del_from_channel_hash_table(const char* name, struct Channel* chptr)
{
  struct Channel* tmp;
  struct Channel* prev = NULL;
  unsigned int    hashv;
  assert(0 != name);
  assert(0 != chptr);

  hashv = hash_channel_name(name);
  tmp = (struct Channel*) channelTable[hashv].list;

  for ( ; tmp; tmp = tmp->hnextch)
    {
      if (tmp == chptr)
        {
          if (prev)
            prev->hnextch = tmp->hnextch;
          else
            channelTable[hashv].list = (void*) tmp->hnextch;
          tmp->hnextch = NULL;

          assert(channelTable[hashv].links > 0);
          if (channelTable[hashv].links > 0)
            --channelTable[hashv].links;
          return;
        }
      prev = tmp;
    }
}


/*
 * hash_find_client
 */
struct Client* hash_find_client(const char* name, struct Client* cptr)
{
  struct Client* tmp;
  unsigned int   hashv;
  assert(0 != name);

  hashv = hash_nick_name(name);
  tmp = (struct Client*) clientTable[hashv].list;
  /*
   * Got the bucket, now search the chain.
   */
  for ( ; tmp; tmp = tmp->hnext) {
    if (irccmp(name, tmp->name) == 0)
      return tmp;
  }
  return cptr;
}

/*
 * Whats happening in this next loop ? Well, it takes a name like
 * foo.bar.edu and proceeds to earch for *.edu and then *.bar.edu.
 * This is for checking full server names against masks although
 * it isnt often done this way in lieu of using matches().
 *
 * Rewrote to do *.bar.edu first, which is the most likely case,
 * also made const correct
 * --Bleep
 */
static struct Client* hash_find_masked_server(const char* name)
{
  char           buf[HOSTLEN + 1];
  char*          p = buf;
  char*          s;
  struct Client* server;

  if ('*' == *name || '.' == *name)
    return 0;

  /*
   * copy the damn thing and be done with it
   */
  strncpy_irc(buf, name, HOSTLEN);
  buf[HOSTLEN] = '\0';

  while ((s = strchr(p, '.')) != 0)
    {
       *--s = '*';
      /*
       * Dont need to check IsServer() here since nicknames cant
       * have *'s in them anyway.
       */
      if ((server = hash_find_client(s, 0)))
        return server;
      p = s + 2;
    }
  return 0;
}

/*
 * hash_find_server
 */
struct Client* hash_find_server(const char* name)
{
  struct Client* tmp;
  unsigned int   hashv;

  assert(0 != name);
  hashv = hash_nick_name(name);
  tmp = (struct Client*) clientTable[hashv].list;

  for ( ; tmp; tmp = tmp->hnext)
    {
      if (!IsServer(tmp) && !IsMe(tmp))
        continue;
      if (irccmp(name, tmp->name) == 0)
          return tmp;
    }
  return hash_find_masked_server(name);
}

/*
 * find_channel
 */
struct Channel* hash_find_channel(const char* name, struct Channel* chptr)
{
  struct Channel*    tmp;
  unsigned int hashv;
  
  assert(0 != name);
  hashv = hash_channel_name(name);
  tmp = (struct Channel*) channelTable[hashv].list;

  for ( ; tmp; tmp = tmp->hnextch) {
    if (irccmp(name, tmp->chname) == 0)
      return tmp;
  }
  return chptr;
}

void hash_rebuild_client()
{
  struct Client* client;

  clear_client_hash_table();
  for (client = GlobalClientList; client; client = client->next)
    add_to_client_hash_table(client->name, client);
}

void hash_rebuild_channel()
{
  struct Channel* channel;

  clear_channel_hash_table();
  for (channel = GlobalChannelList; channel; channel = channel->nextch)
    add_to_channel_hash_table(channel->chname, channel);
}

static void hash_get_stats(struct HashEntry* table, int size, 
                           struct HashStats* stats)
{
  int links;
  int i;
  assert(0 != table);
  assert(0 != stats);

  memset(stats, 0, sizeof(struct HashStats));

  stats->table_size = size;

  for (i = 0; i < size; i++) {
    if (0 < (links = table[i].links)) {
      ++stats->buckets_used;
      stats->entries += links;
      if (links > stats->longest_chain)
        stats->longest_chain = links;

      if (--links < 9)  /* index 0 is number of entries with 1 link */
        ++stats->link_counts[links];
      else
        ++stats->link_counts[9];
    }
  }
}

void hash_get_channel_stats(struct HashStats* stats)
{
  hash_get_stats(channelTable, CH_MAX, stats);
}

void hash_get_client_stats(struct HashStats* stats)
{
  hash_get_stats(clientTable, U_MAX, stats);
}

