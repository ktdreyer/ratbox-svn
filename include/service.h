/* $Id$ */
#ifndef INCLUDED_service_h
#define INCLUDED_service_h

struct client;

struct service_handler
{
	const char *name;
	const char *username;
	const char *host;
	const char *info;
	void (*func)(struct client *, char *text);
};

extern dlink_list service_list;

extern void add_service();
extern void introduce_service(struct client *client_p);
extern void introduce_services(void);

#endif
