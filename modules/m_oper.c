/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_oper.c: Makes a user an IRC Operator.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2004 ircd-ratbox development team
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

#ifdef HAVE_LIBCRYPTO
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/md5.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#endif

#include "tools.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "commio.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_log.h"
#include "s_user.h"
#include "send.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "packet.h"
#include "cache.h"

static int m_oper(struct Client *, struct Client *, int, const char **);

struct Message oper_msgtab = {
	"OPER", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, {m_oper, 3}, mg_ignore, mg_ignore, mg_ignore, {m_oper, 3}}
};

#ifdef HAVE_LIBCRYPTO
static int m_challenge(struct Client *, struct Client *, int, const char **);

struct Message challenge_msgtab = {
        "CHALLENGE", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, {m_challenge, 2}, mg_ignore, mg_ignore, mg_ignore, {m_challenge, 2}}
};
static int generate_challenge(char **r_challenge, char **r_response, RSA * key);
#endif                


mapi_clist_av1 oper_clist[] = { &oper_msgtab, 
#ifdef HAVE_LIBCRYPTO
				&challenge_msgtab, 
#endif
				NULL };

DECLARE_MODULE_AV1(oper, NULL, NULL, oper_clist, NULL, NULL, "$Revision$");

static int match_oper_password(const char *password, struct oper_conf *oper_p);
static void oper_up(struct Client *source_p, struct oper_conf *oper_p);
extern char *crypt();

/*
 * m_oper
 *      parv[0] = sender prefix
 *      parv[1] = oper name
 *      parv[2] = oper password
 */
static int
m_oper(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct oper_conf *oper_p;
	const char *name;
	const char *password;

	name = parv[1];
	password = parv[2];

	if(IsOper(source_p))
	{
		sendto_one(source_p, form_str(RPL_YOUREOPER), me.name, source_p->name);
		send_oper_motd(source_p);
		return 0;
	}

	/* end the grace period */
	if(!IsFloodDone(source_p))
		flood_endgrace(source_p);

	oper_p = find_oper_conf(source_p->username, source_p->host, 
				source_p->sockhost, name);

	if(oper_p == NULL)
	{
		sendto_one(source_p, form_str(ERR_NOOPERHOST), me.name, source_p->name);
		ilog(L_FOPER, "FAILED OPER (%s) by (%s!%s@%s)",
		     name, source_p->name,
		     source_p->username, source_p->host);

		if(ConfigFileEntry.failed_oper_notice)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Failed OPER attempt - host mismatch by %s (%s@%s)",
					     source_p->name, source_p->username, source_p->host);
		}

		return 0;
	}

	if(match_oper_password(password, oper_p))
	{
		oper_up(source_p, oper_p);

		ilog(L_OPERED, "OPER %s by %s!%s@%s",
		     name, source_p->name, source_p->username, source_p->host);
		return 0;
	}
	else
	{
		sendto_one(source_p, form_str(ERR_PASSWDMISMATCH),
			   me.name, source_p->name);

		ilog(L_FOPER, "FAILED OPER (%s) by (%s!%s@%s)",
		     name, source_p->name, source_p->username, source_p->host);

		if(ConfigFileEntry.failed_oper_notice)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Failed OPER attempt by %s (%s@%s)",
					     source_p->name, source_p->username, source_p->host);
		}
	}

	return 0;
}

/*
 * match_oper_password
 *
 * inputs       - pointer to given password
 *              - pointer to Conf 
 * output       - YES or NO if match
 * side effects - none
 */
static int
match_oper_password(const char *password, struct oper_conf *oper_p)
{
	const char *encr;

	/* passwd may be NULL pointer. Head it off at the pass... */
	if(EmptyString(oper_p->passwd))
		return NO;

	if(IsOperConfEncrypted(oper_p))
	{
		/* use first two chars of the password they send in as salt */
		/* If the password in the conf is MD5, and ircd is linked   
		 * to scrypt on FreeBSD, or the standard crypt library on
		 * glibc Linux, then this code will work fine on generating
		 * the proper encrypted hash for comparison.
		 */
		if(!EmptyString(password))
			encr = crypt(password, oper_p->passwd);
		else
			encr = "";
	}
	else
		encr = password;

	if(strcmp(encr, oper_p->passwd) == 0)
		return YES;
	else
		return NO;
}

