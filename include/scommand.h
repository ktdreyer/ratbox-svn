/* $Id$ */
#ifndef INCLUDED_scommand_h
#define INCLUDED_scommand_h

#define MAX_SCOMMAND_HASH 100

struct client;

struct scommand_handler
{
	const char *cmd;
	void (*func)(struct client *, char *parv[], int parc);
	int flags;
};

#define FLAGS_UNKNOWN	0x0001

extern void init_scommand(void);
extern void handle_scommand(const char *command, char *parv[], int parc);
extern void add_scommand_handler(struct scommand_handler *);

#endif
