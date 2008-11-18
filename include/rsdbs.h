/* $Id$ */
#ifndef INCLUDED_rsdbs_h
#define INCLUDED_rsdbs_h

/* rsdbs_schema.c */
void schema_init(void);

/* schema generation */
typedef enum rsdbs_schema_col_option
{
	RSDB_SCHEMA_SERIAL,		/* serial/autoincrement id field */
	RSDB_SCHEMA_SERIAL_REF,		/* reference to a serial */
	RSDB_SCHEMA_BOOLEAN,		/* boolean */
	RSDB_SCHEMA_INT,		/* integer */
	RSDB_SCHEMA_UINT,		/* unsigned integer */
	RSDB_SCHEMA_VARCHAR,		/* varchar */
	RSDB_SCHEMA_CHAR,		/* char */
	RSDB_SCHEMA_TEXT,		/* text */
}
rsdbs_schema_col_option;

typedef enum rsdbs_schema_key_option
{
	RSDB_SCHEMA_KEY_PRIMARY,	/* PRIMARY KEY */
	RSDB_SCHEMA_KEY_UNIQUE,		/* UNIQUE constraint */
	RSDB_SCHEMA_KEY_INDEX,		/* normal INDEX */
	RSDB_SCHEMA_KEY_F_MATCH,	/* FOREIGN KEY -- MATCH FULL */
	RSDB_SCHEMA_KEY_F_CASCADE	/* FOREIGN KEY -- CASCADE DELETE */
}
rsdbs_schema_key_option;

struct rsdbs_schema_col
{
	rsdbs_schema_col_option option;
	unsigned int length;
	unsigned int not_null;
	const char *name;
	const char *def;
};

struct rsdbs_schema_key
{
	rsdbs_schema_key_option option;
	unsigned int length;
	unsigned int not_null;
	const char *name;
	const char *def;
};


struct rsdb_schema_set
{
	const char *table_name;
	struct rsdbs_schema_col *schema_col;
	struct rsdbs_schema_key *schema_key;
	int has_serial;
	int dropped_primary_key;
};

const char *rsdbs_sql_check_table(const char *table_name);
const char *rsdbs_sql_create_col(struct rsdb_schema_set *schema_set, struct rsdbs_schema_col *schema_element,
					int alter_table);
const char *rsdbs_sql_create_key(struct rsdb_schema_set *schema_set, struct rsdbs_schema_key *schema_element);
const char *rsdbs_sql_drop_key_pri(const char *table_name);
const char *rsdbs_sql_drop_key_unique(const char *table_name, const char *key_name);
const char *rsdbs_sql_drop_key_index(const char *table_name, const char *key_name);

int rsdbs_check_column(const char *table_name, const char *column_name);
int rsdbs_check_key_pri(const char *table_name, const char *key_list_str);
int rsdbs_check_key_unique(const char *table_name, const char *key_list_str);
int rsdbs_check_key_index(const char *table_name, const char *key_list_str);

void rsdbs_check_deletekey_unique(const char *table_name, dlink_list *unique_list, dlink_list *table_data);
void rsdbs_check_deletekey_index(const char *table_name, dlink_list *unique_list, dlink_list *table_data);

/* rsdbs_common.c */
void rsdb_schema_check(struct rsdb_schema_set *schema_set);

struct _dlink_list *rsdb_schema_split_key(const char *key_fields);

struct _dlink_list *rsdb_schema_generate_table(struct rsdb_schema_set *schema_set);
const char *rsdbs_generate_key_name(const char *table_name, const char *field_list_text, rsdbs_schema_key_option option);

#endif
