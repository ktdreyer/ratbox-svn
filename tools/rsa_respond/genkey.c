/*
 * tools/rsa_respond/genkey.c
 * A simple RSA key generator for use with the CHALLENGE command in
 * ircd-hybrid.
 *  This code is Copyright(C)2001 by the past and present ircd-hybrid
 *  developers.
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *  $Id$
 */


#include <openssl/rsa.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
 RSA *rsa;
 char *pass, *bnn, *bnd, *bnds;
 int a=0, l;
 FILE *kfile;
 /* genkey publicfile privatefile */
 if (argc < 2)
   {
    puts("Usage: genkey publicfile privatefile");
    return -1;
   }
 rsa = RSA_generate_key(1024, RSA_F4, NULL, NULL);
 do {
   pass = getpass("Keyphrase: ");
   l = strlen(pass);
 } while (!l);
 bnn = BN_bn2hex(rsa->n);
 bnd = BN_bn2hex(rsa->d);
 bnds = bnd;
 while (*bnds)
   *bnds++ ^= pass[a++%l];
 if (!(kfile = fopen(argv[2], "w")))
   {
    puts("Could not open the private key file.");
    exit(-1);
   }
 fwrite(bnn, 256, 1, kfile);
 fwrite(bnd, 256, 1, kfile);
 fclose(kfile);
 if (!(kfile = fopen(argv[1], "w")))
   {
    puts("Could not open the public key file.");
    exit(-1);
   }
 fprintf(kfile, "1024 %s %s", BN_bn2dec(rsa->e), BN_bn2dec(rsa->n));
 fclose(kfile);
 return 0;
}
