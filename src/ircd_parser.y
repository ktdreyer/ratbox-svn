/* This code is in the public domain.
 * $Nightmare: nightmare/src/main/parser.y,v 1.2.2.1.2.1 2002/07/02 03:42:10 ejb Exp $
 * $Id$
 */

%{
#include <sys/types.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "stdinc.h"
#include "setup.h"
#include "common.h"
#include "ircd_defs.h"
#include "config.h"
#include "client.h"
#include "modules.h"
#include "newconf.h"

#define YY_NO_UNPUT

int yyparse();
int yyerror(char *);
int yylex();

static time_t conf_find_time(char*);

static struct {
	char *	name;
	char *	plural;
	time_t 	val;
} times[] = {
	{"second",     "seconds",    1},
	{"minute",     "minutes",    60},
	{"hour",       "hours",      60 * 60},
	{"day",        "days",       60 * 60 * 24},
	{"week",       "weeks",      60 * 60 * 24 * 7},
	{"fortnight",  "fortnights", 60 * 60 * 24 * 14},
	{"month",      "months",     60 * 60 * 24 * 7 * 4},
	{"year",       "years",      60 * 60 * 24 * 365},
	/* ok-- we now do sizes here too. they aren't times, but 
	   it's close enough */
	{"byte",	"bytes",	1},
	{"kb",		NULL,		1024},
	{"kbyte",	"kbytes",	1024},
	{"kilobyte",	"kilebytes",	1024},
	{"mb",		NULL,		1024 * 1024},
	{"mbyte",	"mbytes",	1024 * 1024},
	{"megabyte",	"megabytes",	1024 * 1024},
	{NULL},
};

time_t conf_find_time(char *name)
{
  int i;

  for (i = 0; times[i].name; i++)
    {
      if (strcasecmp(times[i].name, name) == 0 ||
	  (times[i].plural && strcasecmp(times[i].plural, name) == 0))
	return times[i].val;
    }

  return 0;
}

static struct
{
	char *word;
	int yesno;
} yesno[] = {
	{"yes",		1},
	{"no",		0},
	{"true",	1},
	{"false",	0},
	{"on",		1},
	{"off",		0},
	{NULL,		0}
};

static int	conf_get_yesno_value(char *str)
{
	int i;

	for (i = 0; yesno[i].word; i++)
	{
		if (strcasecmp(str, yesno[i].word) == 0)
		{
			return yesno[i].yesno;
		}
	}

	return -1;
}

conf_parm_t *	cur_list = NULL;

%}

%union {
	int 		number;
	char 		string[IRCD_BUFSIZE + 1];
	conf_parm_t *	conf_parm;
}

%token LOADMODULE

%token <string> QSTRING STRING
%token <number> NUMBER

%type <string> qstring string
%type <number> number
%type <number> timespec
%type <conf_parm> itemlist oneitem

%start conf

%%

conf: 
    | conf conf_item
    ;

conf_item:
         | block
	 | loadmodule
         ;

block: string 
         { 
           conf_start_block($1, NULL);
         }
       '{' block_items '}' ';' 
         {
	   if (conf_cur_block)
           	conf_end_block(conf_cur_block);
         }
     | string qstring 
         { 
           conf_start_block($1, $2);
         }
       '{' block_items '}' ';'
         {
	   if (conf_cur_block)
           	conf_end_block(conf_cur_block);
         }
     ;

block_items: block_items block_item 
           | block_item 
           ;

block_item:	string '=' itemlist ';'
		{
			conf_call_set(conf_cur_block, $1, cur_list, CF_LIST);
			cur_list = NULL;
		}
		;

itemlist: itemlist ',' oneitem 
	{
		/* add this item to the list ($1) */
		if (cur_list == NULL)
		{
			cur_list = MyMalloc(sizeof(conf_parm_t));
			memset(cur_list, 0, sizeof(conf_parm_t));
			cur_list->v.list = $3;
			cur_list->type |= CF_FLIST;
		}
		else
		{
			$3->next = cur_list->v.list;
			cur_list->v.list = $3;
			cur_list->type |= CF_FLIST;
		}
	}
	| oneitem
	{
		if (cur_list == NULL)
		{
			cur_list = MyMalloc(sizeof(conf_parm_t));
			memset(cur_list, 0, sizeof(conf_parm_t));
			cur_list->v.list = $1;
		}
		else
		{
			$1->next = cur_list->v.list;
			cur_list->v.list = $1;
		}
	}
	;

oneitem: qstring
            {
		$$ = MyMalloc(sizeof(conf_parm_t));
		memset($$, 0, sizeof(conf_parm_t));
		$$->type = CF_QSTRING;
		DupString($$->v.string, $1);
	    }
          | timespec
            {
		$$ = MyMalloc(sizeof(conf_parm_t));
		memset($$, 0, sizeof(conf_parm_t));
		$$->type = CF_TIME;
		$$->v.number = $1;
	    }
          | number
            {
		$$ = MyMalloc(sizeof(conf_parm_t));
		memset($$, 0, sizeof(conf_parm_t));
		$$->type = CF_INT;
		$$->v.number = $1;
	    }
          | string
            {
		/* a 'string' could also be a yes/no value .. 
		   so pass it as that, if so */
		int val = conf_get_yesno_value($1);

		$$ = MyMalloc(sizeof(conf_parm_t));
		memset($$, 0, sizeof(conf_parm_t));

		if (val != -1)
		{
			$$->type = CF_YESNO;
			$$->v.number = val;
		}
		else
		{
			$$->type = CF_STRING;
			DupString($$->v.string, $1);
		}
            }
          ;

loadmodule:
	  LOADMODULE QSTRING
            {
	      load_one_module($2);
	    }
	  ';'
          ;

qstring: QSTRING { strcpy($$, $1); } ;
string: STRING { strcpy($$, $1); } ;
number: NUMBER { $$ = $1; } ;

timespec: number
          {
            $$ = $1;
          }
        | number string
          {
	    time_t t;

	    if ((t = conf_find_time($2)) == 0)
	      {
		conf_report_error("Unrecognised time type '%s'", $2);
		t = 1;
	      }
	    
	    $$ = $1 * t;
	  }
          | timespec timespec
          {
            $$ = $1 + $2;
          }
          ;
