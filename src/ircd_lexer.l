/************************************************************************
 *   IRC - Internet Relay Chat, src/ircd_parser.y
 *   Copyright (C) 2000 Diane Bruce <db@db.net>
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                    University of Oulu, Co Center
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <ctype.h>
#include "s_conf.h"
#include "y.tab.h"

/* int yylineno=1; */

#undef YY_INPUT
#define YY_FATAL_ERROR(msg) conf_yy_fatal_error( msg )
#define YY_INPUT(buf,result,max_size) \
	   if ((result = conf_fbgets( buf, max_size, conf_fbfile_in )) == 0) \
		YY_FATAL_ERROR( "input in flex scanner failed" ); 



%}

ws		[ \t]*
comment		#.*
qstring		\"[^\"\n]*[\"\n]

%%
\n		{ yylineno++; }
{ws}		;
{comment}	;

[0-9]+		{ yylval.number = atoi(yytext); return NUMBER; }
([0-9]{1,3}\.){1,3}[0-9]{1,3}(\/[0-3][0-9])?  {

	if(is_address(yytext,&yylval.ip_entry.ip,&yylval.ip_entry.ip_mask))
	  return( IP_TYPE );
	 }

{qstring}	{ yylval.string = strdup(yytext+1);
		  if(yylval.string[yyleng-2] != '"')
		    fprintf(stderr,"Unterminated character string\n");
		  else
		    yylval.string[yyleng-2] = '\0'; /* remove close quote */
		  return QSTRING;
		}

include		{ return INCLUDE; }
yes		{ return YES; }
no		{ return NO; }
serverinfo	{ return SERVERINFO; }
description	{ return DESCRIPTION; }
administrator	{ return ADMIN; }
admin		{ return ADMIN; }
class		{ return CLASS; }
client		{ return CLIENT; }
operator	{ return OPERATOR; }
global		{ return GLOBAL; }
oper		{ return OPERATOR; }
user		{ return USER;} 
host		{ return HOST; }
kill		{ return KILL; }
deny		{ return DENY; }
global_kill	{ return GLOBAL_KILL; }
remote		{ return REMOTE; }
kline		{ return KLINE; }
unkline		{ return UNKLINE; }
gline		{ return GLINE; }
nick_changes	{ return NICK_CHANGES; }
die		{ return DIE; }
rehash		{ return REHASH; }
quarantine	{ return QUARANTINE; }
connect		{ return CONNECT; }
name		{ return NAME; }
email		{ return EMAIL; }
hub		{ return HUB; }
leaf		{ return LEAF; }
ping_time	{ return PING_TIME; }
listen		{ return LISTEN; }
port		{ return PORT; }
ip		{ return IP; }
number_per_ip	{ return NUMBER_PER_IP; }
max_number	{ return MAX_NUMBER; }
sendq		{ return SENDQ; }
send_password	{ return SEND_PASSWORD; }
accept_password { return ACCEPT_PASSWORD; }
compressed	{ return COMPRESSED; }
lazylink	{ return LAZYLINK; }
hub_mask	{ return HUB_MASK; }
hub_masks	{ return HUB_MASKS; }
leaf_mask	{ return LEAF_MASK; }
leaf_masks	{ return LEAF_MASKS; }
password	{ return PASSWORD; }
passwd		{ return PASSWORD; }
kline_exempt	{ return KLINE_EXEMPT; }
allow_bots	{ return ALLOW_BOTS; }
allow		{ return ALLOW; }
no_tilde	{ return NO_TILDE; }
have_ident	{ return HAVE_IDENT; }
reason		{ return REASON; }
spoof		{ return SPOOF; }
.		{ return yytext[0]; }

%%
