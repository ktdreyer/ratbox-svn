/*
 *  ircd-hybrid: an advanced Internet Relay Chat Daemon(ircd).
 *  spy_links_notice.c: Sends a notice when someone uses LINKS.
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "modules.h"
#include "hook.h"
#include "client.h"
#include "ircd.h"
#include "send.h"

int
show_links(struct hook_links_data *);

void
_modinit(void)
{
  hook_add_hook("doing_links", (hookfn *)show_links);
}

void
_moddeinit(void)
{
  hook_del_hook("doing_links", (hookfn *)show_links);
}

const char *_version = "$Revision$";

int
show_links(struct hook_links_data *data)
{
  if(MyConnect(data->source_p))
    sendto_realops_flags(FLAGS_SPY, L_ALL,
                         "LINKS '%s' requested by %s (%s@%s) [%s]",
                         data->mask, data->source_p->name, data->source_p->username,
                         data->source_p->host, data->source_p->user->server);

  return 0;
}
