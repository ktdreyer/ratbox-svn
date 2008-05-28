/* $Id$ */
#ifndef INCLUDED_rsdb_schema_h
#define INCLUDED_rsdb_schema_h

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
	RSDB_SCHEMA_KEY_UNIQUE,		/* UNIQUE constraint */
	RSDB_SCHEMA_KEY_INDEX		/* normal INDEX */
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
	const char *primary_key;
};

void rsdb_schema_generate(struct rsdb_schema_set *schema_set);

#endif
