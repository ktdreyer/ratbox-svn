#ifndef INCLUDED_dbhook_h
#define INCLUDED_dbhook_h

/* chicken and egg, these depend on each other.. */
struct rsdb_hook;

typedef int (*dbh_callback)(struct rsdb_hook *dbh, const char *value);

struct rsdb_hook
{
	char *table;
	char *hook_value;
	dbh_callback callback;
	dlink_node ptr;
};

void init_rsdb_hook(void);

struct rsdb_hook *rsdb_hook_add(const char *table, const char *hook_value,
				unsigned int frequency, dbh_callback);
void rsdb_hook_delete(dbh_callback);

void rsdb_hook_schedule(const char *format, ...);

#endif
