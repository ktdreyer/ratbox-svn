/************************************************************************
 *   IRC - Internet Relay Chat, src/ircd_parser.y
 *   Copyright (C) 2000 Diane Bruce <db@db.net>
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
#include "s_conf.h"

int yyparse();
	

%}

%union {
  	int  number;
	char *string;
  	struct ip_value ip_entry;
}

%token	INCLUDE
%token  NUMBER
%token  QSTRING
%token	YES
%token	NO
%token  SERVERINFO
%token  DESCRIPTION
%token	ADMIN
%token	CLASS
%token	CLIENT
%token  KLINE_EXEMPT
%token  ALLOW_BOTS
%token  NO_TILDE
%token  HAVE_IDENT
%token	OPERATOR
%token  HOST
%token  KILL
%token  DENY
%token  IP
%token  IP_TYPE
%token  GLOBAL_KILL
%token  REMOTE
%token  UNKLINE
%token  GLINE
%token  NICK_CHANGES
%token  DIE
%token  REASON
%token  REHASH
%token	QUARANTINE
%token	CONNECT
%token  SEND_PASSWORD
%token  ACCEPT_PASSWORD
%token  COMPRESSED
%token  LAZYLINK
%token	NAME
%token	EMAIL
%token	HUB
%token  HUB_MASK
%token  HUB_MASKS
%token  LEAF
%token  LEAF_MASK
%token  LEAF_MASKS
%token	PING_TIME
%token	NUMBER_PER_IP
%token	MAX_NUMBER
%token	SENDQ
%token	PASSWORD
%token	KLINE_EXEMPT
%token  ALLOW
%token	ALLOW_BOTS
%token  LISTEN
%token  PORT
%token  SPOOF
%token  QUARANTINE

%%
conf:
	| conf conf_item
	;

conf_item:	admin_entry  |
		oper_entry   |
		class_entry  |
		listen_entry |
		client_entry |
		serverinfo_entry |
		quarantine_entry |
		connect_entry |
		kill_entry |
		deny_entry
	;

/***************************************************************************
 *  section server
 ***************************************************************************/

serverinfo_entry:	SERVERINFO '{' serverinfo_items '}' ';'

serverinfo_items:	serverinfo_items serverinfo_item |
		serverinfo_item

serverinfo_item:	serverinfo_name | serverinfo_description |
		serverinfo_email | serverinfo_hub

serverinfo_name:	NAME '=' QSTRING ';'  { sendto_realops("server.name [%s]",yylval.string); };

serverinfo_email:	EMAIL '=' QSTRING ';'  { sendto_realops("server.email [%s]",yylval.string); };

serverinfo_description:	DESCRIPTION '=' QSTRING ';'  { sendto_realops("server.description [%s]",yylval.string); };

serverinfo_hub:	HUB '=' YES ';'  { sendto_realops("server is hub"); } |
		HUB '=' NO ';'   { sendto_realops("server is leaf"); } ;

/***************************************************************************
 * admin section
 ***************************************************************************/

admin_entry:	ADMIN '{' admin_items '}' ';'

admin_items:	admin_items admin_item |
		admin_item

admin_item:	admin_name |
		admin_email 

admin_name:	NAME '=' QSTRING ';'  { sendto_realops("admin.name [%s]",yylval.string); };

admin_email:	EMAIL '=' QSTRING ';' { sendto_realops("admin_email [%s]",yylval.string); };

/***************************************************************************
 * oper section
 ***************************************************************************/

oper_entry:	OPERATOR '{' oper_items '}' ';'

oper_items:	oper_items oper_item |
		oper_item

oper_item:	oper_name | oper_host | oper_password | oper_class | 
		oper_global_kill | oper_remote | oper_unkline | oper_gline |
		oper_nick_changes | oper_die | oper_rehash

oper_name:	NAME '=' QSTRING ';' { sendto_realops("oper.name [%s]", yylval.string); };

oper_host:	HOST '=' QSTRING ';' { sendto_realops("oper.host [%s]", yylval.string); };

