/* $Id$ */
#ifndef INCLUDED_c_init_h
#define INCLUDED_c_init_h

/* c_error.c */
extern struct scommand_handler error_command;

/* c_message.c */
extern struct scommand_handler privmsg_command;

/* c_mode.c */
extern struct scommand_handler mode_command;

/* s_alis.c */
extern void init_s_alis(void);

/* u_help.c */
extern struct ucommand_handler help_ucommand;

/* stats.c */
extern struct ucommand_handler stats_ucommand;

#endif