/* oper_up()
 *
 * inputs	- pointer to given client to oper
 *		- pointer to ConfItem to use
 * output	- none
 * side effects	- opers up source_p using aconf for reference
 */
static void
oper_up(struct Client *source_p, struct oper_conf *oper_p)
{
	int old = (source_p->umodes & ALL_UMODES);

	SetOper(source_p);

	if(oper_p->umodes)
		source_p->umodes |= oper_p->umodes & ALL_UMODES;
	else if(ConfigFileEntry.oper_umodes)
		source_p->umodes |= ConfigFileEntry.oper_umodes & ALL_UMODES;
	else
		source_p->umodes |= DEFAULT_OPER_UMODES & ALL_UMODES;

	Count.oper++;

	SetExemptKline(source_p);

	source_p->flags2 |= oper_p->flags;
	DupString(source_p->localClient->opername, oper_p->name);

	dlinkAddAlloc(source_p, &oper_list);

	if(IsOperAdmin(source_p) && !IsOperHiddenAdmin(source_p))
		source_p->umodes |= UMODE_ADMIN;
	if(!IsOperN(source_p))
		source_p->umodes &= ~UMODE_NCHANGE;

	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "%s (%s@%s) is now an operator", source_p->name,
			     source_p->username, source_p->host);
	send_umode_out(source_p, source_p, old);
	sendto_one(source_p, form_str(RPL_YOUREOPER), me.name, source_p->name);
	sendto_one(source_p, ":%s NOTICE %s :*** Oper privs are %s", me.name,
		   source_p->name, get_oper_privs(oper_p->flags));
	send_oper_motd(source_p);

	return;
}

#ifdef HAVE_LIBCRYPTO

/*
 * m_challenge - generate RSA challenge for wouldbe oper
 * parv[0] = sender prefix
 * parv[1] = operator to challenge for, or +response
 *
 */
