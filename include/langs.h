#ifndef INCLUDED_langs_h
#define INCLUDED_langs_h

struct lconn;
struct client;

/* DO NOT CHANGE THIS.
 *
 * This macro is only used to define a "fallback" language that is
 * guaranteed to be complete.  It will only be used when a translated
 * version of the helpfile we are looking for is not around.
 *
 * There is a conf option to set a default language, use that instead.
 */
#define LANG_DEFAULT LANG_en_GB

enum langs_enum
{
	LANG_en_GB,

	/* THIS ENTRY MUST BE LAST */
	LANG_LAST
} langs_id;

extern const char *langs_available[];

struct cachefile *lang_get_cachefile(struct cachefile **, struct client *);
struct cachefile *lang_get_cachefile_u(struct cachefile **, struct lconn *);

#endif
