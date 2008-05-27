/* $Id$ */
#ifndef INCLUDED_rsdb_schema_h
#define INCLUDED_rsdb_schema_h

typedef enum rsdb_schema_option
{
	RSDB_SCHEMA_SERIAL,
	RSDB_SCHEMA_BOOLEAN,
	RSDB_SCHEMA_INT,
	RSDB_SCHEMA_UINT,
	RSDB_SCHEMA_VARCHAR,
	RSDB_SCHEMA_TEXT
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
