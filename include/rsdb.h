/* $Id$ */
#ifndef INCLUDED_rsdb_h
#define INCLUDED_rsdb_h

struct _rsdb_conf
{
	const char *db_name;
	const char *db_host;
	const char *db_username;
	const char *db_password;
};

extern struct _rsdb_conf rsdb_conf;


typedef int (*rsdb_callback) (int, const char **);

typedef enum rsdb_transtype
{
	RSDB_TRANS_START,
	RSDB_TRANS_END
} 
rsdb_transtype;

struct rsdb_table
{
	char ***row;
	int row_count;
	int col_count;
	void *arg;
};

void rsdb_init(const char *db_name, const char *db_host, const char *db_username, 
		const char *db_password);
void rsdb_shutdown(void);

const char *rsdb_quote(const char *src);

void rsdb_exec(rsdb_callback cb, const char *format, ...);

void rsdb_exec_insert(unsigned int *insert_id, const char *table_name, 
			const char *field_name, const char *format, ...);

void rsdb_exec_fetch(struct rsdb_table *data, const char *format, ...);
void rsdb_exec_fetch_end(struct rsdb_table *data);

void rsdb_transaction(rsdb_transtype type);


/* rsdb_common.c */
void rsdb_common_fetch_end(struct rsdb_table *table);

#endif
