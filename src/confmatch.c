/*
 *  ircd-ratbox: A slightly useful ircd
 *  confmatch.c: Matches config entries with hostnames and IP addresses
 *
 *  Copyright (C) 2002 Aaron Sethman <androsyn@ratbox.org>
 *  Copyright (C) 2002 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "memory.h"
#include "ircd_defs.h"
#include "s_conf.h"
#include "numeric.h"
#include "send.h"
#include "irc_string.h"
#include "iplines.h"
#include "confmatch.h"
#include "tools.h"

static dlink_list hostconf[ATABLE_SIZE];


void init_confmatch(void)
{
	memset(hostconf, 0, sizeof(hostconf));
}

static int 
wildcard_to_cidr(const char *host, struct irc_inaddr *in, int *cidr)
{
	uint32_t current_ip=0L;
	unsigned int octet=0;
	int dot_count=0;
	char c;
	
#ifdef IPV6
	uint32_t *ip = &((uint32_t *)PIN_ADDR(in))[3];
	((uint32_t *)PIN_ADDR(in))[0] = 0;
	((uint32_t *)PIN_ADDR(in))[1] = 0;
	((uint32_t *)PIN_ADDR(in))[2] = htonl(0xffff);
#else
	uint32_t *ip = PIN_ADDR(in);
#endif	

	while( (c = *host))
	{
		if(IsDigit(c))
		{
			octet *= 10;
			octet += (*host & 0xF);
		}
		else if(c == '.')
		{
			current_ip <<= 8;   
			current_ip += octet;
			if( octet > 255 )
				return( 0 );
			octet = 0;  
			dot_count++;
		}
		else if(c == '*' && (*(host+1) == '\0' || *(host+1) == '.') && dot_count > 0)
		{
			current_ip <<= 32-(8*dot_count);
			*ip = current_ip;
			*cidr = 8*dot_count;
			return( 1 );
		} 
		else
        		return( 0 );
      		host++;
    	}

	return 0;
}

static int
hash_text (const char *start)
{
	const char *p = start;
	unsigned long h = 0;

	while (*p)
	{
		h = (h << 4) - (h + (unsigned char) ToLower (*p++));
	}
	return (h & (ATABLE_SIZE - 1));
}

/* unsigned long get_hash_mask(const char *)
 * Input: The text to hash.
 * Output: The hash of the string right of the first '.' past the last
 *         wildcard in the string.
 * Side-effects: None.
 */
static unsigned long
get_mask_hash (const char *text)
{
	const char *hp = "", *p;

	for (p = text + strlen (text) - 1; p >= text; p--)
	{
		if(*p == '*' || *p == '?')
			return hash_text (hp);
		else if(*p == '.')
			hp = p + 1;
	}
	return hash_text (text);
}


int
parse_netmask(const char *address, struct irc_inaddr *addr, int *bits)
{
	char *p;
	struct irc_inaddr laddr;
	int lbits;

	p = strchr(address, '/');
	
	if(addr == NULL)
		addr = &laddr;
	if(bits == NULL)
		bits = &lbits;
		
#ifdef IPV6
	*bits = 128;
#else
	*bits = 32;
#endif


	if(p != NULL)
	{
		*p = '\0';
		*bits = strtol(++p, NULL, 10);
		if(*bits == 0 && errno == EINVAL)
			return 0;
#ifdef IPV6
		/* Add 96 bits to our IPv4 masks */
		if(IN6_IS_ADDR_V4MAPPED(PIN_ADDR(addr)))
			*bits += 96;	
#endif
		
	} 
	else if((p = strchr(address, '*')) != NULL)
	{
		if(wildcard_to_cidr(address, addr, bits))
		{
#ifdef IPV6
			*bits += 96;			
#endif
			return 1;
		}
	}
	
	if(inetpton(DEF_FAM, address, addr) == 0)
	{
		return 0;
	}
	return 1;
}

static void
add_conf_by_host(struct ConfItem *aconf)
{
	dlink_list *list;
	int lmask;
	lmask = get_mask_hash(aconf->host);	
	list = &hostconf[lmask];
	dlinkAddAlloc(aconf, list);
}

void
add_conf_by_address (const char *address, int type, const char *username, struct ConfItem *aconf)
{
	struct irc_inaddr addr;
	int bits;
	if(parse_netmask(address, &addr, &bits))
	{
		/* An IP/cidr mask */
		add_ipline(aconf, type, &addr, bits);
		return;		
	}

	add_conf_by_host(aconf);
}


