/***********************************************************************
 *   ircd-hybrid project - Internet Relay Chat
 *  hostmask.c: A consistant interface to network and DNS host address
 *       parsing and mask matching.
 *  All parts of this program are Copyright(C) 2001(or later).
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * $Id$ 
 */
 
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "ircd_defs.h"
#include "s_conf.h"
#include "hostmask.h"
#include "numeric.h"
#include "send.h"

#ifdef IPV6
static int try_parse_v6_netmask(const char*, struct irc_inaddr*, int*);
static unsigned long hash_ipv6(struct irc_inaddr*, int);
#endif
static int try_parse_v4_netmask(const char*, struct irc_inaddr*, int*);
static unsigned long hash_ipv4(struct irc_inaddr*, int);

/* The mask parser/type determination code... */

/* int try_parse_v6_netmask(const char *, struct irc_inaddr *, int *);
 * Input: An possible IPV6 address as a string.
 * Output: An integer describing whether it is an IPV6 or hostmask,
 *         an address(if it is IPV6), a bitlength(if it is IPV6).
 * Side effects: None
 * Comments: Called from parse_netmask
 */
#ifdef IPV6
static int
try_parse_v6_netmask(const char *text, struct irc_inaddr *addr, int *b)
{
 const char *p;
 char c;
 int d[8]={0,0,0,0,0,0,0,0}, dp=0, nyble=4, finsert=-1, bits=0, deficit=0;
 short dc[8];
 for (p=text; (c=*p); p++)
  if (c >= '0' && c <= '9')
  {
   if (nyble == 0)
    return HM_HOST;
   d[dp] |= (c-'0')<<(4*--nyble);
  }
  else if (c >= 'A' && c <= 'F')
  {
   if (nyble == 0)
    return HM_HOST;
   d[dp] |= (c-'A'+0xA)<<(4*--nyble);
  }
  else if (c >= 'a' && c <= 'f')
  {
   if (nyble == 0)
    return HM_HOST;
   d[dp] |= (c-'a'+0xA)<<(4*--nyble);
  }
  else if (c == ':')
  {
   if (p>text && *(p-1) == ':')
   {
    if (finsert>=0)
     return HM_HOST;
    finsert = dp;
   }
   else
   {
    /* If there were less than 4 hex digits, e.g. :ABC: shift right
     * so we don't interpret it as ABC0 -A1kmm */
    d[dp] = d[dp] >> 4*nyble;
    nyble = 4;
    if (++dp >= 8)
     return HM_HOST;
   }
  }
  else if (c == '*')
  {
   /* * must be last, and * is ambiguous if there is a ::... -A1kmm */
   if (finsert >= 0 || *(p+1)|| dp==0 || *(p-1) != ':')
    return HM_HOST;
   bits = dp*16;
  }
  else if (c == '/')
  {
   char *after;
   dp++;
   if (p > text && *(p-1)==':')
    return HM_HOST;
   bits = strtoul(p+1, &after, 10);
   if (bits==0 || *after)
    return HM_HOST;
   if (bits > dp*4 && !(finsert>=0 && bits<=128))
    return HM_HOST;
   break;
  }
  else
   return HM_HOST;
 if (c == 0)
  dp++;
 if (finsert < 0 && bits == 0)
  bits = dp*16;
 else if (bits == 0)
  bits = 128;
 /* How many bytes are missing? -A1kmm */
 deficit = bits/16+((bits%16)?1:0)-dp;
 /* Now fill in the gaps(from ::) in the copied table... -A1kmm */
 for (dp=0,nyble=0; dp<8; dp++)
 {
  if (nyble == finsert && deficit)
  {
   dc[dp] = 0;
   deficit--;
  }
  else
   dc[dp] = d[nyble++];
 }
 /* Set unused bits to 0... -A1kmm*/
 if (bits < 128 && (bits%16!=0) )
  dc[bits/16] &= ~((1<<(16-bits%16))-1);
 for (dp=bits/16+(bits%16?1:0); dp<8; dp++)
  dc[dp] = 0;
 /* And assign... -A1kmm */
 if (addr)
  for (dp=0; dp<16; dp++)
   addr->sins.sin6.s6_addr[dp] = htonl(dc[dp]);
 if (b)
  *b = bits;
 return HM_IPV6;
}
#endif

