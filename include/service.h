/* $Id$ */
#ifndef INCLUDED_service_h
#define INCLUDED_service_h

struct client;
struct connection_entry;
struct ucommand_handler;
struct cachefile;

struct service_command
{
        const char *cmd;
        int (*func)(struct client *, char *parv[], int parc);
	int minparc;
        struct cachefile *helpfile;
        int help_penalty;
        unsigned long cmd_use;
	int userreg;
	int operonly;
	int operflags;
};

struct service_handler
{
	const char *id;
	const char *name;
	const char *username;
	const char *host;
	const char *info;

        int flood_max;
        int flood_max_ignore;

	struct service_command *command;
        struct ucommand_handler *ucommand;

        void (*stats)(struct connection_entry *, char *parv[], int parc);
};

extern dlink_list service_list;

extern struct client *add_service();
extern struct client *find_service_id(const char *name);
extern void introduce_service(struct client *client_p);
extern void introduce_service_channels(struct client *client_p);
extern void introduce_services(void);
extern void introduce_services_channels(void);
extern void reintroduce_service(struct client *client_p);

extern void update_service_floodcount(void *unused);

extern void handle_service(struct client *service_p, struct client *client_p,
                           char *text);
extern void service_error(struct client *service_p, struct client *client_p,
                          const char *, ...);

extern void service_stats(struct client *service_p, struct connection_entry *);

#endif
