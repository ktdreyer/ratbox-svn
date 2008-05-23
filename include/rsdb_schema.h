/* $Id$ */
#ifndef INCLUDED_rsdb_schema_h
#define INCLUDED_rsdb_schema_h

enum rsdb_schema_option
{
	RSDB_SCHEMA_SERIAL,
	RSDB_SCHEMA_BOOLEAN,
	RSDB_SCHEMA_INT,
	RSDB_SCHEMA_UINT,
	RSDB_SCHEMA_VARCHAR,
	RSDB_SCHEMA_TEXT
};

struct rsdb_schema
{
	unsigned int option;
	unsigned int length;
	unsigned int not_null;
	const char *name;
	const char *def;
};

void rsdb_schema_generate(void);

#endif