/* int try_parse_v4_netmask(const char *, struct irc_inaddr *, int *);
 * Input: An possible IPV4 address as a string.
 * Output: An integer describing whether it is an IPV4 or hostmask,
 *         an address(if it is IPV4), a bitlength(if it is IPV4).
 * Side effects: None
 * Comments: Called from parse_netmask
 */
static int
try_parse_v4_netmask(const char *text, struct irc_inaddr *addr, int *b)
{
 const char *p;
 const char *digits[4];
 unsigned char addb[4];
 int n = 0, bits = 0;
 char c;
 digits[n++] = text;
 for (p=text; (c=*p); p++)
  if (c >= '0' && c <= '9')
   ;
  else if (c == '.')
  {
   if (n >= 4)
    return HM_HOST;
   digits[n++] = p+1;
  }
  else if (c == '*')
  {
   if (*(p+1) || n==0 || *(p-1) != '.')
    return HM_HOST;
   bits = (n-1)*8;
   break;
  }
  else if (c == '/')
  {
   char *after;
   bits = strtoul(p+1, &after, 10);
   if (!bits || *after)
    return HM_HOST;
   if (bits > n*8)
    return HM_HOST;
   break;
  }
  else
   return HM_HOST;
 if (n<4 && bits==0)
  bits = n*8;
 if (bits)
  while (n < 4)
   digits[n++] = "0";
 for (n=0; n<4; n++)
  addb[n] = strtoul(digits[n], NULL, 10);
 if (bits == 0)
  bits = 32;
 /* Set unused bits to 0... -A1kmm*/
 if (bits < 32 && bits%8)
  addb[bits/8] &= ~((1<<(8-bits%8))-1);
 for (n=bits/8+(bits%8?1:0); n<8; n++)
  addb[n] = 0;
 if (addr)
  addr->sins.sin.s_addr =
    htonl(addb[0]<<24 | addb[1]<<16 | addb[2]<<8 | addb[3]);
 if (b)
  *b = bits;
 return HM_IPV4;
}

/* int parse_netmask(const char *, struct irc_inaddr *, int *);
 * Input: A hostmask, or an IPV4/6 address.
 * Output: An integer describing whether it is an IPV4, IPV6 address or a
           hostmask, an address(if it is an IP mask),
           a bitlength(if it is IP mask).
 * Side effects: None
 */
int
parse_netmask(const char *text, struct irc_inaddr *addr, int *b)
{
#ifdef IPV6
 if (strchr(text, ':'))
  return try_parse_v6_netmask(text, addr, b);
#endif
 if (strchr(text, '.'))
  return try_parse_v4_netmask(text, addr, b);
 return HM_HOST;
}

/* The address matching stuff... */
/* int match_ipv6(struct irc_inaddr *, struct irc_inaddr *, int)
 * Input: An IP address, an IP mask, the number of bits in the mask.
 * Output: if match, -1 else 0
 * Side effects: None
 */
#ifdef IPV6
int
match_ipv6(struct irc_inaddr *addr, struct irc_inaddr *mask, int bits)
{
 int i, m, n = bits / 8;
 for (i=0; i<n; i++)
  if (addr->sins.sin6.s6_addr[i] != mask->sins.sin6.s6_addr[i])
   return 0;
 if ((m=bits % 8) == 0)
  return -1;
 if ((addr->sins.sin6.s6_addr[n] & ~((1<<(8-m)) - 1)) ==
     mask->sins.sin6.s6_addr[n])
  return -1;
 return 0;
}
#endif

/* int match_ipv4(struct irc_inaddr *, struct irc_inaddr *, int)
 * Input: An IP address, an IP mask, the number of bits in the mask.
 * Output: if match, -1 else 0
 * Side Effects: None
 */
