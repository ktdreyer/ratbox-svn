#ifndef INCLUDED_langs_h
#define INCLUDED_langs_h

struct lconn;
struct client;

/* DO NOT CHANGE LANG_DEFAULT.
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

void init_langs(void);

struct cachefile *lang_get_cachefile(struct cachefile **, struct client *);
struct cachefile *lang_get_cachefile_u(struct cachefile **, struct lconn *);

/* when changing this, you MUST reflect the change in svc_notice_string in
 * langs.c and add a default into messages.c
 */
enum svc_notice_enum
{
	/* general service */
	SVC_SUCCESSFUL,
	SVC_SUCCESSFULON,
	SVC_ISSUED,
	SVC_NEEDMOREPARAMS,
	SVC_ISDISABLED,
	SVC_ISDISABLEDEMAIL,
	SVC_NOACCESS,
	SVC_OPTIONINVALID,
	SVC_RATELIMITED,
	SVC_RATELIMITEDHOST,

	/* general irc related */
	SVC_IRC_NOSUCHCHANNEL,
	SVC_IRC_CHANNELINVALID,
	SVC_IRC_CHANNELNOUSERS,
	SVC_IRC_NOSUCHSERVER,

	/* email */
	SVC_EMAIL_INVALID,
	SVC_EMAIL_INVALIDIGNORED,
	SVC_EMAIL_BANNEDDOMAIN,
	SVC_EMAIL_TEMPUNAVAILABLE,
	SVC_EMAIL_SENDFAILED,

	/* userserv */
	SVC_USER_USERLOGGEDIN,
	SVC_USER_REGISTERDISABLED,
	SVC_USER_ALREADYREG,
	SVC_USER_NOTREG,
	SVC_USER_NOWREG,
	SVC_USER_NOWREGLOGGEDIN,
	SVC_USER_NOWREGEMAILED,
	SVC_USER_REGDROPPED,
	SVC_USER_INVALIDUSERNAME,
	SVC_USER_INVALIDPASSWORD,
	SVC_USER_INVALIDLANGUAGE,
	SVC_USER_LONGPASSWORD,
	SVC_USER_LOGINSUSPENDED,
	SVC_USER_LOGINUNACTIVATED,
	SVC_USER_LOGINMAX,
	SVC_USER_ALREADYLOGGEDIN,
	SVC_USER_NICKNOTLOGGEDIN,
	SVC_USER_NOEMAIL,
	SVC_USER_CHANGEDPASSWORD,
	SVC_USER_CHANGEDEMAIL,
	SVC_USER_CHANGEDOPTION,
	SVC_USER_QUERYOPTION,
	SVC_USER_QUERYOPTIONALREADY,
	SVC_USER_REQUESTISSUED,
	SVC_USER_REQUESTPENDING,
	SVC_USER_REQUESTNONE,
	SVC_USER_TOKENBAD,
	SVC_USER_TOKENMISMATCH,
	SVC_USER_DURATIONTOOSHORT,
	/* userserv::activate */
	SVC_USER_ACT_ALREADY,
	SVC_USER_ACT_COMPLETE,
	/* userserv::resetpass */
	SVC_USER_RP_LOGGEDIN,
	/* userserv::userlist */
	SVC_USER_UL_START,
	SVC_USER_UL_END,
	SVC_USER_UL_ENDLIMIT,

	/* userserv::info */
	/* chanserv::info */
	SVC_INFO_REGDURATIONUSER,
	SVC_INFO_REGDURATIONCHAN,
	SVC_INFO_SUSPENDED,
	SVC_INFO_SUSPENDEDADMIN,
	SVC_INFO_ACCESSLIST,
	SVC_INFO_NICKNAMES,
	SVC_INFO_EMAIL,
	SVC_INFO_URL,
	SVC_INFO_TOPIC,
	SVC_INFO_SETTINGS,
	SVC_INFO_ENFORCEDMODES,
	SVC_INFO_CURRENTLOGON,

	/* operserv */
	SVC_OPER_CONNECTIONSSTART,
	SVC_OPER_CONNECTIONSEND,
	SVC_OPER_SERVERNAMEMISMATCH,

	/* banserv */
	SVC_BAN_ISSUED,
	SVC_BAN_ALREADYPLACED,
	SVC_BAN_NOTPLACED,
	SVC_BAN_INVALID,
	SVC_BAN_LISTSTART,
	SVC_BAN_LISTEND,
	SVC_BAN_NOPERMACCESS,

	/* this must be last */
	SVC_LAST
} svc_notice_id;

/* contains the string version of the enum above */
extern const char *svc_notice_string[];

extern const char **svc_notice;

const char *lang_get_notice(enum svc_notice_enum msgid, struct client *, struct lconn *);

/* used to create the 'default' hardcoded language from messages.c */
struct _lang_internal
{
	enum svc_notice_enum id;
	const char *msg;
};

extern struct _lang_internal lang_internal[];

#endif
