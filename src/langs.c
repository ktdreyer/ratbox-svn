/* src/langs.c
 *   Contains code for dealing with translations
 *
 * Copyright (C) 2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2007 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $Id$
 */
#include "stdinc.h"

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif

#include "rserv.h"
#include "langs.h"
#include "cache.h"
#include "client.h"
#include "io.h"
#include "conf.h"
#include "log.h"
#ifdef ENABLE_USERSERV
#include "s_userserv.h"
#endif

const char *langs_available[LANG_MAX];
const char **svc_notice;

const char *svc_notice_string[] =
{
	/* general service */
	"SVC_UNKNOWNCOMMAND",
	"SVC_SUCCESSFUL",
	"SVC_SUCCESSFULON",
	"SVC_ISSUED",
	"SVC_NEEDMOREPARAMS",
	"SVC_ISDISABLED",
	"SVC_ISDISABLEDEMAIL",
	"SVC_NOTSUPPORTED",
	"SVC_NOACCESS",
	"SVC_OPTIONINVALID",
	"SVC_RATELIMITEDGENERIC",
	"SVC_RATELIMITED",
	"SVC_RATELIMITEDHOST",
	"SVC_NOTLOGGEDIN",
	"SVC_ENDOFLIST",
	"SVC_ENDOFLISTLIMIT",
	"SVC_USECOMMANDSHORTCUT",

	/* general irc related */
	"SVC_IRC_NOSUCHCHANNEL",
	"SVC_IRC_CHANNELINVALID",
	"SVC_IRC_CHANNELNOUSERS",
	"SVC_IRC_NOSUCHSERVER",
	"SVC_IRC_SERVERNAMEINVALID",
	"SVC_IRC_ALREADYONCHANNEL",
	"SVC_IRC_YOUALREADYONCHANNEL",
	"SVC_IRC_NOTINCHANNEL",
	"SVC_IRC_YOUNOTINCHANNEL",
	"SVC_IRC_NOTOPPEDONCHANNEL",

	/* email */
	"SVC_EMAIL_INVALID",
	"SVC_EMAIL_INVALIDIGNORED",
	"SVC_EMAIL_BANNEDDOMAIN",
	"SVC_EMAIL_TEMPUNAVAILABLE",
	"SVC_EMAIL_SENDFAILED",

	/* service help */
	"SVC_HELP_INDEXINFO",
	"SVC_HELP_TOPICS",
	"SVC_HELP_UNAVAILABLE",
	"SVC_HELP_UNAVAILABLETOPIC",
	"SVC_HELP_INDEXADMIN",

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

	/* userserv::info */
	/* chanserv::info */
	/* nickserv::info */
	"SVC_INFO_REGDURATIONUSER",
	"SVC_INFO_REGDURATIONCHAN",
	"SVC_INFO_REGDURATIONNICK",
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

	/* nickserv */
	"SVC_NICK_NOTONLINE",
	"SVC_NICK_ALREADYREG",
	"SVC_NICK_NOTREG",
	"SVC_NICK_NOWREG",
	"SVC_NICK_CANTREGUID",
	"SVC_NICK_USING",
	"SVC_NICK_TOOMANYREG",
	"SVC_NICK_LOGINFIRST",
	"SVC_NICK_REGGEDOTHER",
	"SVC_NICK_CHANGEDOPTION",
	"SVC_NICK_QUERYOPTION",

	/* chanserv */
	"SVC_CHAN_NOWREG",
	"SVC_CHAN_NOTREG",
	"SVC_CHAN_ALREADYREG",
	"SVC_CHAN_CHANGEDOPTION",
	"SVC_CHAN_UNSETOPTION",
	"SVC_CHAN_QUERYOPTION",
	"SVC_CHAN_QUERYOPTIONALREADY",
	"SVC_CHAN_LISTSTART",
	"SVC_CHAN_ISSUSPENDED",
	"SVC_CHAN_NOACCESS",
	"SVC_CHAN_USERNOACCESS",
	"SVC_CHAN_USERALREADYACCESS",
	"SVC_CHAN_USERHIGHERACCESS",
	"SVC_CHAN_INVALIDACCESS",
	"SVC_CHAN_INVALIDAUTOLEVEL",
	"SVC_CHAN_INVALIDSUSPENDLEVEL",
	"SVC_CHAN_USERSETACCESS",
	"SVC_CHAN_USERREMOVED",
	"SVC_CHAN_USERSETAUTOLEVEL",
	"SVC_CHAN_USERSETSUSPEND",
	"SVC_CHAN_USERSUSPENDREMOVED",
	"SVC_CHAN_USERHIGHERSUSPEND",
	"SVC_CHAN_REQUESTPENDING",
	"SVC_CHAN_REQUESTNONE",
	"SVC_CHAN_TOKENMISMATCH",
	"SVC_CHAN_NOMODE",
	"SVC_CHAN_INVALIDMODE",
	"SVC_CHAN_ALREADYOPPED",
	"SVC_CHAN_ALREADYVOICED",
	"SVC_CHAN_YOUNOTBANNED",
	"SVC_CHAN_USEDELOWNER",
	"SVC_CHAN_BANSET",
	"SVC_CHAN_BANREMOVED",
	"SVC_CHAN_ALREADYBANNED",
	"SVC_CHAN_NOTBANNED",
	"SVC_CHAN_BANLISTFULL",
	"SVC_CHAN_INVALIDBAN",
	"SVC_CHAN_BANHIGHERLEVEL",
	"SVC_CHAN_BANHIGHERACCOUNT",
	"SVC_CHAN_BANLISTSTART",

	/* operserv */
	"SVC_OPER_CONNECTIONSSTART",
	"SVC_OPER_CONNECTIONSEND",
	"SVC_OPER_SERVERNAMEMISMATCH",
	"SVC_OPER_OSPARTACCESS",

	/* banserv */
	"SVC_BAN_ISSUED",
	"SVC_BAN_ALREADYPLACED",
	"SVC_BAN_NOTPLACED",
	"SVC_BAN_INVALID",
	"SVC_BAN_LISTSTART",
	"SVC_BAN_NOPERMACCESS",

	/* global */
	"SVC_GLOBAL_WELCOMETOOLONG",
	"SVC_GLOBAL_WELCOMEINVALID",
	"SVC_GLOBAL_WELCOMESET",
	"SVC_GLOBAL_WELCOMENOTSET",
	"SVC_GLOBAL_WELCOMEDELETED",
	"SVC_GLOBAL_WELCOMELIST",

	/* jupeserv */
	"SVC_JUPE_ALREADYJUPED",
	"SVC_JUPE_NOTJUPED",
	"SVC_JUPE_ALREADYREQUESTED",
	"SVC_JUPE_PENDINGLIST",

	/* alis */
	"SVC_ALIS_LISTSTART",

	/* must be last */
	"\0"
};