int
match_ipv4(struct irc_inaddr *addr, struct irc_inaddr *mask, int bits)
{
 if ((ntohl(addr->sins.sin.s_addr)&~((1<<(32-bits))-1))!=
     ntohl(mask->sins.sin.s_addr))
  return 0;
 return -1;
}

/* Hashtable stuff... -A1kmm */
struct AddressRec *atable[ATABLE_SIZE];

void
init_host_hash(void)
{
 memset(&atable, 0, sizeof(atable));
}

/* unsigned long hash_ipv4(struct irc_inaddr*)
 * Input: An IP address.
 * Output: A hash value of the IP address.
 * Side effects: None
 */
static unsigned long
hash_ipv4(struct irc_inaddr *addr, int bits)
{
 unsigned long av = ntohl(addr->sins.sin.s_addr) & ~((1<<(32-bits))-1);
 return (av^(av>>12)^(av>>24)) & (ATABLE_SIZE-1);
}

/* unsigned long hash_ipv6(struct irc_inaddr*)
 * Input: An IP address.
 * Output: A hash value of the IP address.
 * Side effects: None
 */
#ifdef IPV6
static unsigned long
hash_ipv6(struct irc_inaddr *addr, int bits)
{
 unsigned long v = 0, n;
 for (n=0; n<16; n++)
 {
  if (bits >= 8)
  {
   v ^= addr->sins.sin6.s6_addr[n];
   bits -= 8;
  }
  else if (bits)
  {
   v ^= addr->sins.sin6.s6_addr[n] & ~((1<<(8-bits))-1);
   return v & (ATABLE_SIZE-1);
  }
  else
   return v & (ATABLE_SIZE-1);
 }
 return v & (ATABLE_SIZE-1);
}
#endif

/* int hash_text(const char *start)
 * Input: The start of the text to hash.
 * Output: The hash of the string between 1 and (TH_MAX-1)
 * Side-effects: None.
 */
static int
hash_text(const char* start)
{
  register const char *p = start;
  register unsigned long h = 0;
  while (*p)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*p++));
    }
  return(h & (ATABLE_SIZE - 1));
}

/* unsigned long get_hash_mask(const char *)
 * Input: The text to hash.
 * Output: The hash of the string right of the first '.' past the last
 *         wildcard in the string.
 * Side-effects: None.
 */
static unsigned long
get_mask_hash(const char *text)
{
 const char *hp = "", *p;
 for (p=text+strlen(text); p>=text; p--)
  if (*p == '*' || *p == '?')
   return hash_text(hp);
  else if (*p == '.')
   hp = p+1;
 return hash_text(text);
}

/* struct ConfItem* find_conf_by_address(const char*, struct irc_inaddr*,
 *         int type, int fam, const char *username)
 * Input: The hostname, the address, the type of mask to find, the address
 *        family, the username.
 * Output: The matching value with the highest precedence.
 * Side-effects: None
 * Note: Setting bit 0 of the type means that the username is ignored.
 */
