/* $Id$ */
#ifndef INCLUDED_service_h
#define INCLUDED_service_h

struct client;
struct lconn;
struct ucommand_handler;
struct cachefile;

#define SCMD_WALK(i, svc) do { int m = svc->service->command_size / sizeof(struct service_command); \
				for(i = 0; i < m; i++)
#define SCMD_END		} while(0)

struct service_command
{
        const char *cmd;
        int (*func)(struct client *, struct lconn *, const char **, int);
	int minparc;
        struct cachefile *helpfile;
        int help_penalty;
        unsigned long cmd_use;
	int userreg;
	int operonly;
	int operflags;
	int spyflags;
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
	unsigned long command_size;
        struct ucommand_handler *ucommand;

        void (*stats)(struct lconn *, const char **, int);
};

extern dlink_list service_list;

#define OPER_NAME(client_p, conn_p) ((conn_p) ? (conn_p)->name : (client_p)->user->oper->name)

extern struct client *add_service();
extern struct client *find_service_id(const char *name);
extern void introduce_service(struct client *client_p);
extern void introduce_service_channels(struct client *client_p);
extern void introduce_services(void);
extern void introduce_services_channels(void);
extern void reintroduce_service(struct client *client_p);
extern void deintroduce_service(struct client *client_p);

extern void update_service_floodcount(void *unused);

extern void handle_service_msg(struct client *service_p,
				struct client *client_p, char *text);
extern void handle_service(struct client *service_p, struct client *client_p,
                           const char *command, int parc, const char **parv);
extern void service_error(struct client *service_p, struct client *client_p,
                          const char *, ...);

void service_send(struct client *, struct client *, struct lconn *,
		const char *, ...);
			

extern void service_stats(struct client *service_p, struct lconn *);

#endif
