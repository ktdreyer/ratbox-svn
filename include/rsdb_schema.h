/* $Id$ */
#ifndef INCLUDED_rsdb_schema_h
#define INCLUDED_rsdb_schema_h

struct rsdb_schema_set
{
	const char *table_name;
	struct rsdb_schema *schema;
	const char *primary_key;
};

void rsdb_schema_generate(struct rsdb_schema_set *schema_set);

#endif
