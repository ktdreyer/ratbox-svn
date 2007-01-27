#include "langs.h"

struct _lang_internal lang_internal[] = 
{
	/* general service */
	{ SVC_SUCCESSFUL,		"%s%s successful"					},
	{ SVC_SUCCESSFULON,		"%s%s successful on %s"					},
	{ SVC_ISSUED,			"%s%s issued"						},
	{ SVC_NEEDMOREPARAMS,		"Insufficient parameters to %s%s"			},
	{ SVC_ISDISABLED,		"%s%s is disabled"					},
	{ SVC_ISDISABLEDEMAIL,		"%s%s is disabled as it cannot send emails"		},
	{ SVC_NOTSUPPORTED,		"%s%s is not supported by your server"			},
	{ SVC_NOACCESS,			"No access to %s%s"					},
	{ SVC_OPTIONINVALID,		"%s%s option invalid"					},
	{ SVC_RATELIMITED,		"%s%s rate-limited, try again shortly"			},
	{ SVC_RATELIMITEDHOST,		"%s%s rate-limited for your host, try again shortly"	},
	{ SVC_ENDOFLIST,		"End of list"						},

	/* general irc related */
	{ SVC_IRC_NOSUCHCHANNEL,	"Channel %s does not exist"				},
	{ SVC_IRC_CHANNELINVALID,	"Invalid channel %s"					},
	{ SVC_IRC_CHANNELNOUSERS,	"Channel %s has no users"				},
	{ SVC_IRC_NOSUCHSERVER,		"Server %s does not exist"				},
	{ SVC_IRC_ALREADYONCHANNEL,	"%s is already on channel %s"				},
	{ SVC_IRC_NOTONCHANNEL,		"%s is not on channel %s"				},

	/* email */
	{ SVC_EMAIL_INVALID,		"Email %s invalid"					},
	{ SVC_EMAIL_INVALIDIGNORED,	"Email %s invalid, ignoring"				},
	{ SVC_EMAIL_BANNEDDOMAIN,	"Email provider banned"					},
	{ SVC_EMAIL_TEMPUNAVAILABLE,	"Temporarily unable to send email, please try later"	},
	{ SVC_EMAIL_SENDFAILED,		"Unable to complete %s%s due to problems sending email"	},

	/* userserv */
	{ SVC_USER_USERLOGGEDIN,	"%s has just authenticated as you (%s)"			},
	{ SVC_USER_REGISTERDISABLED,	"%s%s is disabled, see %s"				},
	{ SVC_USER_ALREADYREG,		"Username %s is already registered"			},
	{ SVC_USER_NOTREG,		"Username %s is not registered"				},
	{ SVC_USER_NOWREG,		"Username %s registered"				},
	{ SVC_USER_NOWREGLOGGEDIN,	"Username %s registered, you are now logged in"		},
	{ SVC_USER_NOWREGEMAILED,	"Username %s registered, your activation token has been emailed"		},
	{ SVC_USER_REGDROPPED,		"Username %s registration dropped"			},
	{ SVC_USER_INVALIDUSERNAME,	"Username %s invalid"					},
	{ SVC_USER_INVALIDPASSWORD,	"Invalid password"					},
	{ SVC_USER_INVALIDLANGUAGE,	"Invalid language %s"					},
	{ SVC_USER_LONGPASSWORD,	"Password too long"					},
	{ SVC_USER_LOGINSUSPENDED,	"Login failed, username has been suspended"		},
	{ SVC_USER_LOGINUNACTIVATED,	"Login failed, username has not been activated.  Use %s::ACTIVATE first"	},
	{ SVC_USER_LOGINMAX,		"Login failed, username has %d logged in users"		},
	{ SVC_USER_ALREADYLOGGEDIN,	"You are already logged in"				},
	{ SVC_USER_NICKNOTLOGGEDIN,	"Nickname %s is not logged in"				},
	{ SVC_USER_NOEMAIL,		"Username %s does not have an email address"		},
	{ SVC_USER_CHANGEDPASSWORD,	"Username %s password changed"				},
	{ SVC_USER_CHANGEDOPTION,	"Username %s %s set to %s"				},
	{ SVC_USER_QUERYOPTION,		"Username %s %s is set to %s"				},
	{ SVC_USER_QUERYOPTIONALREADY,	"Username %s %s is already set to %s",			},
	{ SVC_USER_REQUESTISSUED,	"Username %s has been sent an email to confirm the %s request"			},
	{ SVC_USER_REQUESTPENDING,	"Username %s already has a pending %s request"		},
	{ SVC_USER_REQUESTNONE,		"Username %s does not have a pending %s request"	},
	{ SVC_USER_TOKENBAD,		"Username %s %s token is malformed"			},
	{ SVC_USER_TOKENMISMATCH,	"Username %s %s tokens do not match"			},
	{ SVC_USER_DURATIONTOOSHORT,	"Username %s has not been registered long enough to use %s%s"			},

