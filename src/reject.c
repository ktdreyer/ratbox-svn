/*
 * reject.c
 *
 * $Id$
 */

#include "stdinc.h"
#include "config.h"
#include "patricia.h"
#include "client.h"
#include "s_conf.h"
#include "event.h"
#include "tools.h"
#include "reject.h"

static patricia_tree_t *reject_tree;
static dlink_list delay_exit;


static void
reject_exit(void *unused)
{
	struct Client *client_p;
	dlink_node *ptr, *ptr_next;
	DLINK_FOREACH_SAFE(ptr, ptr_next, delay_exit.head)
	{
		client_p = (struct Client *)ptr->data;
		dlinkDestroy(ptr, &delay_exit);
		exit_client(client_p, client_p, &me, "*** Banned");
	}
}


static void
reject_expires(void *unused)
{
	patricia_node_t *pnode;
	time_t last;
	PATRICIA_WALK_ALL(reject_tree->head, pnode)
	{
		last = (time_t)pnode->data;
		if(last + ConfigFileEntry.reject_ban_time < CurrentTime)
		{
			patricia_remove(reject_tree, pnode);
		}
	}
	PATRICIA_WALK_END;
}


void
init_reject(void)
{
#ifdef IPV6
	reject_tree = New_Patricia(128);
#else
	reject_tree = New_Patricia(32);
#endif
	eventAdd("reject_exit", reject_exit, NULL, 5);
	eventAdd("reject_expires", reject_expires, NULL, 60);
}


void
add_reject(struct Client *client_p)
{
	patricia_node_t *pnode;
	pnode = make_and_lookup_ip(reject_tree, &client_p->localClient->ip, GET_SS_LEN(client_p->localClient->ip));
	(time_t)pnode->data = CurrentTime;
}

int
check_reject(struct Client *client_p)
{
	patricia_node_t *pnode;
	
	pnode = match_ip(reject_tree, &client_p->localClient->ip);
	if(pnode != NULL)
	{
		/* Update the time */
		(time_t)pnode->data = CurrentTime;
		dlinkAddAlloc(client_p, &delay_exit);
		
		/* We will delay the exit for the caller */
		return 1;
	}	
	/* Caller does what it wants */	
	return 0;
}

