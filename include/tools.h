/* $Id$ */
#ifndef INCLUDED_tools_h
#define INCLUDED_tools_h

#include "setup.h"

#define EmptyString(x) ((x == NULL) || (*(x) == '\0'))
#define MAXPARA	15


extern const char *get_duration(time_t seconds);
extern const char *get_short_duration(time_t seconds);
extern const char *get_time(time_t when, int show_tz);

time_t get_temp_time(const char *duration);

extern const char *lcase(const char *);
extern const char *ucase(const char *);

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
#define BAN_C	  0x400
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
#define IsBanChar(c)	(CharAttrs[(unsigned char)(c)] & BAN_C)
#define IsNonEOS(c) (CharAttrs[(unsigned char)(c)] & NONEOS_C)
#define IsEol(c) (CharAttrs[(unsigned char)(c)] & EOL_C)

extern int match(const char *mask, const char *name);
extern int irccmp(const char *s1, const char *s2);
extern int ircncmp(const char *s1, const char *s2, int n);

void collapse(char *);
extern char *strip_tabs(char *dest, const unsigned char *src, size_t len);

#define DLINK_EMPTY { NULL, NULL, 0 }


#define HASH_WALK(i, max, ptr, table) for (i = 0; i < max; i++) { RB_DLINK_FOREACH(ptr, table[i].head)
#define HASH_WALK_SAFE(i, max, ptr, nptr, table) for (i = 0; i < max; i++) { RB_DLINK_FOREACH_SAFE(ptr, nptr, table[i].head)
#define HASH_WALK_END }

#define HASH_WALK_SAFE_POS(i, start, max, max_bound, ptr, nptr, table) for (i = start; i < start+max && i < max_bound; i++) { RB_DLINK_FOREACH_SAFE(ptr, nptr, table[i].head)
#define HASH_WALK_SAFE_POS_END(i, start, max_bound) 		\
				}				\
				do				\
				if(i >= max_bound) {		\
					start = 0;		\
				} else {			\
					start = i;		\
				}				\
				while(0)

#ifndef HARD_ASSERT
#ifdef __GNUC__
#define s_assert(expr)	do						\
	if(!(expr)) {							\
		mlog("file: %s line: %d (%s): Assertion failed: (%s)",	\
			__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
		sendto_all("file: %s line: %d (%s): Assertion failed: (%s)",\
			__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
	}								\
	while(0)
#else
#define s_assert(expr)	do						\
	if(!(expr)) {							\
		mlog("file: %s line: %d: Assertion failed: (%s)",	\
			__FILE__, __LINE__, #expr);                     \
		sendto_all("file: %s line: %d: Assertion failed: (%s)",\
			__FILE__, __LINE__, #expr);                     \
	}								\
	while(0)
#endif
#else
#define s_assert(expr)	assert(expr)
#endif

#endif
