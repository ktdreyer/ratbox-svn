/* $Id$ */
#ifndef INCLUDED_event_h
#define INCLUDED_event_h

#define	MAX_EVENTS	50

struct connection_entry;

typedef void EVH(void *);

/* The list of event processes */
struct ev_entry
{
	EVH *func;
	void *arg;
	const char *name;
	time_t frequency;
	time_t when;
	int active;
};

extern void eventAdd(const char *name, EVH * func, void *arg, time_t when);
extern void eventAddOnce(const char *name, EVH * func, void *arg, time_t when);
extern void eventRun(void);
extern time_t eventNextTime(void);
extern void init_events(void);
extern void eventDelete(EVH * func, void *);
extern int eventFind(EVH * func, void *);

extern void event_show(struct connection_entry *conn_p);

#endif /* INCLUDED_event_h */
