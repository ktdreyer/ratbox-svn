/* src/ucommand.c
 *  Contains code for handling of commands received from local users.
 *
 *  Copyright (C) 2003 ircd-ratbox development team
 *
 *  $Id$
 */
#include "stdinc.h"
#include "ucommand.h"
#include "rserv.h"
#include "tools.h"
#include "io.h"

static dlink_list ucommand_table[MAX_UCOMMAND_HASH];

static void u_quit(struct connection_entry *, char *parv[], int parc);
static struct ucommand_handler quit_ucommand = { "quit", u_quit, 0 };

void
init_ucommand(void)
{
	add_ucommand_handler(&quit_ucommand);
}

static int
hash_command(const char *p)
{
	unsigned int hash_val = 0;

	while(*p)
	{
		hash_val += ((int) (*p) & 0xDF);
		p++;
	}

	return(hash_val % MAX_UCOMMAND_HASH);
}

void
handle_ucommand(struct connection_entry *conn_p, const char *command, 
		char *parv[], int parc)
{
	struct ucommand_handler *handler;
	dlink_node *ptr;
	unsigned int hashv = hash_command(command);

	DLINK_FOREACH(ptr, ucommand_table[hashv].head)
	{
		handler = ptr->data;
		if(!strcasecmp(command, handler->cmd))
		{
			handler->func(conn_p, parv, parc);
			break;
		}
	}
}

void
add_ucommand_handler(struct ucommand_handler *chandler)
{
	unsigned int hashv;

	if(chandler == NULL || EmptyString(chandler->cmd))
		return;

	hashv = hash_command(chandler->cmd);
	dlink_add_alloc(chandler, &ucommand_table[hashv]);
}


static void
u_quit(struct connection_entry *conn_p, char *parv[], int parc)
{
	sendto_connection(conn_p, "Goodbye.");
	(conn_p->io_close)(conn_p);
}