oper_password:	PASSWORD '=' QSTRING ';' { sendto_realops("oper.host [%s]", yylval.string); };

oper_global_kill: GLOBAL_KILL '=' YES ';' {sendto_realops("oper can global kill");}|
	GLOBAL_KILL '=' NO ';' {sendto_realops("oper can't global kill"); } ;

oper_remote: REMOTE '=' YES ';' {sendto_realops("oper can do remote ops");}|
	REMOTE '=' NO ';' {sendto_realops("oper can't do remote ops"); } ;

oper_unkline: UNKLINE '=' YES ';' {sendto_realops("oper can unkline");}|
	UNKLINE '=' NO ';' {sendto_realops("oper can't unkline"); } ;

oper_gline: GLINE '=' YES ';' {sendto_realops("oper can gline");}|
	GLINE '=' NO ';' {sendto_realops("oper can't gline"); } ;

oper_nick_changes: NICK_CHANGES '=' YES ';' {sendto_realops("oper can see nick changes");}|
	NICK_CHANGES '=' NO ';' {sendto_realops("oper can't see nick changes"); } ;

oper_die: DIE '=' YES ';' {sendto_realops("oper can die");}|
	DIE '=' NO ';' {sendto_realops("oper can't die"); } ;

oper_rehash: REHASH '=' YES ';' {sendto_realops("oper can rehash");}|
	REHASH '=' NO ';' {sendto_realops("oper can't rehash"); } ;

oper_class:	CLASS '=' QSTRING ';' {sendto_realops("oper.class [%s]", yylval.string); };


/***************************************************************************
 *  section class
 ***************************************************************************/

class_entry:	CLASS '{' class_items '}' ';'

class_items:	class_items class_item |
		class_item

class_item:	class_name |
		class_ping_time |
		class_number_per_ip |
		class_max_number |
		class_sendq

class_name:	NAME '=' QSTRING ';'  { sendto_realops("class.name [%s]",yylval.string); };

class_ping_time:	PING_TIME '=' NUMBER ';' { sendto_realops("ping_time %d",yylval.number); };

class_number_per_ip:	NUMBER_PER_IP '=' NUMBER ';' { sendto_realops("number_per_ip [%d]",yylval.number); };

class_max_number:	MAX_NUMBER '=' NUMBER ';' { sendto_realops("max_number [%d]",yylval.number); };

class_sendq:	SENDQ '=' NUMBER ';' { sendto_realops("sendq [%d]",yylval.number); };

/***************************************************************************
 *  section listen
 ***************************************************************************/

listen_entry:	LISTEN '{' listen_items '}' ';'

listen_items:	listen_items listen_item |
		listen_item

listen_item:	listen_name | listen_port 

listen_name:	NAME '=' QSTRING ';'  { sendto_realops("listen.name [%s]",yylval.string); };

listen_port:	PORT '=' NUMBER ';'  { sendto_realops("listen.port [%d]",yylval.number); };

/***************************************************************************
 *  section client
 ***************************************************************************/

client_entry:	CLIENT '{' client_items '}' ';'

client_items:	client_items client_item |
		client_item

client_item:	client_allow | client_passwd | client_class |
		client_kline_exempt | client_allow_bots | client_have_ident |
		client_no_tilde | client_spoof

client_allow:	ALLOW '=' QSTRING ';' { sendto_realops("client.allow [%s]",yylval.string); } | ALLOW '=' IP_TYPE ';' {sendto_realops("client.allow IP_TYPE [%X] [%X]", yylval.ip_entry.ip, yylval.ip_entry.ip_mask); } ;

client_passwd:	PASSWORD '=' QSTRING ';'  { sendto_realops("client.password [%s]",yylval.string); };

client_spoof:	SPOOF '=' QSTRING ';'  { sendto_realops("client.spoof [%s]",yylval.string); };

client_kline_exempt:	KLINE_EXEMPT '=' YES ';'  { sendto_realops("client is exempt from klines"); } |
  KLINE_EXEMPT '=' NO ';' {sendto_realops("client is not exempt from klines"); } ;

