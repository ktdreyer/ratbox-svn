/* copyright (c) 2000 Edward Brocklesby, Hybrid Development Team */

#include "modules.h"
#include "hook.h"
#include "client.h"
#include "ircd.h"
#include "send.h"

int
show_stats(struct hook_mfunc_data *);

void
_modinit(void)
{
	hook_add_hook("doing_stats", (hookfn *)show_stats);
}

void
_moddeinit(void)
{
	hook_del_hook("doing_stats", (hookfn *)show_stats);
}

char *_version = "1.0";

/* show a stats request */
int
show_stats(struct hook_stats_data *data)
{
	sendto_realops_flags(FLAGS_SPY, "STATS %c requested by %s (%s@%s) [%s]",
						 data->statchar, data->sptr->name, data->sptr->username,
						 data->sptr->host, data->sptr->user->server);
}