struct ConfItem*
find_conf_by_address(const char *name, struct irc_inaddr *addr, int type,
                     int fam, const char *username)
{
 unsigned long hprecv = 0;
 struct ConfItem *hprec = NULL;
 struct AddressRec *arec;
 int b;
 if (username == NULL)
  username = "";
 if (addr)
 {
  /* Check for IPV6 matches... */
#ifdef IPV6
  if (fam == AF_INET6)
  {
   for (b=128; b>=0; b-=16)
   {
    for (arec = atable[hash_ipv6(addr, b)]; arec; arec=arec->next)
     if (arec->type == (type&~0x1) &&
         arec->masktype == HM_IPV6 &&
         match_ipv6(addr, &arec->Mask.ipa.addr, arec->Mask.ipa.bits) &&
         (type&0x1 || match(arec->username, username)) &&
         arec->precedence > hprecv
        )
      {
       hprecv = arec->precedence;
       hprec = arec->aconf;
      }
   }
  } else
#endif
  if (fam == AF_INET)
  {
   for (b=32; b>=0; b-=8)
   {
    for (arec = atable[hash_ipv4(addr, b)]; arec; arec=arec->next)
     if (arec->type == (type&~0x1) &&
         arec->masktype == HM_IPV4 &&
         match_ipv4(addr, &arec->Mask.ipa.addr, arec->Mask.ipa.bits) &&
         (type&0x1 || match(arec->username, username)) &&
         arec->precedence > hprecv
        )
      {
       hprecv = arec->precedence;
       hprec = arec->aconf;
      }
   }
  }
 }
 if (name != NULL)
 {
  const char *p;
  for (p=name; p!=(char*)1; p=strchr(p, '.')+1)
   for (arec=atable[hash_text(p)]; arec; arec=arec->next)
    if ((arec->type == (type&~0x1)) &&
        (arec->masktype == HM_HOST) &&
        match(arec->Mask.hostname, name) &&
        (type&0x1 || match(arec->username, username)) &&
        (arec->precedence > hprecv)
       )
    {
     hprecv = arec->precedence;
     hprec = arec->aconf;
    }
  for (arec=atable[0]; arec; arec=arec->next)
   if (arec->type == (type&~0x1) &&
       arec->masktype == HM_HOST &&
       match(arec->Mask.hostname, name) &&
       (type&0x1 || match(arec->username, username)) &&
       arec->precedence > hprecv
      )
   {
    hprecv = arec->precedence;
    hprec = arec->aconf;
   }
 }
 return hprec;
}

/* struct ConfItem* find_address_conf(const char*, const char*,
                                      struct irc_inaddr*, int);
 * Input: The hostname, username, address, address family.
 * Output: The applicable ConfItem.
 * Side-effects: None
 */
struct ConfItem*
find_address_conf(const char *host, const char *user,
                  struct irc_inaddr *ip, int aftype)
{
 struct ConfItem *iconf, *kconf;
 /* Find the best I-line... If none, return NULL -A1kmm */
 if (!(iconf = find_conf_by_address(host, ip, CONF_CLIENT, aftype, user)))
  return NULL;
 /* If they are exempt from K-lines, return the best I-line. -A1kmm */
 if (IsConfExemptKline(iconf))
  return iconf;
 /* Find the best K-line... -A1kmm */
 kconf = find_conf_by_address(host, ip, CONF_KILL, aftype, user);
 /* If they are K-lined, return the K-line. Otherwise, return the
  * I-line. -A1kmm */
 if (kconf)
  return kconf;
 return iconf;
}

/* struct ConfItem* find_dline(struct irc_inaddr*, int)
 * Input: An address, an address family.
 * Output: The best matching D-line or exempt line.
 * Side effects: None.
 */
struct ConfItem*
find_dline(struct irc_inaddr *addr, int aftype)
{
 struct ConfItem *eline, *dline;
 eline = find_conf_by_address(NULL, addr, CONF_EXEMPTDLINE|1, aftype,
                              NULL);
 dline = find_conf_by_address(NULL, addr, CONF_DLINE|1, aftype, NULL);
 return dline ? (eline ? eline : dline) : NULL;
}

/* void add_conf_by_address(const char*, int, const char *,
 *         struct ConfItem *aconf)
 * Input: 
 * Output: None
 * Side-effects: Adds this entry to the hash table.
 */
void
add_conf_by_address(const char *address, int type, const char *username,
                    struct ConfItem *aconf)
{
 static unsigned long prec_value = 0xFFFFFFFF;
 int masktype, bits;
 unsigned long hv;
 struct AddressRec *arec;
 if (address == NULL)
  address = "/NOMATCH!/";
 arec = MyMalloc(sizeof(*arec));
 masktype = parse_netmask(address, &arec->Mask.ipa.addr, &bits);
 arec->Mask.ipa.bits = bits;
 arec->masktype = masktype;
#ifdef IPV6
 if (masktype == HM_IPV6)
 {
  /* We have to do this, since we do not re-hash for every bit -A1kmm. */
  bits -= bits%16;
  arec->next = atable[(hv = hash_ipv6(&arec->Mask.ipa.addr, bits))];
  atable[hv] = arec;
 } else
#endif
 if (masktype == HM_IPV4)
 {
  /* We have to do this, since we do not re-hash for every bit -A1kmm. */
  bits -= bits%8;
  arec->next = atable[(hv = hash_ipv4(&arec->Mask.ipa.addr, bits))];
  atable[hv] = arec;
 } else
 {
  arec->Mask.hostname = address;
  arec->next = atable[(hv = get_mask_hash(address))];
  atable[hv] = arec;
 }
 arec->username = username;
 arec->aconf = aconf;
 arec->precedence = prec_value--;
 arec->type = type;
}

