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

/* s_operbot.c */
extern void init_s_operbot(void);

/* s_userserv.c */
extern void init_s_userserv(void);

/* s_chanserv.c */
extern void init_s_chanserv(void);

void init_s_jupeserv(void);
void init_s_operserv(void);
void init_s_nickserv(void);

void init_s_global(void);

/* u_stats.c */
extern struct ucommand_handler stats_ucommand;

#endif