struct ConfItem *  
find_conf_by_address (const char *hostname,
		      struct irc_inaddr *addr, int type, const char *username)
{
	struct ConfItem *aconf;
	const char *p = hostname;
	dlink_list *list;
	dlink_node *ptr;
	int x;
	if(username == NULL)
		username = "";
	
	
	if(hostname != NULL)
	{
		while(p != NULL)
		{
			list = &hostconf[hash_text(p)];
			DLINK_FOREACH(ptr, list->head)
			{
				aconf = ptr->data;
				if(match(aconf->user, username) && match(aconf->host, hostname))
					return(aconf);
			}	

			p = strchr(p, '.');
			if(p != NULL)
				p++;
			else
				break;
		}	
		
		/* Life is miserable..we go to a linear scan now */
		for(x = 0; x < ATABLE_SIZE; x++)
		{
			list = &hostconf[x];
			DLINK_FOREACH(ptr, list->head)
			{
				aconf = ptr->data;
				if(match(aconf->user, username) && match(aconf->host, hostname))
					return(aconf);
			}
		}
	}
	
	if(addr != NULL)
	{
		aconf = find_generic_line(type, addr);
		if(aconf != NULL)
		{
			if(match(aconf->user, username))
				return aconf;
		}
	}
	return NULL;

}


struct ConfItem *
find_dline(struct irc_inaddr *ip)
{
	return(find_ipdline(ip));
}


struct ConfItem *
find_gline(struct Client *client_p)
{
	return(find_conf_by_address (client_p->host,
				&client_p->localClient->ip, CONF_GLINE,
				client_p->username));
}

struct ConfItem *
find_kline(struct Client *client_p)
{
	return(find_conf_by_address (client_p->host,
				&client_p->localClient->ip, CONF_KILL,
				client_p->username));
}


struct ConfItem *
find_address_conf (const char *host, const char *user, struct irc_inaddr *ip)
{
	struct ConfItem *iconf, *kconf;

	iconf = find_conf_by_address(host, ip, CONF_CLIENT, user);
	if(iconf == NULL)
		return NULL;
	
	if(IsConfExemptKline(iconf))
		return(iconf);
	
	kconf = find_conf_by_address (host, ip, CONF_KILL, user);
	
	if(ConfigFileEntry.glines)
	{
		kconf = find_conf_by_address (host, ip, CONF_GLINE, user);
		if((kconf != NULL) && !IsConfExemptGline (iconf))
			return(kconf);
	}
	return(iconf);
}

void
delete_one_address_conf(struct ConfItem *aconf)
{
	struct irc_inaddr addr;
	int bits, lmask;
	dlink_list *list;
	if(parse_netmask(aconf->host, &addr, &bits))
	{
		/* An IP line */
		delete_ipline(aconf, aconf->status);		
		return;
	}
	
	lmask = hash_text(aconf->host);
	list = &hostconf[lmask];
	
	dlinkFindDestroy(list, aconf);
	aconf->status |= CONF_ILLEGAL;
	if(!aconf->clients)
		free_conf(aconf);
}

void clear_out_address_conf(void)
{
	int x;
	dlink_list *list;
	dlink_node *ptr, *tmp;
	struct ConfItem *aconf;
	for(x = 0; x < ATABLE_SIZE; x++)
	{
		list = &hostconf[x];
		DLINK_FOREACH_SAFE(ptr, tmp, list->head)
		{
			aconf = (struct ConfItem *)ptr->data;
			aconf->status |= CONF_ILLEGAL;
			if(!aconf->clients)
				free_conf(aconf);

			dlinkDestroy(ptr, list);
		}
	}	
}

char *
show_iline_prefix(struct Client *sptr, struct ConfItem *aconf, char *name)
{
  static char prefix_of_host[USERLEN + 15];
  char *prefix_ptr;

  prefix_ptr = prefix_of_host;
  if (IsNoTilde(aconf))
    *prefix_ptr++ = '-';
  if (IsLimitIp(aconf)) 
    *prefix_ptr++ = '!';
  if (IsNeedIdentd(aconf))
    *prefix_ptr++ = '+';  
  if (IsPassIdentd(aconf))
    *prefix_ptr++ = '$';  
  if (IsNoMatchIp(aconf)) 
    *prefix_ptr++ = '%';  
  if (IsConfDoSpoofIp(aconf))
    *prefix_ptr++ = '=';
  if (MyOper(sptr) && IsConfExemptKline(aconf))
    *prefix_ptr++ = '^';
  if (MyOper(sptr) && IsConfExemptLimits(aconf))
    *prefix_ptr++ = '>';
  if (MyOper(sptr) && IsConfIdlelined(aconf))
    *prefix_ptr++ = '<';
  *prefix_ptr = '\0';   
  strncpy(prefix_ptr, name, USERLEN);
  return (prefix_of_host);
}
