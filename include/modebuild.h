/* $Id$ */
#ifndef INCLUDED_modebuild_h
#define INCLUDED_modebuild_h

#define DIR_NONE 0
#define DIR_ADD  1
#define DIR_DEL  2

void modebuild_start(struct client *, struct channel *);
void modebuild_add(int dir, const char *mode, const char *arg);
void modebuild_finish(void);

#endif
