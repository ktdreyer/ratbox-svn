#include "langs.h"

struct _lang_internal lang_internal[] = 
{
	/* general service */
	{ SVC_SUCCESSFUL,		"%s%s successful"					},
	{ SVC_NEEDMOREPARAMS,		"Insufficient parameters to %s%s"			},
	{ SVC_ISDISABLED,		"%s%s is disabled"					},
	{ SVC_ISDISABLEDEMAIL,		"%s%s is disabled as it cannot send emails"		},
	{ SVC_OPTIONINVALID,		"%s%s option invalid"					},
	{ SVC_RATELIMITED,		"%s%s rate-limited, try again shortly"			},
	{ SVC_RATELIMITEDHOST,		"%s%s rate-limited for your host, try again shortly"	},
	
	/* email */
	{ SVC_EMAIL_INVALID,		"Email %s invalid"					},
	{ SVC_EMAIL_BANNEDDOMAIN,	"Email provider banned"					},
	{ SVC_EMAIL_TEMPUNAVAILABLE,	"Temporarily unable to send email, please try later"	},
	{ SVC_EMAIL_SENDFAILED,		"Unable to complete %s%s due to problems sending email"	},

	/* userserv */
	{ SVC_USER_USERLOGGEDIN,	"%s has just authenticated as you (%s)"			},
	{ SVC_USER_REGISTERDISABLED,	"%s%s is disabled, see %s"				},
	{ SVC_USER_ALREADYREG,		"Username %s is already registered"			},
	{ SVC_USER_NOTREG,		"Username %s is not registered"				},
	{ SVC_USER_NOWREG,		"Username %s registered, you are now logged in"		},
	{ SVC_USER_NOWREGEMAILED,	"Username %s registered, your activation token has been emailed"		},
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
	{ SVC_USER_CHANGEDEMAIL,	"Username %s email changed"				},
	{ SVC_USER_CHANGEDOPTION,	"Username %s %s set to %s"				},
	{ SVC_USER_QUERYOPTION,		"Username %s %s is set to %s"				},
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

	/* this must be last */
	{ SVC_LAST,		"\0"	},
};

