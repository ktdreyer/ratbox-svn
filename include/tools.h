/* $Id$ */
#ifndef INCLUDED_tools_h
#define INCLUDED_tools_h

#define EmptyString(x) (!(x) || (*(x) == '\0'))

extern char *getfield(char *newline);

#define my_malloc(x) (my_calloc(1, x))
extern void *my_calloc(int, size_t);
extern void my_free(void *);
extern char *my_strdup(const char *s);

#ifndef HAVE_STRLCAT
extern size_t strlcat(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_STRLCPY
extern size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

extern const unsigned char ToLowerTab[];
#define ToLower(c) (ToLowerTab[(unsigned char)(c)])
extern const unsigned char ToUpperTab[];
#define ToUpper(c) (ToUpperTab[(unsigned char)(c)])

extern const unsigned int CharAttrs[];

#define PRINT_C   0x001
#define CNTRL_C   0x002
#define ALPHA_C   0x004
#define LETTER_C  0x008
#define DIGIT_C   0x010
#define SPACE_C   0x020
#define NICK_C    0x040
#define SERV_C	  0x080
#define CHAN_C    0x100
#define CHANPFX_C 0x200
#define NONEOS_C 0x1000
#define EOL_C    0x4000

#define IsPrint(c) (CharAttrs[(unsigned char)(c)] & PRINT_C)
#define IsCntrl(c)      (CharAttrs[(unsigned char)(c)] & CNTRL_C)
#define IsAlpha(c)      (CharAttrs[(unsigned char)(c)] & ALPHA_C)
#define IsLetter(c)     (CharAttrs[(unsigned char)(c)] & LETTER_C)
#define IsDigit(c)      (CharAttrs[(unsigned char)(c)] & DIGIT_C)
#define IsAlNum(c) (CharAttrs[(unsigned char)(c)] & (DIGIT_C | ALPHA_C))
#define IsSpace(c)      (CharAttrs[(unsigned char)(c)] & SPACE_C)
#define IsNickChar(c)   (CharAttrs[(unsigned char)(c)] & NICK_C)
#define IsServChar(c)   (CharAttrs[(unsigned char)(c)] & (NICK_C | SERV_C))
#define IsChanChar(c)   (CharAttrs[(unsigned char)(c)] & CHAN_C)
#define IsChanPrefix(c) (CharAttrs[(unsigned char)(c)] & CHANPFX_C)
#define IsNonEOS(c) (CharAttrs[(unsigned char)(c)] & NONEOS_C)
#define IsEol(c) (CharAttrs[(unsigned char)(c)] & EOL_C)

extern int match(const char *mask, const char *name);
extern int irccmp(const char *s1, const char *s2);

typedef struct _dlink_node dlink_node;
typedef struct _dlink_list dlink_list;

struct _dlink_node
{
	void *data;
	dlink_node *prev;
	dlink_node *next;

};

struct _dlink_list
{
	dlink_node *head;
	dlink_node *tail;
	unsigned long length;
};

extern dlink_node *make_dlink_node(void);
extern void free_dlink_node(dlink_node *lp);

extern void dlink_move_node(dlink_node * m, dlink_list * oldlist, dlink_list * newlist);
extern void dlink_add(void *data, dlink_node * m, dlink_list * list);
extern void dlink_add_tail(void *data, dlink_node * m, dlink_list * list);
extern void dlink_delete(dlink_node * m, dlink_list * list);
extern void dlink_move_list(dlink_list * from, dlink_list * to);

extern dlink_node *dlink_find(dlink_list * m, void *data);
extern dlink_node *dlink_find_delete(dlink_list *, void *);
extern int dlink_find_destroy(dlink_list *, void *data);

#define DLINK_FOREACH(pos, head) for (pos = (head); pos != NULL; pos = pos->next)
#define DLINK_FOREACH_SAFE(pos, n, head) for (pos = (head), n = pos ? pos->next : NULL; pos != NULL; pos = n, n = pos ? pos->next : NULL)
#define DLINK_FOREACH_PREV(pos, head) for (pos = (head); pos != NULL; pos = pos->prev)

/* Returns the list length */
#define dlink_list_length(list) (list)->length

#define dlink_add_alloc(data, list) dlink_add(data, make_dlink_node(), list)
#define dlink_add_tail_alloc(data, list) dlink_add_tail(data, make_dlink_node(), list)
#define dlink_destroy(node, list) do { dlink_delete(node, list); my_free(node); } while(0)

#endif