void
init_langs(void)
{
	char pathbuf[PATH_MAX];
	DIR *helpdir;
	struct dirent *subdir;
	struct stat subdirinfo;
	int i;

	/* ensure the default language is always at position 0 */
	memset(langs_available, 0, sizeof(const char *) * LANG_MAX);
	(void) lang_get_langcode(LANG_DEFAULT);

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

	if((helpdir = opendir(HELPDIR)) == NULL)
	{
		mlog("Warning: Unable to open helpfile directory: %s", HELPDIR);
		return;
	}

	while((subdir = readdir(helpdir)))
	{
		/* skip '.' and '..' */
		if(!strcmp(subdir->d_name, ".") || !strcmp(subdir->d_name, ".."))
			continue;

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s",
				HELPDIR, subdir->d_name);

		if(stat(pathbuf, &subdirinfo) >= 0)
		{
			if(S_ISDIR(subdirinfo.st_mode))
				(void) lang_get_langcode(subdir->d_name);
		}
	}

	(void) closedir(helpdir);
}

unsigned int
lang_get_langcode(const char *name)
{
	unsigned int i;

	/* first hunt for a match */
	for(i = 0; langs_available[i]; i++)
	{
		if(!strcasecmp(langs_available[i], name))
			return i;
	}

	/* not found, add it in at i */
	if(i+1 >= LANG_MAX)
	{
		mlog("Warning: Reach maximum amount of languages, translations may not be loaded correctly");
		return 0;
	}

	langs_available[i] = my_strdup(name);
	return i;
}

struct cachefile *
lang_get_cachefile(struct cachefile **translations, struct client *client_p)
{
#ifdef ENABLE_USERSERV
	if(client_p != NULL && client_p->user != NULL && client_p->user->user_reg != NULL)
	{
		unsigned int language = client_p->user->user_reg->language;

		if(translations[language] != NULL)
			return translations[language];
	}
#endif

	if(translations[config_file.default_language] != NULL)
		return translations[config_file.default_language];

	/* base translation is always first */
	return translations[0];
}

struct cachefile *
lang_get_cachefile_u(struct cachefile **translations, struct lconn *conn_p)
{
	/* base translation is always first */
	return translations[0];
}

const char *
lang_get_notice(enum svc_notice_enum msgid, struct client *client_p, struct lconn *conn_p)
{
	return svc_notice[msgid];
}

