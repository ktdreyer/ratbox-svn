/* s_u_crypt.c
 *   Contains functions for encrypting a password.
 *
 * Copyright (C) 2004-2007 Lee Hardy
 * Copyright (C) 2004-2007 ircd-ratbox development team
 *
 * $Id$
 */
/* Original header:
 * 
 * simple password generator by Nelson Minar (minar@reed.edu)
** copyright 1991, all rights reserved.
** You can use this code as long as my name stays with it.
**
** md5 patch by W. Campbell <wcampbel@botbay.net>
** Modernization, getopt, etc for the Hybrid IRCD team
** by W. Campbell
** 
** /dev/random for salt generation added by 
** Aaron Sethman <androsyn@ratbox.org>
**
** $Id$
*/
#include "stdinc.h"
#include "rserv.h"

static char saltChars[] =
       "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
       /* 0 .. 63, ascii - 64 */

void
init_crypt_seed(void)
{
	unsigned int seed;
	if(rb_get_random(&seed, sizeof(seed)) == -1)
	{
		const struct timeval *tv = rb_current_time_tv();
		srand(tv->tv_sec ^ (tv->tv_usec | (getpid() << 20)));
		return;
	}
	srand(seed);
}

static char *
generate_salt(char *salt, unsigned int len)
{
	unsigned int i;

	for(i = 0; i < len; i++)
	{
		salt[i] = saltChars[rand() % 64];
	}

	return salt;
}

static char *
make_md5_salt(void)
{
	static char salt[13];

	salt[0] = '$';
	salt[1] = '1';
	salt[2] = '$';
	generate_salt(&salt[3], 8);
	salt[11] = '$';
	salt[12] = '\0';

	return salt;
}

static char *
make_des_salt()
{
	static char salt[3];

	generate_salt(salt, 2);
	salt[2] = '\0';

	return salt;
}


const char *
get_crypt(const char *password, const char *csalt)
{
	const char *salt = csalt;
	const char *result;

	if(have_md5_crypt)
	{
		if(salt == NULL)
			salt = make_md5_salt();
	}
	else if(salt == NULL)
		salt = make_des_salt();

	result = rb_crypt(password, salt);
	return result;
}

const char *
get_password(void)
{
	static char buf[9];

	generate_salt(buf, 8);
	buf[8] = '\0';

	return buf;
}
