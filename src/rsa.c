/*
 *  ircd-ratbox: A slightly useful ircd.
 *  rsa.c: Functions for use with RSA public key cryptography.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
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

#ifdef HAVE_LIBCRYPTO
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/md5.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#endif

#include "memory.h"
#include "rsa.h"
#include "s_conf.h"
#include "s_log.h"
#include "client.h"		/* CIPHERKEYLEN .. eww */

#ifdef HAVE_LIBCRYPTO

void report_crypto_errors(void);
static void binary_to_hex(unsigned char *bin, char *hex, int length);

/*
 * report_crypto_errors - Dump crypto error list to log
 */
void
report_crypto_errors(void)
{
	unsigned long e = 0;
	unsigned long cnt = 0;

	ERR_load_crypto_strings();
	while ((cnt < 100) && (e = ERR_get_error()))
	{
		ilog(L_CRIT, "SSL error: %s", ERR_error_string(e, 0));
		cnt++;
	}
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

int
get_randomness(unsigned char *buf, int length)
{
	/* Seed OpenSSL PRNG with EGD enthropy pool -kre */
	if(ConfigFileEntry.use_egd && (ConfigFileEntry.egdpool_path != NULL))
	{
		if(RAND_egd(ConfigFileEntry.egdpool_path) == -1)
			return -1;
	}

	if(RAND_status())
		return RAND_bytes(buf, length);
	else			/* XXX - abort? */
		return RAND_pseudo_bytes(buf, length);
}

int generate_challenge(char **, char **, RSA *);
int
generate_challenge(char **r_challenge, char **r_response, RSA * rsa)
{
	unsigned char secret[32], *tmp;
	unsigned long length, ret;

	if(!rsa)
		return -1;
	get_randomness(secret, 32);
	*r_response = MyMalloc(65);
	binary_to_hex(secret, *r_response, 32);

	length = RSA_size(rsa);
	tmp = MyMalloc(length);
	ret = RSA_public_encrypt(32, secret, tmp, rsa, RSA_PKCS1_PADDING);

	*r_challenge = MyMalloc((length << 1) + 1);
	binary_to_hex(tmp, *r_challenge, length);
	(*r_challenge)[length << 1] = 0;
	MyFree(tmp);

	if(ret < 0)
	{
		report_crypto_errors();
		return (-1);
	}
	return (0);
}

#endif /* HAVE_LIBCRYPTO */