static int
m_challenge(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct oper_conf *oper_p;
	char *challenge;

	/* if theyre an oper, reprint oper motd and ignore */
	if(IsOper(source_p))
	{
		sendto_one(source_p, form_str(RPL_YOUREOPER), me.name, source_p->name);
		send_oper_motd(source_p);
		return 0;
	}

	if(*parv[1] == '+')
	{
		/* Ignore it if we aren't expecting this... -A1kmm */
		if(!source_p->user->response)
			return 0;

		if(irccmp(source_p->user->response, ++parv[1]))
		{
			sendto_one(source_p, form_str(ERR_PASSWDMISMATCH), me.name, source_p->name);
			ilog(L_FOPER, "FAILED OPER (%s) by (%s!%s@%s)",
			     source_p->user->auth_oper, source_p->name,
			     source_p->username, source_p->host);

			if(ConfigFileEntry.failed_oper_notice)
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Failed OPER attempt by %s (%s@%s)",
						     source_p->name, source_p->username,
						     source_p->host);
			return 0;
		}

		oper_p = find_oper_conf(source_p->username, source_p->host, 
					source_p->sockhost, 
					source_p->user->auth_oper);

		if(oper_p == NULL)
		{
			sendto_one(source_p, form_str(ERR_NOOPERHOST), 
				   me.name, source_p->name);
			ilog(L_FOPER, "FAILED OPER (%s) by (%s!%s@%s)",
			     source_p->user->auth_oper, source_p->name,
			     source_p->username, source_p->host);

			if(ConfigFileEntry.failed_oper_notice)
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Failed CHALLENGE attempt - host mismatch by %s (%s@%s)",
						     source_p->name, source_p->username,
						     source_p->host);
			return 0;
		}

		oper_up(source_p, oper_p);

		ilog(L_OPERED, "OPER %s by %s!%s@%s",
		     source_p->user->auth_oper, source_p->name, 
		     source_p->username, source_p->host);

		MyFree(source_p->user->response);
		MyFree(source_p->user->auth_oper);
		source_p->user->response = NULL;
		source_p->user->auth_oper = NULL;
		return 0;
	}

	MyFree(source_p->user->response);
	MyFree(source_p->user->auth_oper);
	source_p->user->response = NULL;
	source_p->user->auth_oper = NULL;

	oper_p = find_oper_conf(source_p->username, source_p->host, 
				source_p->sockhost, parv[1]);

	if(oper_p == NULL)
	{
		sendto_one(source_p, form_str(ERR_NOOPERHOST), me.name, source_p->name);
		ilog(L_FOPER, "FAILED OPER (%s) by (%s!%s@%s)",
		     parv[1], source_p->name,
		     source_p->username, source_p->host);

		if(ConfigFileEntry.failed_oper_notice)
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Failed CHALLENGE attempt - host mismatch by %s (%s@%s)",
					     source_p->name, source_p->username, source_p->host);
		return 0;
	}

	if(!oper_p->rsa_pubkey)
	{
		sendto_one(source_p, ":%s NOTICE %s :I'm sorry, PK authentication "
			   "is not enabled for your oper{} block.", me.name, parv[0]);
		return 0;
	}

	if(!generate_challenge(&challenge, &(source_p->user->response), oper_p->rsa_pubkey))
	{
		sendto_one(source_p, form_str(RPL_RSACHALLENGE), 
			   me.name, source_p->name, challenge);
	}

	DupString(source_p->user->auth_oper, oper_p->name);
	MyFree(challenge);
	return 0;
}

static void
binary_to_hex(unsigned char *bin, char *hex, int length)
{
	static const char trans[] = "0123456789ABCDEF";
	int i;

	for (i = 0; i < length; i++)
	{
		hex[i << 1] = trans[bin[i] >> 4];
		hex[(i << 1) + 1] = trans[bin[i] & 0xf];
	}
	hex[i << 1] = '\0';
}

static int
get_randomness(unsigned char *buf, int length)
{
	/* Seed OpenSSL PRNG with EGD enthropy pool -kre */
	if(ConfigFileEntry.use_egd && (ConfigFileEntry.egdpool_path != NULL))
	{
		if(RAND_egd(ConfigFileEntry.egdpool_path) == -1)
			return -1;
	}

	if(RAND_status())
	{
	 	if(RAND_bytes(buf, length) > 0)
	 	        return 1;
	}
	else {
	        if(RAND_pseudo_bytes(buf, length) >= 0)
	                return 1;
	}
	return 0;
}

int
generate_challenge(char **r_challenge, char **r_response, RSA * rsa)
{
	unsigned char secret[32], *tmp;
	unsigned long length;
	unsigned long e = 0;
	unsigned long cnt = 0;
	int ret;

	if(!rsa)
		return -1;
	if(get_randomness(secret, 32))
	{
		*r_response = MyMalloc(65);
		binary_to_hex(secret, *r_response, 32);

		length = RSA_size(rsa);
		tmp = MyMalloc(length);
		ret = RSA_public_encrypt(32, secret, tmp, rsa, RSA_PKCS1_PADDING);
	
		*r_challenge = MyMalloc((length << 1) + 1);
		binary_to_hex(tmp, *r_challenge, length);
		(*r_challenge)[length << 1] = 0;
		MyFree(tmp);
		if(ret >= 0)
			return 0;
	}

	ERR_load_crypto_strings();
	while ((cnt < 100) && (e = ERR_get_error()))
	{
		ilog(L_MAIN, "SSL error: %s", ERR_error_string(e, 0));
		cnt++;
	}

	return (-1);
}

#endif /* HAVE_LIBCRYPTO */
