/************************************************************************
 *   IRC - Internet Relay Chat, doc/example_module.c
 *   Copyright (C) 2001 Hybrid Development Team
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   $Id$
 */

/* List of ircd includes from ../include/ */
#include "stdinc.h"
#include "handlers.h"
#include "client.h"
#include "common.h"		/* FALSE bleah */
#include "ircd.h"
#include "irc_string.h"
#include "numeric.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"


/* Declare the void's initially up here, as modules dont have an
 * include file, we will normally have client_p, source_p, parc
 * and parv[] where:
 *
 * client_p == client issuing command
 * source_p == where the command came from
 * parc     == the number of parameters
 * parv     == an array of the parameters
 */
static void mr_test(struct Client *client_p, struct Client *source_p, int parc, char *parv[]);
static void m_test(struct Client *client_p, struct Client *source_p, int parc, char *parv[]);
static void ms_test(struct Client *client_p, struct Client *source_p, int parc, char *parv[]);
static void mo_test(struct Client *client_p, struct Client *source_p, int parc, char *parv[]);

/* Show the commands this module can handle in a msgtab
 * and give the msgtab a name, here its test_msgtab
 */
struct Message test_msgtab = {

	/* Fields are in order:
	 *-> "COMMAND", 0, 0, parc_count, maxparc, MFLG_SLOW, 0,
	 *
	 * where:
	 * COMMAND == the /command you want
	 * parc_count == the number of parameters needed
	 *               (the clients name is one param, parv[0])
	 * maxparc == the maximum parameters we allow
	 * the 0's and MFLG_SLOW should not be changed..
	 */

	/* This would add the command "TEST" which requires no additional
	 * parameters
	 */
	"TEST", 0, 0, 1, 0, MFLG_SLOW, 0,

	/* Fields are in order:
	 *-> {unregged, regged, remote, oper}
	 *
	 * where:
	 * unregged == function to call for unregistered clients
	 * regged == function to call for normal users
	 * remote == function to call for servers/remote users
	 * oper == function to call for operators
	 *
	 * There are also some pre-coded functions for use:
	 * m_unregistered: prevent the client using this if unregistered
	 * m_not_oper:     tell the client it requires being an operator
	 * m_ignore:       ignore the command when it comes from certain types
	 * m_error:        give an error when the command comes from certain types
	 */
	{mr_test, m_test, ms_test, mo_test}

	/* It is normal for unregistered functions to be prefixed with mr_
	 *   "      "       normal users to be prefixed with m_
	 *   "      "       remote clients to be prefixed with ms_
	 *   "      "       operators to be prefixed with mo_
	 */
};

/* Thats the msgtab finished */

#ifndef STATIC_MODULES
/* The mapi_clist_av1 indicates which commands (struct Message)
   should be loaded from the module. The list should be terminated
   by a NULL. */
mapi_clist_av1 test_clist[] = { &test_msgtab, NULL };

/* Here we tell it what to do when the module is loaded */
int
modinit(void)
{
	/* Nothing to do for the example module. */
	/* The init function should return -1 on failure,
	   which will cause the module to be unloaded,
	   otherwise 0 to indicate success. */
	return 0;
}

/* here we tell it what to do when the module is unloaded */
void
moddeinit(void)
{
	/* Again, nothing to do. */
}

/* DECLARE_MODULE_AV1() actually declare the MAPI header. */
DECLARE_MODULE_AV1(
			  /* The first argument is the function to call on load */
			  modinit,
			  /* And the function to call on unload */
			  moddeinit,
			  /* Then the MAPI command list */
			  test_clist,
			  /* Next the hook list, if we have one. */
			  NULL,
			  /* And finally the version number of this module. */
			  "$Revision$");

/* Any of the above arguments can be NULL to indicate they aren't used. */
#endif

/*
 * mr_test
 *      parv[0] = sender prefix
 *      parv[1] = parameter
 */

/* Here we have the functions themselves that we declared above,
 * and the fairly normal C coding
 */
static void
mr_test(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	if(parc == 1)
		sendto_one(source_p, ":%s NOTICE %s :You are unregistered and sent no parameters",
			   me.name, source_p->name);
	else
		sendto_one(source_p, ":%s NOTICE %s :You are unregistered and sent parameter: %s",
			   me.name, source_p->name, parv[1]);
}

/*
 * m_test
 *      parv[0] = sender prefix
 *      parv[1] = parameter
 */
static void
m_test(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	if(parc == 1)
		sendto_one(source_p, ":%s NOTICE %s :You are a normal user, and sent no parameters",
			   me.name, source_p->name);
	else
		sendto_one(source_p,
			   ":%s NOTICE %s :You are a normal user, and send parameters: %s", me.name,
			   source_p->name, parv[1]);
}

/*
 * ms_test
 *      parv[0] = sender prefix
 *      parv[1] = parameter
 */
static void
ms_test(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	if(parc == 1)
	{
		if(IsServer(source_p))
			sendto_one(source_p,
				   ":%s NOTICE %s :You are a server, and sent no parameters",
				   me.name, source_p->name);
		else
			sendto_one(source_p,
				   ":%s NOTICE %s :You are a remote client, and sent no parameters",
				   me.name, source_p->name);
	}
	else
	{
		if(IsServer(source_p))
			sendto_one(source_p,
				   ":%s NOTICE %s :You are a server, and sent parameters: %s",
				   me.name, source_p->name, parv[1]);
		else
			sendto_one(source_p,
				   ":%s NOTICE %s :You are a remote client, and sent parameters: %s",
				   me.name, source_p->name, parv[1]);
	}
}

/*
 * mo_test
 *      parv[0] = sender prefix
 *      parv[1] = parameter
 */
static void
mo_test(struct Client *client_p, struct Client *source_p, int parc, char *parv[])
{
	if(parc == 1)
		sendto_one(source_p, ":%s NOTICE %s :You are an operator, and sent no parameters",
			   me.name, source_p->name);
	else
		sendto_one(source_p, ":%s NOTICE %s :You are an operator, and sent parameters: %s",
			   me.name, source_p->name, parv[1]);
}

/* END OF EXAMPLE MODULE */
