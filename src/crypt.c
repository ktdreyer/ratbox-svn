/* s_u_crypt.c
 *   Contains functions for encrypting a password.
 *
 * Copyright (C) 2004-2005 Lee Hardy
 * Copyright (C) 2004-2005 ircd-ratbox development team
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

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

static char saltChars[] =
       "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
       /* 0 .. 63, ascii - 64 */

static char *
generate_poor_salt(char *salt)
{
	int i;

	srandom(CURRENT_TIME);

	for(i = 0; i < 8; i++)
	{
		salt[i] = saltChars[random() % 64];
	}

	return salt;
}

static char *
generate_random_salt(char *salt)
{
	static char buf[9];
	int fd, i;

	if((fd = open("/dev/random", O_RDONLY)) < 0)
	{
		return generate_poor_salt(salt);
	}

	if(read(fd, buf, 8) != 8)
	{
		return generate_poor_salt(salt);
	}

	for(i = 0; i < 8; i++)
	{
		salt[i] = saltChars[abs(buf[i]) % 64];
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
	generate_random_salt(&salt[3]);
	salt[11] = '$';
	salt[12] = '\0';

	return salt;
}

static char *
make_des_salt()
{
	static char salt[9];

	generate_random_salt(salt);
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

	result = crypt(password, salt);
	return result;
}