client_allow_bots:	ALLOW_BOTS '=' YES ';'  { sendto_realops("client is allowed to run bots"); } |
  ALLOW_BOTS '=' NO ';' {sendto_realops("client is not allowed to run bots"); } ;

client_have_ident:	HAVE_IDENT ';'  { sendto_realops("client must have identd"); };

client_no_tilde:	NO_TILDE ';'  { sendto_realops("client never has tilde"); };
client_class:	CLASS '=' QSTRING ';'  { sendto_realops("client.name [%s]",yylval.string); };

/***************************************************************************
 *  section quarantine
 ***************************************************************************/

quarantine_entry:	QUARANTINE '{' quarantine_items '}' ';'

quarantine_items:	quarantine_items quarantine_item |
		quarantine_item

quarantine_item:	quarantine_name | quarantine_reason

quarantine_name:	NAME '=' QSTRING ';'  { sendto_realops("quarantine.name [%s]",yylval.string); };

quarantine_reason:	REASON '=' QSTRING ';'  { sendto_realops("quarantine.reason [%s]",yylval.string); };

/***************************************************************************
 *  section connect
 ***************************************************************************/

connect_entry:	CONNECT '{' connect_items '}' ';'

connect_items:	connect_items connect_item |
		connect_item

connect_item:	connect_name | connect_host | connect_send_password |
		connect_accept_password | connect_port |
		connect_compressed | connect_lazylink |
		connect_hub_mask | connect_class

connect_name:	NAME '=' QSTRING ';'  { sendto_realops("connect.name [%s]",yylval.string); };

connect_host:	HOST '=' QSTRING ';'  { sendto_realops("connect.host [%s]",yylval.string); };

connect_send_password:	SEND_PASSWORD '=' QSTRING ';'  { sendto_realops("connect.send_password [%s]",yylval.string); };

connect_accept_password:	ACCEPT_PASSWORD '=' QSTRING ';'  { sendto_realops("connect.accept_password [%s]",yylval.string); };

connect_port:	PORT '=' NUMBER ';'  { sendto_realops("connect.port [%d]",yylval.number); };

connect_compressed:	COMPRESSED '=' YES ';'  { sendto_realops("connect compressed YES"); } |
		COMPRESSED '=' NO ';'   { sendto_realops("connect compressed NO"); } ;

connect_lazylink:	LAZYLINK '=' YES ';'  { sendto_realops("connect lazylink YES"); } |
		LAZYLINK '=' NO ';'   { sendto_realops("connect lazylink NO"); } ;

/* connect_hub.masks:	HUB_MASKS  '{' QSTRING ';' '}' */

connect_hub_mask:	HUB_MASK '=' QSTRING ';'  { sendto_realops("connect.hub_mask [%s]",yylval.string); };

connect_class:	CLASS '=' QSTRING ';'  { sendto_realops("connect.class [%s]",yylval.string); };


/***************************************************************************
 *  section kill
 ***************************************************************************/

kill_entry:	KILL '{' kill_items '}' ';'

kill_items:	kill_items kill_item |
		kill_item

kill_item:	kill_name | kill_reason


kill_name:	NAME '=' QSTRING ';'  { sendto_realops("kill.name [%s]",yylval.string); };

kill_reason:	REASON '=' QSTRING ';'  { sendto_realops("kill.name [%s]",yylval.string); };

/***************************************************************************
 *  section deny
 ***************************************************************************/

deny_entry:	DENY '{' deny_items '}' ';'

deny_items:	deny_items deny_item |
		deny_item

deny_item:	deny_ip | deny_reason


deny_ip:	IP '=' IP_TYPE ';'  { sendto_realops("deny.ip IP_TYPE [%X] [%X]",yylval.ip_entry.ip, yylval.ip_entry.ip_mask); };

deny_reason:	REASON '=' QSTRING ';'  { sendto_realops("deny.reason [%s]",yylval.string); };





