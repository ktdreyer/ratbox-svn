/* $Id$ */
#ifndef INCLUDED_ucommand_h
#define INCLUDED_ucommand_h

#define MAX_UCOMMAND_HASH 100

struct lconn;
struct cachefile;
struct client;

extern dlink_list ucommand_list;

struct ucommand_handler
{
	const char *cmd;
	void (*func)(struct client *, struct lconn *, const char **, int);
	int flags;
	int minpara;
	int spy;
        struct cachefile *helpfile;
};

extern void init_ucommand(void);
extern void handle_ucommand(struct lconn *, const char *command, 
				const char *parv[], int parc);
extern void add_ucommand_handler(struct client *, struct ucommand_handler *, const char *);
extern void add_ucommands(struct client *, struct ucommand_handler *, const char *);

extern struct ucommand_handler *find_ucommand(const char *command);

#endif
