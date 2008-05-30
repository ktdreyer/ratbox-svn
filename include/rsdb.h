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

struct rsdb_table
{
	char ***row;
	int row_count;
	int col_count;
	void *arg;
};

void rsdb_init(void);
void rsdb_shutdown(void);

const char *rsdb_quote(const char *src);

void rsdb_exec(rsdb_callback cb, const char *format, ...);

void rsdb_exec_insert(unsigned int *insert_id, const char *table_name, 
			const char *field_name, const char *format, ...);

void rsdb_exec_fetch(struct rsdb_table *data, const char *format, ...);
void rsdb_exec_fetch_end(struct rsdb_table *data);

void rsdb_transaction(rsdb_transtype type);

/* schema generation */
typedef enum rsdb_schema_option
{
	RSDB_SCHEMA_SERIAL,		/* serial/autoincrement id field */
	RSDB_SCHEMA_SERIAL_REF,		/* reference to a serial */
	RSDB_SCHEMA_BOOLEAN,		/* boolean */
	RSDB_SCHEMA_INT,		/* integer */
	RSDB_SCHEMA_UINT,		/* unsigned integer */
	RSDB_SCHEMA_VARCHAR,		/* varchar */
	RSDB_SCHEMA_CHAR,		/* char */
	RSDB_SCHEMA_TEXT,		/* text */
	RSDB_SCHEMA_KEY_PRIMARY,	/* PRIMARY KEY */
	RSDB_SCHEMA_KEY_UNIQUE,		/* UNIQUE constraint */
	RSDB_SCHEMA_KEY_INDEX,		/* normal INDEX */
	RSDB_SCHEMA_KEY_F_MATCH,	/* FOREIGN KEY -- MATCH FULL */
	RSDB_SCHEMA_KEY_F_CASCADE	/* FOREIGN KEY -- CASCADE DELETE */
}
rsdb_schema_option;

struct rsdb_schema
{
	rsdb_schema_option option;
	unsigned int length;
	unsigned int not_null;
	const char *name;
	const char *def;
};

void rsdb_schema_generate_element(const char *table_name, struct rsdb_schema *schema_element,
				dlink_list *table_data, dlink_list *key_data);

#endif
