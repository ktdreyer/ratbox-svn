#ifndef INCLUDED_m_privmsg_h
#define INCLUDED_m_privmsg_h

struct entity {
  void *ptr;
  int type;
  int flags;
};

int build_target_list(char *nicks_channels, struct entity target_table[]);

#endif
