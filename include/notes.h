/* $Id: notes.h 19992 2005-01-04 21:01:16Z leeh $ */
#ifndef INCLUDED_notes_h
#define INCLUDED_notes_h

#define NOTE_CF_ALERT	0x0000001
#define NOTE_CF_BLOCK	0x0000002

int add_channote(const char *, const char *, uint32_t, const char *, ...);
int delete_channote(long);
int show_alert_note(struct client *, struct client *, const char *);
int show_block_note(struct client *, struct client *, const char *);

#endif
