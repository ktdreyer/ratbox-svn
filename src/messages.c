#include "langs.h"

struct _lang_internal lang_internal[] = {

	/* general service */
	{ SVC_ISDISABLED,		"%s%s is disabled"			},

	/* userserv */
	{ SVC_USER_ALREADYREG,		"Username %s is already registered"	},
	{ SVC_USER_NOTREG,		"Username %s is not registered"		},
	{ SVC_USER_INVALIDUSERNAME,	"Username %s invalid"			},
	{ SVC_USER_INVALIDPASSWORD,	"Invalid password"			},
	{ SVC_USER_LONGPASSWORD,	"Password too long"			},
	{ SVC_USER_ALREADYLOGGEDIN,	"You are already logged in"		},

	/* this must be last */
	{ SVC_LAST,		"\0"	},
};