	/* userserv::activate */
	{ SVC_USER_ACT_ALREADY,		"Username %s has already been activated for use"	},
	{ SVC_USER_ACT_COMPLETE,	"Username %s activated, you may now LOGIN"		},

	/* userserv::resetpass */
	{ SVC_USER_RP_LOGGEDIN,		"You cannot request a password reset whilst logged in"	},

	/* userserv::userlist */
	{ SVC_USER_UL_START,		"Username list matching %s, limit %u%s"			},
	{ SVC_USER_UL_ENDLIMIT,		"End of username list, limit reached"			},

	/* userserv::info */
	/* chanserv::info */
	/* nickserv::info */
	{ SVC_INFO_REGDURATIONUSER,	"[%s] Username registered for %s"			},
	{ SVC_INFO_REGDURATIONCHAN,	"[%s] Channel registered to %s for %s"			},
	{ SVC_INFO_REGDURATIONNICK,	"[%s] Nickname registered to %s for %s"			},
	{ SVC_INFO_SUSPENDED,		"[%s] Suspended by %s: %s"				},
	{ SVC_INFO_SUSPENDEDADMIN,	"[%s] Suspended by services admin"			},
	{ SVC_INFO_ACCESSLIST,		"[%s] Access list: %s"					},
	{ SVC_INFO_NICKNAMES,		"[%s] Registered nicknames: %s"				},
	{ SVC_INFO_EMAIL,		"[%s] Email: %s"					},
	{ SVC_INFO_URL,			"[%s] URL: %s"						},
	{ SVC_INFO_TOPIC,		"[%s] Topic: %s"					},
	{ SVC_INFO_SETTINGS,		"[%s] Settings: %s"					},
	{ SVC_INFO_ENFORCEDMODES,	"[%s] Enforced modes: %s"				},
	{ SVC_INFO_CURRENTLOGON,	"[%s] Currently logged on via:"				},

	/* nickserv */
	{ SVC_NICK_NOTONLINE,		"Nickname %s is not online"				},
	{ SVC_NICK_ALREADYREG,		"Nickname %s is already registered"			},
	{ SVC_NICK_NOTREG,		"Nickname %s is not registered"				},
	{ SVC_NICK_NOWREG,		"Nickname %s registered"				},
	{ SVC_NICK_CANTREGUID,		"You may not register your UID, please change to a real nickname"			},
	{ SVC_NICK_USING,		"Nickname %s is already in use by you"			},
	{ SVC_NICK_TOOMANYREG,		"You have already registered %d nicknames"		},
	{ SVC_NICK_LOGINFIRST,		"You must register a username with %s and log in before you can register your nickname"	},
	{ SVC_NICK_REGGEDOTHER,		"Nickname %s is not registered to you"			},
	{ SVC_NICK_CHANGEDOPTION,	"Nickname %s %s set to %s"				},
	{ SVC_NICK_QUERYOPTION,		"Nickname %s %s is set to %s"				},

	/* operserv */
	{ SVC_OPER_CONNECTIONSSTART,	"Current connections (%s)"				},
	{ SVC_OPER_CONNECTIONSEND,	"End of connections"					},
	{ SVC_OPER_SERVERNAMEMISMATCH,	"Servernames do not match"				},
	{ SVC_OPER_OSPARTACCESS,	"No access to %s::OSPART on channels joined through %s"	},

	/* banserv */
	{ SVC_BAN_ISSUED,		"Issued %s for %s"					},
	{ SVC_BAN_ALREADYPLACED,	"%s already placed on %s"				},
	{ SVC_BAN_NOTPLACED,		"%s not placed on %s"					},
	{ SVC_BAN_INVALID,		"Invalid %s: %s"					},
	{ SVC_BAN_LISTSTART,		"Ban list matching %s"					},
	{ SVC_BAN_NOPERMACCESS,		"No access to set a permanent %s"			},

	/* this must be last */
	{ SVC_LAST,		"\0"	},
};

