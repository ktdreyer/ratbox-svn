/************************************************************************
 *   IRC - Internet Relay Chat, servlink/servlink.c
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
 *   $Id$
 */

#include "../include/setup.h"                                                   

#include <sys/types.h>
#include <sys/time.h>

#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef HAVE_LIBCRYPTO
#include <openssl/evp.h>
#include <openssl/err.h>
#endif
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#include "servlink.h"
#include "io.h"
#include "control.h"

static void usage(void);

struct slink_state       in_state;
struct slink_state       out_state;

struct fd_table          fds[NUM_FDS] =
        {
          {      NULL, NULL }, /* stdin */
          {      NULL, NULL }, /* stdout */
          {      NULL, NULL }, /* stderr */
          { read_ctrl, NULL },
          {      NULL, NULL },
#ifdef MISSING_SOCKPAIR
          {      NULL, NULL },
          {      NULL, NULL },
#endif
          {      NULL, NULL }
        };


char *fd_name[NUM_FDS] =
#ifdef MISSING_SOCKPAIR
  {
    "control read", "data read", "net read",
    "control write", "data write"
  };
#else
  { "control", "data", "net" };
#endif

/* usage();
 *
 * Display usage message
 */
static void usage(void)
{
  fprintf(stderr, "ircd-hybrid server link v1.0-beta\n");
  fprintf(stderr, "2001-05-23\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "This program is called by the ircd-hybrid ircd.\n");
  fprintf(stderr, "It cannot be used on its own.\n");
  exit(1);
}

int main(int argc, char *argv[])
{
  fd_set rfds;
  fd_set wfds;
  int i;
#ifdef SERVLINK_DEBUG
  int GDBAttached = 0;

  while (!GDBAttached)
    sleep(1);
#endif

#ifdef HAVE_LIBCRYPTO
  ERR_load_crypto_strings();
#endif

  /* Make sure we are running under hybrid.. */
  if (isatty(0) || argc != 1 || strcmp(argv[0], "-slink"))
    usage(); /* exits */
  
  for (i = 0; i < 3; i++)
  {
    fcntl(i, F_SETFL, O_NONBLOCK);
  }

  /* The following FDs should be open:
   *
   * 3 - bi/uni-directional pipe for control commands
   * 4 - bi/uni-directional pipe for data
   * 5 - bi-directional socket connected to remote server
   * maybe:
   * 6 - uni-directional (write) pipe for control commands
   * 7 - uni-directional (write) pipe for data
   */

  /* loop forever */
  while (1)
  {
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    
    for (i = 3; i < 6; i++)
    {
      if (fds[i].read_cb)
        FD_SET(i, &rfds);
#ifndef MISSING_SOCKPAIR
      if (fds[i].write_cb)
        FD_SET(i, &wfds);
#endif
    }

#ifdef MISSING_SOCKPAIR
    for (i = 6; i < 8; i++)
    {
      if (fds[i].write_cb)
        FD_SET(i, &wfds);
    }
#endif
      
    /* we have <=6 fds ever, so I don't think select is too painful */
    if (select(NUM_FDS, &rfds, &wfds, NULL, NULL))
    {
      for (i = 0; i < NUM_FDS; i++)
      {
        if (FD_ISSET(i, &rfds) && fds[i].read_cb)
          (*fds[i].read_cb)();
        if (FD_ISSET(i, &wfds) && fds[i].write_cb)
          (*fds[i].write_cb)();
      }
    }
  }

  /* NOTREACHED */
  exit(0);
} /* main() */

void process_command(struct ctrl_command *cmd)
{
  int ret;

  switch (cmd->command)
  {
#ifdef HAVE_LIBCRYPTO
    case CMD_SET_CRYPT_IN_CIPHER:
    {
      unsigned int cipher = *cmd->data;
      
      assert(!in_state.crypt_state.cipher);
      
      switch (cipher)
      {
#ifdef HAVE_BF_CFB64_ENCRYPT
        case CIPHER_BF:
          in_state.crypt_state.cipher = EVP_bf_cfb();
          break;
#endif
#ifdef HAVE_CAST_CFB64_ENCRYPT
        case CIPHER_CAST:
          in_state.crypt_state.cipher = EVP_cast5_cfb();
          break;
#endif
#ifdef HAVE_DES_CFB64_ENCRYPT
        case CIPHER_DES:
          in_state.crypt_state.cipher = EVP_des_cfb();
          break;
#endif
#ifdef HAVE_DES_EDE3_CFB64_ENCRYPT
        case CIPHER_3DES:
          in_state.crypt_state.cipher = EVP_des_ede3_cfb();
          break;
#endif
#ifdef HAVE_IDEA_CFB64_ENCRYPT
        case CIPHER_IDEA:
          in_state.crypt_state.cipher = EVP_idea_cfb();
          break;
#endif
#ifdef HAVE_RC5_32_CFB64_ENCRYPT
        case CIPHER_RC5_8:
          in_state.crypt_state.cipher = EVP_rc5_32_12_16_cfb();
          in_state.crypt_state.rounds = 8;
          break;
        case CIPHER_RC5_12:
          in_state.crypt_state.cipher = EVP_rc5_32_12_16_cfb();
          in_state.crypt_state.rounds = 12;
          break;
        case CIPHER_RC5_16:
          in_state.crypt_state.cipher = EVP_rc5_32_12_16_cfb();
          in_state.crypt_state.rounds = 16;
          break;
#endif
        default:
            send_error("Invalid cipher (servlink/ircd out of sync?): %d",
                       cipher); /* invalid cipher */
            break;
        }
        break;
      }
      case CMD_SET_CRYPT_IN_KEY:
      {
        assert(!in_state.crypt_state.key);

        in_state.crypt_state.keylen = cmd->datalen;
        in_state.crypt_state.key = malloc(cmd->datalen);

        memcpy(in_state.crypt_state.key, cmd->data, cmd->datalen);
        break;
      }
      case CMD_SET_CRYPT_OUT_CIPHER:
      {
        unsigned int cipher = *cmd->data;

        assert(!out_state.crypt_state.cipher);

        switch (cipher)
        {
#ifdef HAVE_BF_CFB64_ENCRYPT
          case CIPHER_BF:
            out_state.crypt_state.cipher = EVP_bf_cfb();
            break;
#endif
#ifdef HAVE_CAST_CFB64_ENCRYPT
          case CIPHER_CAST:
            out_state.crypt_state.cipher = EVP_cast5_cfb();
            break;
#endif
#ifdef HAVE_DES_CFB64_ENCRYPT
          case CIPHER_DES:
            out_state.crypt_state.cipher = EVP_des_cfb();
            break;
#endif
#ifdef HAVE_DES_EDE3_CFB64_ENCRYPT
          case CIPHER_3DES:
            out_state.crypt_state.cipher = EVP_des_ede3_cfb();
            break;
#endif
#ifdef HAVE_IDEA_CFB64_ENCRYPT
          case CIPHER_IDEA:
            out_state.crypt_state.cipher = EVP_idea_cfb();
            break;
#endif
#ifdef HAVE_RC5_32_CFB64_ENCRYPT
          case CIPHER_RC5_8:
            out_state.crypt_state.cipher = EVP_rc5_32_12_16_cfb();
            out_state.crypt_state.rounds = 8;
            break;
          case CIPHER_RC5_12:
            out_state.crypt_state.cipher = EVP_rc5_32_12_16_cfb();
            out_state.crypt_state.rounds = 12;
            break;
          case CIPHER_RC5_16:
            out_state.crypt_state.cipher = EVP_rc5_32_12_16_cfb();
            out_state.crypt_state.rounds = 16;
            break;
#endif
          default:
            send_error("Invalid cipher (servlink/ircd out of sync?): %d",
                       cipher); /* invalid cipher */
          break;
      }
      break;
    }
    case CMD_SET_CRYPT_OUT_KEY:
    {
      assert(!out_state.crypt_state.key);

      out_state.crypt_state.keylen = cmd->datalen;
      out_state.crypt_state.key = malloc(cmd->datalen);

      memcpy(out_state.crypt_state.key, cmd->data, cmd->datalen);
      break;
    }
    case CMD_START_CRYPT_IN:
      if (in_state.crypt ||
          !(in_state.crypt_state.cipher && in_state.crypt_state.key))
        send_error("START_CRYPT_IN sent twice or before setting cipher/key");

      in_state.crypt = 1;
      if (!EVP_DecryptInit(&in_state.crypt_state.ctx,
                           in_state.crypt_state.cipher, NULL, NULL))
        send_error("DecryptInit failed: %s",
                   ERR_error_string(ERR_get_error(), NULL));

      /*
       * XXX - ugly hack to work around OpenSSL bug
       *       if/when OpenSSL fix it, or give proper workaround
       *       use that, and force minimum OpenSSL version
       *
       * Without this hack, BF/256 will fail.
       */
      /* cast to avoid warning */
      *(unsigned int *)( &in_state.crypt_state.ctx.cipher->flags)
        |= EVP_CIPH_VARIABLE_LENGTH;

      if (!EVP_CIPHER_CTX_set_key_length(&in_state.crypt_state.ctx,
                                         in_state.crypt_state.keylen))
        send_error("set_key_length failed: %s",
                   ERR_error_string(ERR_get_error(), NULL));
      
      in_state.crypt_state.ivlen =
        EVP_CIPHER_CTX_iv_length(&in_state.crypt_state.ctx);

      if (in_state.crypt_state.ivlen)
         in_state.crypt_state.iv = calloc(in_state.crypt_state.ivlen, 1);

      if (in_state.crypt_state.rounds)
      {
         if (!EVP_CIPHER_CTX_ctrl(&in_state.crypt_state.ctx,
                                  EVP_CTRL_SET_RC5_ROUNDS,
                                  in_state.crypt_state.rounds,
                                  NULL))
           send_error("SET_RC5_ROUNDS failed: %s",
                      ERR_error_string(ERR_get_error(), NULL));
      }

      if (!EVP_DecryptInit(&in_state.crypt_state.ctx,
                           NULL,
                           in_state.crypt_state.key,
                           in_state.crypt_state.iv))
        send_error("DecryptInit failed: %s",
                   ERR_error_string(ERR_get_error(), NULL));
      break;
    case CMD_START_CRYPT_OUT:
      if (out_state.crypt ||
          !(out_state.crypt_state.cipher && out_state.crypt_state.key))
        send_error("START_CRYPT_OUT called twice/before setting key/cipher");

      out_state.crypt = 1;
      if (!EVP_EncryptInit(&out_state.crypt_state.ctx,
                           out_state.crypt_state.cipher, NULL, NULL))
        send_error("EncryptInit failed: %s",
                   ERR_error_string(ERR_get_error(), NULL));

      /*
       * XXX - ugly hack to work around OpenSSL bug
       *       if/when OpenSSL fix it, or give proper workaround
       *       use that, and force minimum OpenSSL version
       *
       * Without this hack, BF/256 will fail.
       */
      /* cast to avoid warning */
      *(unsigned int *)(&out_state.crypt_state.ctx.cipher->flags)
        |= EVP_CIPH_VARIABLE_LENGTH;

      if (!EVP_CIPHER_CTX_set_key_length(&out_state.crypt_state.ctx,
                                         out_state.crypt_state.keylen))
        send_error("set_key_length failed: %s",
                   ERR_error_string(ERR_get_error(), NULL));

      out_state.crypt_state.ivlen =
        EVP_CIPHER_CTX_iv_length(&out_state.crypt_state.ctx);   

      if (out_state.crypt_state.ivlen)
        out_state.crypt_state.iv = calloc(out_state.crypt_state.ivlen, 1);

      if (out_state.crypt_state.rounds)
      {
         if (!EVP_CIPHER_CTX_ctrl(&out_state.crypt_state.ctx,
                                  EVP_CTRL_SET_RC5_ROUNDS,
                                  out_state.crypt_state.rounds, NULL))
           send_error("SET_RC5_ROUNDS failed: %s",
                      ERR_error_string(ERR_get_error(), NULL));
      }

      if (!EVP_EncryptInit(&out_state.crypt_state.ctx,
                           NULL,
                           out_state.crypt_state.key,
                           out_state.crypt_state.iv))
        send_error("EncryptInit failed: %s",
                   ERR_error_string(ERR_get_error(), NULL));
      break;
#endif
#ifdef HAVE_LIBZ
    case CMD_START_ZIP_IN:
      assert(!in_state.zip);
      
      in_state.zip_state.z_stream.total_in = 0;
      in_state.zip_state.z_stream.total_out = 0;
      in_state.zip_state.z_stream.zalloc = (alloc_func)0;
      in_state.zip_state.z_stream.zfree = (free_func)0;
      in_state.zip_state.z_stream.data_type = Z_ASCII;
      if ((ret = inflateInit(&in_state.zip_state.z_stream)) != Z_OK)
        send_error("inflateInit failed: %d", ret);
      in_state.zip = 1;
      break;
    case CMD_SET_ZIP_OUT_LEVEL:
      out_state.zip_state.level = *cmd->data;
      if ((out_state.zip_state.level < -1) ||
          (out_state.zip_state.level > 9))
        send_error("invalid compression level %d",
                   out_state.zip_state.level);
      break;
    case CMD_START_ZIP_OUT:
      assert(!out_state.zip);
      
      out_state.zip_state.z_stream.total_in = 0;
      out_state.zip_state.z_stream.total_out = 0;
      out_state.zip_state.z_stream.zalloc = (alloc_func)0;
      out_state.zip_state.z_stream.zfree = (free_func)0;
      out_state.zip_state.z_stream.data_type = Z_ASCII;

      if (out_state.zip_state.level <= 0)
        out_state.zip_state.level = Z_DEFAULT_COMPRESSION;

      if ((ret = deflateInit(&out_state.zip_state.z_stream,
                             out_state.zip_state.level)) != Z_OK)
        send_error("deflateInit failed: %d", ret);
      out_state.zip = 1;
      break;
    case CMD_ZIPSTATS:
      send_zipstats();
      break;
#endif
    case CMD_INJECT_RECVQ:
      process_recvq(cmd->data, cmd->datalen);
      break;
    case CMD_INJECT_SENDQ:
      process_sendq(cmd->data, cmd->datalen);
      break;
    case CMD_INIT:
      assert(!(in_state.active || out_state.active));
      in_state.active = 1;
      out_state.active = 1;
      fds[CONTROL_FD_R].read_cb = read_ctrl;
      fds[CONTROL_FD_W].write_cb = NULL;
      fds[LOCAL_FD_R].read_cb = read_data;
      fds[LOCAL_FD_W].write_cb = NULL;
      fds[REMOTE_FD_R].read_cb = read_net;
      fds[REMOTE_FD_W].write_cb = NULL;
      break;
    default:
      /* invalid command */
      send_error("Invalid command (servlink/ircd out of sync?): %d",
                 cmd->command);
      break;
  }
}

int checkError(int ret, int io, int fd)
{
  if (ret > 0) /* no error */
    return ret;
  if (ret == 0) /* EOF */
    exit(1);
  
  /* ret == -1.. */
  switch (errno) {
    case EINPROGRESS:
    case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
    case EAGAIN:
#endif
    case EALREADY:
    case EINTR:
#ifdef ERESTART
    case ERESTART:
#endif
      /* non-fatal error, 0 bytes read */
      return 0;
  }

  /* fatal error */
  send_error("%s failed on %s: %s",
             IO_TYPE(io),
             FD_NAME(fd),
             strerror(errno));
}
