#ifndef INCLUDED_langs_h
#define INCLUDED_langs_h

#define LANG_DEFAULT LANG_en_GB

enum
{
	LANG_en_GB,

	/* THIS ENTRY MUST BE LAST */
	LANG_LAST
} langs_id;

const char *langs_available[] =
{
	"en_GB",
	"\0"
};

#endif
