#include "langs.h"

struct _lang_internal lang_internal[] = {

	/* general service */
	{ SVC_SUCCESSFUL,		"%s%s successful"					},
	{ SVC_NEEDMOREPARAMS,		"Insufficient parameters to %s%s"			},
	{ SVC_ISDISABLED,		"%s%s is disabled"					},
	{ SVC_RATELIMITED,		"%s%s rate-limited, try again shortly"			},
	{ SVC_RATELIMITEDHOST,		"%s%s rate-limited for your host, try again shortly"	},
	
	/* email */
	{ SVC_EMAIL_INVALID,		"Email %s invalid"					},
	{ SVC_EMAIL_BANNEDDOMAIN,	"Email provider banned"					},
	{ SVC_EMAIL_TEMPUNAVAILABLE,	"Temporarily unable to send email, please try later"	},
	{ SVC_EMAIL_SENDFAILED,		"Unable to complete %s%s due to problems sending email"	},

	/* userserv */
	{ SVC_USER_REGISTERDISABLED,	"%s%s is disabled, see %s"				},
	{ SVC_USER_ALREADYREG,		"Username %s is already registered"			},
	{ SVC_USER_NOTREG,		"Username %s is not registered"				},
	{ SVC_USER_INVALIDUSERNAME,	"Username %s invalid"					},
	{ SVC_USER_INVALIDPASSWORD,	"Invalid password"					},
	{ SVC_USER_LONGPASSWORD,	"Password too long"					},
	{ SVC_USER_ALREADYLOGGEDIN,	"You are already logged in"				},
	{ SVC_USER_NICKNOTLOGGEDIN,	"Nickname %s is not logged in"				},
	{ SVC_USER_NOEMAIL,		"Username %s does not have an email address"		},
	{ SVC_USER_CHANGEDPASSWORD,	"Username %s password changed"				},

	/* this must be last */
	{ SVC_LAST,		"\0"	},
};