/* void delete_one_address(const char*, struct ConfItem*)
 * Input: An address string, the associated ConfItem.
 * Output: None
 * Side effects: Deletes an address record. Frees the ConfItem if there
 *               is nothing referencing it, sets it as illegal otherwise.
 */
void
delete_one_address_conf(const char *address, struct ConfItem *aconf)
{
 int masktype, bits;
 unsigned long hv;
 struct AddressRec *arec, *arecl=NULL;
 struct irc_inaddr addr;
 masktype = parse_netmask(address, &addr, &bits);
#ifdef IPV6
 if (masktype == HM_IPV6)
 {
  /* We have to do this, since we do not re-hash for every bit -A1kmm. */
  bits -= bits%16;
  hv = hash_ipv6(&addr, bits);
 } else
#endif
 if (masktype == HM_IPV4)
 {
  /* We have to do this, since we do not re-hash for every bit -A1kmm. */
  bits -= bits%8;
  hv = hash_ipv4(&addr, bits);
 } else
  hv = get_mask_hash(address);
 for (arec = atable[hv]; arec; arec=arec->next)
 {
  if (arec->aconf == aconf)
  {
   if (arecl)
    arecl->next = arec->next;
   else
    atable[hv] = arec->next;
   aconf->flags |= CONF_ILLEGAL;
   if (!aconf->clients)
    free_conf(aconf);
   MyFree(arec);
   return;
  }
  arecl = arec;
 }
}

/* void clear_out_address_conf(void)
 * Input: None
 * Output: None
 * Side effects: Clears out all address records in the hash table,
 *               frees them, and frees the ConfItems if nothing references
 *               them, otherwise sets them as illegal.
 */
void
clear_out_address_conf(void)
{
 int i;
 struct AddressRec **store_next;
 struct AddressRec *arec, *arecn;
 for (i=0; i<ATABLE_SIZE; i++)
 {
  store_next = &atable[i];
  for (arec=atable[i]; arec; arec=arecn)
  {
   arecn = arec->next;
   /* We keep the temporary K-lines and destroy the
    * permanent ones, just to be confusing :) -A1kmm */
   if (arec->aconf->flags & CONF_FLAGS_TEMPORARY)
   {
    *store_next = arec;
    store_next = &arec->next;
   } else
   {
    arec->aconf->flags |= CONF_ILLEGAL;
    if (!arec->aconf->clients)
     free_conf(arec->aconf);
    MyFree(arec);
   }
  }
  *store_next = NULL;
 }
}

/* void report_dlines(struct Client *client_p)
 * Input: Client to report to.
 * Output: None
 * Side effects: Reports configured D-lines to client_p
 */
void
report_dlines(struct Client *client_p)
{
 char *name, *host, *pass, *user, *classname;
 struct AddressRec *arec;
 struct ConfItem *aconf;
 int i, port;
 for (i=0; i<ATABLE_SIZE; i++)
  for (arec=atable[i]; arec; arec=arec->next)
   if (arec->type == CONF_DLINE)
   {
    aconf = arec->aconf;
    get_printable_conf(aconf, &name, &host, &pass, &user, &port,
                       &classname);
    sendto_one(client_p, form_str(RPL_STATSDLINE), me.name,
               client_p->name, 'D', host, pass);
   }
}

/* void report_exemptlines(struct Client *client_p)
 * Input: Client to report to.
 * Output: None
 * Side effects: Reports configured exemptions to client_p
 */
