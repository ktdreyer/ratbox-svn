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
#include "irc_string.h"

int yyparse();
	
static struct ConfItem *yy_aconf;
static struct ConfItem *yy_cconf;
static struct ConfItem *yy_nconf;
static struct ConfItem *yy_hconf;

char* class_name_var;
int   class_ping_time_var;
int   class_number_per_ip_var;
int   class_max_number_var;
int   class_sendq_var;

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
%token  GLOBAL
%token  USER
%token  HOST
%token  KILL
%token  DENY
%token  IP
%token  IP_TYPE
%token  GLOBAL_KILL
%token  REMOTE
%token  KLINE
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
%token  VHOST

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
 *  section serverinfo
 ***************************************************************************/

serverinfo_entry:	SERVERINFO
  {
    if(yy_aconf)
      {
	free_conf(yy_aconf);
	yy_aconf = NULL;
      }
    yy_aconf=make_conf();
    yy_aconf->status = CONF_ME;
  }
  '{' serverinfo_items '}' ';'
  {
    if(yy_aconf->user)
      {
	conf_add_me(yy_aconf);
	conf_add_conf(yy_aconf);
      }
    else
      free_conf(yy_aconf);
    yy_aconf = NULL;
  } ;


serverinfo_items:	serverinfo_items serverinfo_item |
		serverinfo_item 

serverinfo_item:	serverinfo_name | serverinfo_vhost |
		        serverinfo_hub | serverinfo_description 

serverinfo_name:	NAME '=' QSTRING ';'  {
    DupString(yy_aconf->host,yylval.string);
  };

serverinfo_description: DESCRIPTION '=' QSTRING ';' {
    DupString(yy_aconf->user,yylval.string);
  };

serverinfo_vhost:	VHOST '=' IP_TYPE ';'  {
    yy_aconf->ip = yylval.ip_entry.ip; };

serverinfo_hub:	HUB '=' YES ';' 
  {
    ConfigFileEntry.hub = 1;
  } |
  HUB '=' NO ';'
  {
    ConfigFileEntry.hub = 0;
  } ;

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

oper_entry:	OPERATOR 
  {
    if(yy_aconf)
      {
	free_conf(yy_aconf);
	yy_aconf = NULL;
      }
    yy_aconf=make_conf();
    yy_aconf->status = CONF_LOCOP;
  };
 '{' oper_items '}' ';' {
  if(yy_aconf->name && yy_aconf->passwd && yy_aconf->host)
    {
      conf_add_class_to_conf(yy_aconf);
      conf_add_conf(yy_aconf);
    }
  else
    {
      free_conf(yy_aconf);
    }
  yy_aconf = NULL;
}; 

oper_items:	oper_items oper_item |
		oper_item

oper_item:	oper_name  | oper_user | oper_host | oper_password |
		oper_class | oper_global | oper_global_kill | oper_remote |
		oper_kline | oper_unkline | oper_gline | oper_nick_changes |
		oper_die | oper_rehash 

oper_name:	NAME '=' QSTRING ';' {
  DupString(yy_aconf->name,yylval.string);
};

oper_user:	USER '=' QSTRING ';' {
  DupString(yy_aconf->user,yylval.string);
};

oper_host:	HOST '=' QSTRING ';' {
  DupString(yy_aconf->host,yylval.string);
};

oper_password:	PASSWORD '=' QSTRING ';' {
  DupString(yy_aconf->passwd,yylval.string);
};

oper_class:	CLASS '=' QSTRING ';' {
  DupString(yy_aconf->className,yylval.string);
};

oper_global:	GLOBAL '=' YES ';' {yy_aconf->status = CONF_OPERATOR;} |
		GLOBAL '=' NO ';' {yy_aconf->status = CONF_LOCOP;} ;

oper_global_kill: GLOBAL_KILL '=' YES ';' {
	yy_aconf->port |= CONF_OPER_GLOBAL_KILL;}|
	GLOBAL_KILL '=' NO ';' {
	yy_aconf->port &= ~CONF_OPER_GLOBAL_KILL;} ;

oper_remote: REMOTE '=' YES ';' {
	yy_aconf->port |= CONF_OPER_REMOTE;}|
	REMOTE '=' NO ';' {
	yy_aconf->port &= ~CONF_OPER_REMOTE; } ;

oper_kline: KLINE '=' YES ';' {
	yy_aconf->port |= CONF_OPER_K;}|
	KLINE '=' NO ';' {
	yy_aconf->port &= ~CONF_OPER_K; } ;

oper_unkline: UNKLINE '=' YES ';' {
	yy_aconf->port |= CONF_OPER_UNKLINE;}|
	UNKLINE '=' NO ';' {
	yy_aconf->port &= ~CONF_OPER_UNKLINE; } ;

