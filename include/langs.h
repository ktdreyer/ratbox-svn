#ifndef INCLUDED_langs_h
#define INCLUDED_langs_h

#define LANG_DEFAULT LANG_en_GB

enum langs_enum
{
	LANG_en_GB,

	/* THIS ENTRY MUST BE LAST */
	LANG_LAST
} langs_id;

extern const char **langs_available;

#endif
