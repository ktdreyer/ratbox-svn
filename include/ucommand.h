/* $Id$ */
#ifndef INCLUDED_ucommand_h
#define INCLUDED_ucommand_h

#define MAX_UCOMMAND_HASH 100

struct connection_entry;

struct ucommand_handler
{
	const char *cmd;
	void (*func)(struct connection_entry *, char *parv[], int parc);
	int flags;
};

extern void init_ucommand(void);
extern void handle_ucommand(struct connection_entry *, const char *command, 
				char *parv[], int parc);
extern void add_ucommand_handler(struct ucommand_handler *);

#endif