oper_gline: GLINE '=' YES ';' {
	yy_aconf->port |= CONF_OPER_GLINE;}|
	GLINE '=' NO ';' {
	yy_aconf->port &= ~CONF_OPER_GLINE; } ;

oper_nick_changes: NICK_CHANGES '=' YES ';' {
	yy_aconf->port |= CONF_OPER_N;}|
	NICK_CHANGES '=' NO ; {
	yy_aconf->port &= ~CONF_OPER_N;} ;

oper_die: DIE '=' YES ';' {
	yy_aconf->port |= CONF_OPER_DIE; }|
	DIE '=' NO ';' {
	yy_aconf->port &= ~CONF_OPER_DIE; } ;

oper_rehash: REHASH '=' YES ';' {
	yy_aconf->port |= CONF_OPER_REHASH;}|
	REHASH '=' NO ';' {
	yy_aconf->port &= ~CONF_OPER_REHASH; } ;


/***************************************************************************
 *  section class
 ***************************************************************************/

class_entry:	CLASS 
  {
    if(class_name_var)
      MyFree(class_name_var);

    class_name_var = (char *)NULL;
    class_ping_time_var = 0;
    class_number_per_ip_var = 0;
    class_max_number_var = 0;
    class_sendq_var = 0;
  };
  '{' class_items '}' ';' {

  add_class(class_name_var,class_ping_time_var,
	    class_number_per_ip_var, class_max_number_var,
	    class_sendq_var );

  MyFree(class_name_var);
  class_name_var = (char *)NULL;
};

class_items:	class_items class_item |
		class_item

class_item:	class_name |
		class_ping_time |
		class_number_per_ip |
		class_max_number |
		class_sendq

class_name:	NAME '=' QSTRING ';'  {
  DupString(class_name_var,yylval.string); };

class_ping_time:	PING_TIME '=' NUMBER ';' {
  class_ping_time_var = yylval.number; };

class_number_per_ip:	NUMBER_PER_IP '=' NUMBER ';' {
  class_number_per_ip_var = yylval.number; };

class_max_number:	MAX_NUMBER '=' NUMBER ';' {
  class_max_number_var = yylval.number; };

class_sendq:	SENDQ '=' NUMBER ';' {
  class_sendq_var = yylval.number; };

/***************************************************************************
 *  section listen
 ***************************************************************************/

listen_entry:	LISTEN 
  {
    if(yy_aconf)
      {
	free_conf(yy_aconf);
	yy_aconf = NULL;
      }
    yy_aconf=make_conf();
    yy_aconf->status = CONF_LISTEN_PORT;
    DupString(yy_aconf->passwd,"*");
  };
 '{' listen_items '}' ';' {
  conf_add_port(yy_aconf);
  free_conf(yy_aconf);
  yy_aconf = NULL;
 }; 

listen_items:	listen_items listen_item |
		listen_item

listen_item:	listen_name | listen_port | listen_address

listen_name:	NAME '=' QSTRING ';'  {
    DupString(yy_aconf->host,yylval.string) ; 
  };

listen_port:	PORT '=' NUMBER ';'  {
    yy_aconf->port = yylval.number;
  };

listen_address:	IP '=' QSTRING ';' {
  DupString(yy_aconf->passwd,yylval.string);
  };

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

quarantine_entry:	QUARANTINE
  {
    if(yy_aconf)
      {
	free_conf(yy_aconf);
	yy_aconf = NULL;
      }
    yy_aconf=make_conf();
    yy_aconf->status = CONF_QUARANTINED_NICK;
  };
 '{' quarantine_items '}' ';' {
  conf_add_q_line(yy_aconf);
  yy_aconf = NULL;
 }; 

quarantine_items:	quarantine_items quarantine_item |
		quarantine_item

quarantine_item:	quarantine_name | quarantine_reason

quarantine_name:	NAME '=' QSTRING ';'  {
  DupString(yy_aconf->host,yylval.string); };

quarantine_reason:	REASON '=' QSTRING ';'  {
  DupString(yy_aconf->passwd,yylval.string); };

/***************************************************************************
 *  section connect
 ***************************************************************************/

