/* $Id$ */
#ifndef INCLUDED_rsdbs_h
#define INCLUDED_rsdbs_h

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

struct rsdb_schema_set
{
	const char *table_name;
	struct rsdb_schema *schema;
	int has_serial;
};

const char *rsdbs_sql_check_table(const char *table_name);
const char *rsdbs_sql_create_element(struct rsdb_schema_set *schema_set, struct rsdb_schema *schema_element,
					int alter_table);
const char *rsdbs_sql_drop_key_pri(const char *table_name);

int rsdbs_check_column(const char *table_name, const char *column_name);
int rsdbs_check_key_pri(const char *table_name, const char *key_list_str);
int rsdbs_check_key_unique(const char *table_name, const char *key_list_str);
int rsdbs_check_key_index(const char *table_name, const char *key_list_str);

/* rsdbs_common.c */
void rsdb_schema_check(struct rsdb_schema_set *schema_set);

void rsdb_schema_debug(const char *table_name, dlink_list *table_data);
struct _dlink_list *rsdb_schema_split_key(const char *key_fields);

void rsdb_schema_generate_table(struct rsdb_schema_set *schema_set);
const char *rsdbs_generate_key_name(const char *table_name, const char *field_list_text, rsdb_schema_option option);

#endif
