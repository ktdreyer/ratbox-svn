/************************************************************************
 *   IRC - Internet Relay Chat, src/rsa.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Co Center
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
 *
 *
 * $Id$
 */

#include <assert.h>
#include <string.h>

#include "memory.h"
#include "rsa.h"
#include "s_conf.h"
#include "s_log.h"
#include "client.h" /* CIPHERKEYLEN .. eww */

#ifdef HAVE_LIBCRYPTO

#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/md5.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/err.h>

void report_crypto_errors(void);
int verify_private_key(void);
static void binary_to_hex( unsigned char * bin, char * hex, int length );
static int absorb( char ** str, char lowest, char highest );
static RSA * str_to_RSApublic( char * key );

/*
 * report_crypto_errors - Dump crypto error list to log
 */
void report_crypto_errors(void)
{
  unsigned long e = 0;
  unsigned long cnt = 0;

  ERR_load_crypto_strings();
  while ( (cnt < 100) && (e = ERR_get_error()) )
  {
    ilog(L_CRIT, "SSL error: %s", ERR_error_string(e, 0));
    cnt++;
  }
};

/*
 * verify_private_key - reread private key and verify against inmem key
 */
int verify_private_key(void)
{
  BIO * file;
  RSA * key;
  RSA * mkey;

  /* If the rsa_private_key directive isn't found, error out. */
  if (ServerInfo.rsa_private_key == NULL)
  {
    ilog(L_NOTICE, "rsa_private_key in serverinfo{} is not defined.");
    return(-1);
  }

  /* If rsa_private_key_filename isn't available, error out. */
  if (ServerInfo.rsa_private_key_filename == NULL)
  {
    ilog(L_NOTICE, "Internal error: rsa_private_key_filename isn't defined.");
    return(-1);
  }

  file = BIO_new_file(ServerInfo.rsa_private_key_filename, "r");

  /*
   * If BIO_new_file returned NULL (according to OpenSSL docs), then
   * an error occurred.
   */
  if (file == NULL)
  {
    ilog(L_NOTICE, "Failed to open private key file - can't validate it");
    return(-1);
  }

  /*
   * jdc -- Let's do this a little differently.  According to the
   *        OpenSSL documentation, you need to METHOD_free(key) before
   *        assigning it.  Don't believe me?  Check out the following
   *        URL:  http://www.openssl.org/docs/crypto/pem.html#BUGS
   * P.S. -- I have no idea why the key= assignment has to be typecasted.
   *         For some reason the system thinks PEM_read_bio_RSAPrivateKey
   *         is returning an int, not a RSA *.
   */
  key = (RSA *) PEM_read_bio_RSAPrivateKey(file, NULL, 0, NULL);

  if (key == NULL)
  {
    ilog(L_NOTICE, "PEM_read_bio_RSAPrivateKey() failed; possibly not RSA?");
    report_crypto_errors();
    return(-1);
  }

  BIO_set_close(file, BIO_CLOSE);
  BIO_free(file);

  mkey = ServerInfo.rsa_private_key;

  /*
   * Compare the in-memory key to the key we just loaded above.  If
   * any of the portions don't match, then logically we have a different
   * in-memory key vs. the one we just loaded.  This is bad, mmmkay?
   */
  if (mkey->pad != key->pad)
    ilog(L_CRIT, "Private key corrupted: pad %i != pad %i",
                 mkey->pad, key->pad);

  if (mkey->version != key->version)
    ilog(L_CRIT, "Private key corrupted: version %i != version %i",
                  mkey->version, key->version);

  if (BN_cmp(mkey->n, key->n))
    ilog(L_CRIT, "Private key corrupted: n differs");
  if (BN_cmp(mkey->e, key->e))
    ilog(L_CRIT, "Private key corrupted: e differs");
  if (BN_cmp(mkey->d, key->d))
    ilog(L_CRIT, "Private key corrupted: d differs");
  if (BN_cmp(mkey->p, key->p))
    ilog(L_CRIT, "Private key corrupted: p differs");
  if (BN_cmp(mkey->q, key->q))
    ilog(L_CRIT, "Private key corrupted: q differs");
  if (BN_cmp(mkey->dmp1, key->dmp1))
    ilog(L_CRIT, "Private key corrupted: dmp1 differs");
  if (BN_cmp(mkey->dmq1, key->dmq1))
    ilog(L_CRIT, "Private key corrupted: dmq1 differs");
  if (BN_cmp(mkey->iqmp, key->iqmp))
    ilog(L_CRIT, "Private key corrupted: iqmp differs");

  RSA_free(key);
  return(0);
}


static void binary_to_hex( unsigned char * bin, char * hex, int length )
{
  char * trans = "0123456789ABCDEF";
  int i;

  for( i = 0; i < length; i++ )
  {
    hex[i<<1]     = trans[bin[i] >> 4];
    hex[(i<<1)+1] = trans[bin[i] & 0xf];
  }
  hex[i<<1] = '\0';
}

int get_randomness( unsigned char * buf, int length )
{
    /* Seed OpenSSL PRNG with EGD enthropy pool -kre */
    if (ConfigFileEntry.use_egd &&
        (ConfigFileEntry.egdpool_path != NULL))
    {
      if (RAND_egd(ConfigFileEntry.egdpool_path) == -1)
            return -1;
    }

  if ( RAND_status() )
    return RAND_bytes( buf, length );
  else /* XXX - abort? */
    return RAND_pseudo_bytes( buf, length );
}

static int absorb( char ** str, char lowest, char highest )
{
  char * start = *str;

  while( **str >= lowest && **str <= highest )
    (*str)++;

  return *str - start;
}

static RSA * str_to_RSApublic( char * key )
{
  char * e, * n;
  RSA * rsa;

  /* bits */
  if( !absorb( &key, '0', '9' ) )
    return NULL;

  /* space */
  if( !absorb( &key, ' ', ' ' ) )
    return NULL;

  /* e */
  e = key;
  if( !absorb( &key, '0', '9' ) )
    return NULL;
  if( *key != ' ' )
    return NULL;
  *key = '\0';

  /* n */
  n = ++key;
  if( !absorb( &key, '0', '9' ) )
    return NULL;
  if( *key != ' ' && *key != '\0' )
    return NULL;
  *key = '\0';

  /* convert to RSA key structure */
  rsa = RSA_new();
  BN_dec2bn( &(rsa->e), e );
  BN_dec2bn( &(rsa->n), n );

  return rsa;
}

int generate_challenge( char **, char **, char*);
int generate_challenge( char ** r_challenge, char ** r_response, char * key )
{
  unsigned char secret[32], *tmp;
  unsigned long length, ret;
  char *nkey;
  RSA *rsa;
  DupString(nkey, key);
  if (!(rsa = str_to_RSApublic(nkey)))
  {
    *r_challenge = NULL;
    *r_response = NULL;
    MyFree(nkey);
    return -1;
  }
  MyFree(nkey);

  get_randomness(secret, 32);
  *r_response = MyMalloc(65);
  binary_to_hex(secret, *r_response, 32);

  length = RSA_size(rsa);
  tmp = MyMalloc(length);
  ret = RSA_public_encrypt(32, secret, tmp, rsa, RSA_PKCS1_PADDING);

  *r_challenge = MyMalloc( (length << 1) + 1 );
  binary_to_hex( tmp, *r_challenge, length );
  (*r_challenge)[length<<1] = 0;
  MyFree(tmp);

  if (ret < 0)
  {
    report_crypto_errors();
    return(-1);
  }
  return(0);
}

#endif /* HAVE_LIBCRYPTO */
