/* $Id$ */
#ifndef INCLUDED_service_h
#define INCLUDED_service_h

struct client;

struct service_handler
{
	const char *id;
	const char *name;
	const char *username;
	const char *host;
	const char *info;
	int opered;

	void (*func)(struct client *, char *text);
};

extern dlink_list service_list;

extern void add_service();
extern struct client *find_service_id(const char *name);
extern void introduce_service(struct client *client_p);
extern void introduce_services(void);

#endif
