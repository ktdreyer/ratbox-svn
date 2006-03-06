/* $Id$ */
#ifndef INCLUDED_rsdb_h
#define INCLUDED_rsdb_h

typedef int (*rsdb_callback) (int, const char **);

typedef enum rsdb_transtype
{
	RSDB_TRANS_START,
	RSDB_TRANS_END
} 
rsdb_transtype;

void rsdb_init(void);
void rsdb_shutdown(void);

const char *rsdb_quote(const char *src);

void rsdb_exec(rsdb_callback cb, const char *format, ...);
void rsdb_transaction(rsdb_transtype type);

void rsdb_step_init(const char *format, ...);
int rsdb_step(int *, const char ***);
void rsdb_step_end(void);

#endif
