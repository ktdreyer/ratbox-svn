/* $Id$ */
#ifndef INCLUDED_filecache_h
#define INCLUDED_filecache_h

#define CACHELINELEN	81
#define CACHEFILELEN	30

struct lconn;

struct rb_bh *cacheline_heap;

struct cachefile
{
	char name[CACHEFILELEN];
	rb_dlink_list contents;
};

struct cacheline
{
	char data[CACHELINELEN];
	rb_dlink_node linenode;
};

extern struct cacheline *emptyline;

extern void init_cache(void);
extern struct cachefile *cache_file(const char *, const char *, int add_blank);
extern void free_cachefile(struct cachefile *);

extern void send_cachefile(struct cachefile *, struct lconn *);

#endif

