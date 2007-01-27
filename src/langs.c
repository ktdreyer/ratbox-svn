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

const char **svc_notice;

const char *svc_notice_string[] =
{
	/* general service */
	"SVC_SUCCESSFUL",
	"SVC_SUCCESSFULON",
	"SVC_ISSUED",
	"SVC_NEEDMOREPARAMS",
	"SVC_ISDISABLED",
	"SVC_ISDISABLEDEMAIL",
	"SVC_NOACCESS",
	"SVC_OPTIONINVALID",
	"SVC_RATELIMITED",
	"SVC_RATELIMITEDHOST",

	/* general irc related */
	"SVC_IRC_NOSUCHCHANNEL",
	"SVC_IRC_CHANNELINVALID",
	"SVC_IRC_CHANNELNOUSERS",
	"SVC_IRC_NOSUCHSERVER",

	/* email */
	"SVC_EMAIL_INVALID",
	"SVC_EMAIL_INVALIDIGNORED",
	"SVC_EMAIL_BANNEDDOMAIN",
	"SVC_EMAIL_TEMPUNAVAILABLE",
	"SVC_EMAIL_SENDFAILED",

	/* userserv */
	"SVC_USER_USERLOGGEDIN",
	"SVC_USER_REGISTERDISABLED",
	"SVC_USER_ALREADYREG",
	"SVC_USER_NOTREG",
	"SVC_USER_NOWREG",
	"SVC_USER_NOWREGLOGGEDIN",
	"SVC_USER_NOWREGEMAILED",
	"SVC_USER_REGDROPPED",
	"SVC_USER_INVALIDUSERNAME",
	"SVC_USER_INVALIDPASSWORD",
	"SVC_USER_INVALIDLANGUAGE",
	"SVC_USER_LONGPASSWORD",
	"SVC_USER_LOGINSUSPENDED",
	"SVC_USER_LOGINUNACTIVATED",
	"SVC_USER_LOGINMAX",
	"SVC_USER_ALREADYLOGGEDIN",
	"SVC_USER_NICKNOTLOGGEDIN",
	"SVC_USER_NOEMAIL",
	"SVC_USER_CHANGEDPASSWORD",
	"SVC_USER_CHANGEDOPTION",
	"SVC_USER_QUERYOPTION",
	"SVC_USER_QUERYOPTIONALREADY",
	"SVC_USER_REQUESTISSUED",
	"SVC_USER_REQUESTPENDING",
	"SVC_USER_REQUESTNONE",
	"SVC_USER_TOKENBAD",
	"SVC_USER_TOKENMISMATCH",
	"SVC_USER_DURATIONTOOSHORT",
	/* userserv::activate */
	"SVC_USER_ACT_ALREADY",
	"SVC_USER_ACT_COMPLETE",
	/* userserv::resetpass */
	"SVC_USER_RP_LOGGEDIN",
	/* userserv::userlist */
	"SVC_USER_UL_START",
	"SVC_USER_UL_END",
	"SVC_USER_UL_ENDLIMIT",

	/* userserv::info */
	/* chanserv::info */
	"SVC_INFO_REGDURATIONUSER",
	"SVC_INFO_REGDURATIONCHAN",
	"SVC_INFO_SUSPENDED",
	"SVC_INFO_SUSPENDEDADMIN",
	"SVC_INFO_ACCESSLIST",
	"SVC_INFO_NICKNAMES",
	"SVC_INFO_EMAIL",
	"SVC_INFO_URL",
	"SVC_INFO_TOPIC",
	"SVC_INFO_SETTINGS",
	"SVC_INFO_ENFORCEDMODES",
	"SVC_INFO_CURRENTLOGON",

	/* operserv */
	"SVC_OPER_CONNECTIONSSTART",
	"SVC_OPER_CONNECTIONSEND",
	"SVC_OPER_SERVERNAMEMISMATCH",

	/* banserv */
	"SVC_BAN_ISSUED",
	"SVC_BAN_ALREADYPLACED",
	"SVC_BAN_NOTPLACED",
	"SVC_BAN_INVALID",
	"SVC_BAN_LISTSTART",
	"SVC_BAN_LISTEND",
	"SVC_BAN_NOPERMACCESS",

	/* must be last */
	"\0"
};

void
init_langs(void)
{
	int i;

	svc_notice = my_malloc(sizeof(char *) * SVC_LAST);

	for(i = 0; lang_internal[i].id != SVC_LAST; i++)
	{
		svc_notice[lang_internal[i].id] = lang_internal[i].msg;
	}

	for(i = 0; i < SVC_LAST; i++)
	{
		if(svc_notice[i] == NULL)
		{
			die(1, "Unable to find default message for %s", svc_notice_string[i]);
		}
	}
}

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

const char *
lang_get_notice(enum svc_notice_enum msgid, struct client *client_p, struct lconn *conn_p)
{
	return svc_notice[msgid];
}

