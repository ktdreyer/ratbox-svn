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

#define MAX_MULTI_MESSAGES 10


#define ENTITY_NONE    0
#define ENTITY_CHANNEL 1
#define ENTITY_CHANOPS_ON_CHANNEL 2
#define ENTITY_CLIENT  3

#endif
