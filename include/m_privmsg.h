#ifndef INCLUDED_m_privmsg_h
#define INCLUDED_m_privmsg_h

struct entity {
  void *ptr;
  int type;
  int flags;
};

int build_target_list(struct Client *sptr,
		      char *nicks_channels, struct entity target_table[]);

int drone_attack(struct Client *sptr, struct Client *acptr);
#endif