connect_entry:	CONNECT   
  {
    if(yy_cconf)
      {
	free_conf(yy_cconf);
	yy_cconf = NULL;
      }

    if(yy_nconf)
      {
	free_conf(yy_nconf);
	yy_nconf = NULL;
      }

    if(yy_hconf)
      {
	free_conf(yy_hconf);
	yy_hconf = NULL;
      }

    yy_cconf=make_conf();
    yy_nconf=make_conf();
    yy_hconf=make_conf();
    yy_cconf->status = CONF_CONNECT_SERVER;
    yy_nconf->status = CONF_NOCONNECT_SERVER;
    yy_hconf->status = CONF_HUB;
  };
  '{' connect_items '}' ';' {
    if(yy_cconf->host && yy_cconf->passwd && yy_cconf->user)
      {
	++ccount;
	conf_add_server(yy_cconf,ncount,ccount);
	conf_add_conf(yy_cconf);
      }
    else
      {
	free_conf(yy_cconf);
      }

    if(yy_nconf->host && yy_nconf->passwd && yy_nconf->user)
      {
	++ncount;
	conf_add_server(yy_nconf,ncount,ccount);
	conf_add_conf(yy_nconf);
      }
    else
      {
	free_conf(yy_nconf);
      }

    if(yy_hconf->host)
      {
	conf_add_hub_or_leaf(yy_hconf);
	conf_add_conf(yy_hconf);
      }
    else
      {
	free_conf(yy_hconf);
      }

    yy_cconf = NULL;
    yy_nconf = NULL;
    yy_hconf = NULL;
  };

connect_items:	connect_items connect_item |
		connect_item

connect_item:	connect_name | connect_host | connect_send_password |
		connect_accept_password | connect_port |
		connect_compressed | connect_lazylink |
		connect_hub_mask | connect_class

connect_name:	NAME '=' QSTRING ';'  {
  DupString(yy_cconf->host,yylval.string);
  DupString(yy_nconf->host,yylval.string);
  DupString(yy_hconf->user,yylval.string);
  };

connect_host:	HOST '=' QSTRING ';'  {
  DupString(yy_cconf->user,yylval.string);
  DupString(yy_nconf->user,yylval.string); 
  };

connect_send_password:	SEND_PASSWORD '=' QSTRING ';'  {
  DupString(yy_cconf->passwd,yylval.string);
  };

connect_accept_password:	ACCEPT_PASSWORD '=' QSTRING ';'  {
  DupString(yy_nconf->passwd,yylval.string);
   };

connect_port:	PORT '=' NUMBER ';'  {
  yy_cconf->port = yylval.number; };

connect_compressed:	COMPRESSED '=' YES ';'  {
  yy_cconf->flags |= CONF_FLAGS_ZIP_LINK; } |
		COMPRESSED '=' NO ';'   {
		  yy_cconf->flags &= ~CONF_FLAGS_ZIP_LINK; } ;

connect_lazylink:	LAZYLINK '=' YES ';'  {
  yy_nconf->flags |= CONF_FLAGS_LAZY_LINK; } |
		LAZYLINK '=' NO ';'   {
		  yy_nconf->flags &= ~CONF_FLAGS_LAZY_LINK; } ;

/* connect_hub.masks:	HUB_MASKS  '{' QSTRING ';' '}' */

connect_hub_mask:	HUB_MASK '=' QSTRING ';'  {
  DupString(yy_hconf->host,yylval.string);
  };

connect_class:	CLASS '=' QSTRING ';'  {
  DupString(yy_cconf->className,yylval.string);
  DupString(yy_nconf->className,yylval.string);
 };


/***************************************************************************
 *  section kill
 ***************************************************************************/

kill_entry:	KILL
  {
    if(yy_aconf)
      {
	free_conf(yy_aconf);
	yy_aconf = NULL;
      }
    yy_aconf=make_conf();
    yy_aconf->status = CONF_KILL;
  };
 '{' kill_items '}' ';' {
  if(yy_aconf->user && yy_aconf->passwd && yy_aconf->host)
    {
      conf_add_k_line(yy_aconf);
    }
  else
    {
      free_conf(yy_aconf);
    }
  yy_aconf = NULL;
  }; 

kill_items:	kill_items kill_item |
		kill_item

kill_item:	kill_user | kill_host | kill_reason


kill_user:	USER '=' QSTRING ';'  {
  DupString(yy_aconf->user,yylval.string);
 };

kill_host:	HOST '=' QSTRING ';'  {
  DupString(yy_aconf->host,yylval.string);
 };

kill_reason:	REASON '=' QSTRING ';'  {
  DupString(yy_aconf->passwd,yylval.string);
 };

/***************************************************************************
 *  section deny
 ***************************************************************************/

deny_entry:	DENY '{' deny_items '}' ';'

deny_items:	deny_items deny_item |
		deny_item

deny_item:	deny_ip | deny_reason


deny_ip:	IP '=' IP_TYPE ';'  { sendto_realops("deny.ip IP_TYPE [%X] [%X]",yylval.ip_entry.ip, yylval.ip_entry.ip_mask); };

deny_reason:	REASON '=' QSTRING ';'  { sendto_realops("deny.reason [%s]",yylval.string); };





