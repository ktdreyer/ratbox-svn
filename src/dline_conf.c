/*
 * A slightly nicer version of dline_conf.c
 * Written by Aaron Sethman <androsyn@ratbox.org>
 */

#include "dline_conf.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_log.h"
#include "send.h"
#include "memory.h"
#include "patricia.h"


patricia_tree_t *dline;
patricia_tree_t *eline;
patricia_tree_t *kline;

void zap_Dlines(void)
{
#ifdef IPV6
	dline = New_Patricia(128);
	eline = New_Patricia(128);	
	kline = New_Patricia(128);
#else
	dline = New_Patricia(32);
	eline = New_Patricia(32);
	kline =  New_Patricia(32);
#endif
}

struct ConfItem *match_Dline(unsigned long ip)
{
	patricia_node_t *node;
	unsigned long nip = ntohl(ip);
	node = match_string(eline, inetntoa((char *)&nip));
	if(node != NULL)
		return(NULL); // Well we are an exception..
		
	node = match_string(dline, inetntoa((char *)&nip));
	if(node != NULL)
		return(node->data);
	else
		return(NULL);
}

void clear_Dline_table(void)
{
	zap_Dlines();
}
struct ConfItem *match_ip_Kline(unsigned long ip, const char *name)
{
	patricia_node_t *node;
	unsigned long nip = ntohl(ip);
	node = match_string(kline, inetntoa((char *)&nip));
	if(node != NULL)
		return(node->data);
	return(NULL);
}
void report_ip_Klines(struct Client *sptr)
{

}

void add_Dline(struct ConfItem *conf_ptr)
{
        patricia_node_t *node;
	conf_ptr->status = CONF_DLINE;
	node = make_and_lookup(dline, conf_ptr->host);
	node->data = conf_ptr;
}

void add_Eline(struct ConfItem *conf_ptr)
{
	patricia_node_t *node;
	conf_ptr->status = CONF_DLINE;
	conf_ptr->flags |= CONF_FLAGS_E_LINED;
	node = make_and_lookup(eline, conf_ptr->host);
	node->data = conf_ptr;
}

void add_ip_Kline(struct ConfItem *conf_ptr)
{
	patricia_node_t *node;
	log(L_NOTICE, "Added ipKline: %s\n", conf_ptr->host);
	node = make_and_lookup(kline, conf_ptr->host);
	node->data = conf_ptr;	

}

void report_dlines(struct Client *foo)
{


}