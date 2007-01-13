#include "stdinc.h"
#include "rserv.h"
#include "langs.h"
#include "cache.h"
#include "client.h"
#include "io.h"
#include "conf.h"
#ifdef ENABLE_USERSERV
#include "s_userserv.h"
#endif

const char *langs_available[] =
{
	"en_GB",
	"\0"
};

struct cachefile *
lang_get_cachefile(struct cachefile **translations, struct client *client_p)
{
#ifdef ENABLE_USERSERV
	if(client_p != NULL && client_p->user != NULL && client_p->user->user_reg != NULL)
	{
		enum langs_enum language = client_p->user->user_reg->language;

		if(language < LANG_LAST && translations[language] != NULL)
			return translations[language];
	}
#endif

	if(translations[config_file.default_language] != NULL)
		return translations[config_file.default_language];

	return translations[LANG_DEFAULT];
}

struct cachefile *
lang_get_cachefile_u(struct cachefile **translations, struct lconn *conn_p)
{
	return translations[LANG_DEFAULT];
}