void
report_exemptlines(struct Client *client_p)
{
  char *name, *host, *pass, *user, *classname;
  struct AddressRec *arec;
  struct ConfItem *aconf;
  int i, port;
  
  for (i=0; i<ATABLE_SIZE; i++)
    for (arec=atable[i]; arec; arec=arec->next)
      if (arec->type == CONF_EXEMPTDLINE)
        {
           aconf = arec->aconf;
           get_printable_conf(aconf, &name, &host, &pass,
                              &user, &port, &classname);
           sendto_one(client_p, form_str(RPL_STATSDLINE), me.name,
                      client_p->name, 'e', host, pass);
        }
}
 
/*
 * show_iline_prefix()
 *
 * inputs       - pointer to struct Client requesting output
 *              - pointer to struct ConfItem 
 *              - name to which iline prefix will be prefixed to
 * output       - pointer to static string with prefixes listed in ascii form
 * side effects - NONE
 */
char *
show_iline_prefix(struct Client *sptr,struct ConfItem *aconf,char *name)
{
 static char prefix_of_host[USERLEN+15];
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
 if (IsOper(sptr) && IsConfExemptKline(aconf))
  *prefix_ptr++ = '^';
 if (IsOper(sptr) && IsConfExemptLimits(aconf))
  *prefix_ptr++ = '>';
 if (IsOper(sptr) && IsConfIdlelined(aconf))
  *prefix_ptr++ = '<';
 *prefix_ptr = '\0';
 strncat(prefix_of_host,name,USERLEN);
 return(prefix_of_host);
}

/* void report_Ilines(struct Client *client_p, int mask)
 * Input: None
 * Output: None
 * Side effects: Reports configured auth{} blocks to client_p
 */
void
report_Ilines(struct Client *client_p, int mask)
{
 char *name, *host, *pass, *user, *classname;
 struct AddressRec *arec;
 struct ConfItem *aconf;
 int i, port;
 for (i=0; i<ATABLE_SIZE; i++)
  for (arec=atable[i]; arec; arec=arec->next)
   if (arec->type == CONF_CLIENT)
   {
    aconf = arec->aconf;

    if (!(MyConnect(client_p) && IsOper(client_p)) &&
        (IsConfDoSpoofIp(aconf) && !mask))
     continue;

    get_printable_conf(aconf, &name, &host, &pass, &user, &port,
                       &classname);

    /* We are doing a partial list, based on what matches the u@h of the
     * sender, so prepare the strings for comparing --fl_ */
    if(mask)
      {
        char usermask[USERLEN+HOSTLEN+10], maskmatch[USERLEN+HOSTLEN+10];
        ircsprintf(usermask, "%s@%s", user, host);
        ircsprintf(maskmatch, "%s@%s", client_p->username, client_p->host);

        /* If the strings dont match, continue to the next auth block */
        if(!match(host, maskmatch))
          continue;

      }

    sendto_one(client_p, form_str(RPL_STATSILINE), me.name,
               client_p->name, 'I', name,
               show_iline_prefix(client_p, aconf, user), 
	       host, port, classname);
   }
}

/* void report_Klines(struct Client *client_p, int t)
 * Input: Client to report to, type(==0 for perm, !=0 for temporary).
 * Output: None
 * Side effects: Reports configured K(or k)-lines to client_p.
 */
void
report_Klines(struct Client *client_p, int t)
{
 char *name, *host, *pass, *user, *classname, c;
 struct AddressRec *arec;
 struct ConfItem *aconf;
 int i, port;
 if (t)
  c = 'k';
 else
  c = 'K';
 for (i=0; i<ATABLE_SIZE; i++)
  for (arec=atable[i]; arec; arec=arec->next)
   if (arec->type == CONF_KILL)
   {
    if ((t && !((aconf=arec->aconf)->flags & CONF_FLAGS_TEMPORARY))
        || (!t && ((aconf=arec->aconf)->flags & CONF_FLAGS_TEMPORARY)))
     continue;
    get_printable_conf(aconf, &name, &host, &pass, &user, &port,
                       &classname);
    sendto_one(client_p, form_str(RPL_STATSKLINE), me.name,
               client_p->name, c, host, user, pass);
   }
}
