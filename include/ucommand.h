/* $Id$ */
#ifndef INCLUDED_ucommand_h
#define INCLUDED_ucommand_h

#define MAX_UCOMMAND_HASH 100

struct connection_entry;
struct cachefile;
struct client;

extern dlink_list ucommand_list;

struct ucommand_handler
{
	const char *cmd;
	void (*func)(struct connection_entry *, char *parv[], int parc);
	int flags;
	int minpara;
        struct cachefile *helpfile;
};

extern void init_ucommand(void);
extern void handle_ucommand(struct connection_entry *, const char *command, 
				char *parv[], int parc);
extern void add_ucommand_handler(struct client *, struct ucommand_handler *, const char *);
extern void add_ucommands(struct client *, struct ucommand_handler *, const char *);

extern struct ucommand_handler *find_ucommand(const char *command);

#endif
