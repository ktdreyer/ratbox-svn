/************************************************************************
 *   IRC - Internet Relay Chat, servlink/src/io.c
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

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <openssl/evp.h>
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#include "servlink.h"
#include "io.h"
#include "control.h"

static struct ctrl_command cmd = {0, 0, 0, 0, NULL};

#define BUFLEN                  2048

#ifdef HAVE_LIBZ
#define ZIP_BUFLEN              BUFLEN * 8 /* allow for decompression */
#else
#define ZIP_BUFLEN              BUFLEN
#endif

static char out_buf[ZIP_BUFLEN];
static int  out_ofs = 0;
static int  out_len = 0;
static char in_buf[ZIP_BUFLEN];
static int  in_ofs = 0;
static int  in_len = 0;

#if defined( HAVE_LIBCRYPTO ) || defined( HAVE_LIBZ )
static char tmp_buf[ZIP_BUFLEN];
#endif
#if defined( HAVE_LIBZ ) && defined( HAVE_LIBZ )
static char tmp2_buf[ZIP_BUFLEN];
#endif

void send_data_blocking(int fd, unsigned char *data, int datalen)
{
  int ret;
  fd_set wfds;

  while (1)
  {
    ret = write(fd, data, datalen);

    if (ret == datalen)
      return;
    else if ( ret > 0)
    {
      data += ret;
      datalen -= ret;
    }
    if (ret == 0 || errno != EAGAIN)
      exit(1);

    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);

    /* block until we can write to the fd */
    while((ret = select(fd+1, NULL, &wfds, NULL, NULL)) == 0)
      ;
    if (ret == -1)
      exit(1);
  }
}

/*
 * process_sendq:
 * 
 * used before CMD_INIT to pass contents of SendQ from ircd
 * to servlink.  This data must _not_ be encrypted/compressed.
 */
void process_sendq(unsigned char *data, int datalen)
{
  /* we can 'block' here, as we don't have to listen
   * to any other fds anyway
   */
  send_data_blocking(REMOTE_FD, data, datalen);
}

/*
 * process_recvq:
 *
 * used before CMD_INIT to pass contents of RecvQ from ircd
 * to servlink.  This data must be decrypted/decopmressed before
 * sending back to the ircd.
 */
void process_recvq(unsigned char *data, int datalen)
{
  int ret;
  char *buf;
  int  blen;

  buf = data;
  blen = datalen;

#ifdef HAVE_LIBCRYPTO
  if (in_state.crypt)
  {
    /* set where to store decrypted data */
    blen = BUFLEN;
    assert(EVP_DecryptUpdate(&in_state.crypt_state.ctx,
                             tmp_buf, &blen,
                             data, datalen));
    buf = tmp_buf;
  }
#endif

#ifdef HAVE_LIBZ
  if (in_state.zip)
  {
    /* decompress data */
    in_state.zip_state.z_stream.next_in = buf;
    in_state.zip_state.z_stream.avail_in = blen;
    in_state.zip_state.z_stream.next_out = tmp2_buf;
    in_state.zip_state.z_stream.avail_out = ZIP_BUFLEN;
    if ((ret = inflate(&in_state.zip_state.z_stream,
                       Z_NO_FLUSH)) != Z_OK)
      exit(ret);
    assert(in_state.zip_state.z_stream.avail_out);
    assert(in_state.zip_state.z_stream.avail_in == 0);
    blen = ZIP_BUFLEN - in_state.zip_state.z_stream.avail_out;

    buf = tmp2_buf;
  }
#endif
  
  send_data_blocking(LOCAL_FD, buf, blen);
}

/* read_ctrl
 *      called when a command is waiting on the control pipe
 */
void read_ctrl(void)
{
  int ret;
  unsigned char tmp;
 
  while (1) /* read as many commands as possible */
  {
    if (cmd.command == 0) /* we don't have a command yet */
    {
      cmd.gotdatalen = 0;
      cmd.readdata = 0;
      cmd.data = NULL;

      /* read the command */
      ret = read(CONTROL_FD, &cmd.command, 1);
      if (ret == -1 && errno == EAGAIN)
        return;
      else if (ret <= 0)
        exit(1);
    }

    /* read datalen for commands including data */
    switch (cmd.command)
    {
#ifdef HAVE_LIBCRYPTO
      case CMD_SET_CRYPT_IN_CIPHER:
      case CMD_SET_CRYPT_IN_KEY:
      case CMD_SET_CRYPT_OUT_CIPHER:
      case CMD_SET_CRYPT_OUT_KEY:
#endif
#ifdef HAVE_LIBZ
      case CMD_SET_ZIP_OUT_LEVEL:
#endif
      case CMD_INJECT_RECVQ:
      case CMD_INJECT_SENDQ:
        if (cmd.gotdatalen == 0)
        {
          ret = read(CONTROL_FD, &tmp, 1);
          if (ret == -1 && errno == EAGAIN)
            return;
          else if (ret <= 0)
            exit(1);

          cmd.datalen = tmp << 8;
          cmd.gotdatalen = 1;
        }
        if (cmd.gotdatalen == 1)
        {
          ret = read(CONTROL_FD, &tmp, 1);
          if (ret == -1 && errno == EAGAIN)
            return;
          else if (ret <= 0)
            exit(1);

          cmd.datalen |= tmp;
          cmd.gotdatalen = 2;

          if (cmd.datalen > 0)
            cmd.data = calloc(cmd.datalen, 1);
        }
        break;
/*
      case CMD_END_ZIP_IN:
      case CMD_END_ZIP_OUT:
      case CMD_END_CRYPT_IN:
      case CMD_END_CRYPT_OUT:
 */
#ifdef HAVE_LIBCRYPTO
      case CMD_START_CRYPT_IN:
      case CMD_START_CRYPT_OUT:
#endif
#ifdef HAVE_LIBZ
      case CMD_START_ZIP_IN:
      case CMD_START_ZIP_OUT:
#endif
      case CMD_INIT:
        cmd.datalen = -1;
        break;
      default:
        /* invalid command */
        exit(1);
        break;
    }

    if (cmd.readdata < cmd.datalen) /* try to get any remaining data */
    {
      ret = read(CONTROL_FD, (cmd.data + cmd.readdata),
                 cmd.datalen - cmd.readdata);

      if (ret == 0 || (ret == -1 && errno != EAGAIN))
        exit(1); /* EOF or error */
      else if (ret == -1)
        return; /* no data */

      cmd.readdata += ret;

      if (cmd.readdata < cmd.datalen)
        return;
    }

    /* we now have the command and any data */
    process_command(&cmd);

    if (cmd.datalen > 0)
      free(cmd.data);
    cmd.command = 0;
  }
}

void read_data(void)
{
  int ret;
  char *buf = out_buf;
  int  blen;
  
  if (out_len)
    exit(1);

#if defined(HAVE_LIBZ) || defined(HAVE_LIBCRYPTO)
  if (out_state.zip || out_state.crypt)
    buf = tmp_buf;
#endif
    
  while ((ret = read(LOCAL_FD, buf, BUFLEN)) > 0)
  {
    blen = ret;
#ifdef HAVE_LIBZ
    if (out_state.zip)
    {
      out_state.zip_state.z_stream.next_in = buf;
      out_state.zip_state.z_stream.avail_in = ret;

      buf = out_buf;
#ifdef HAVE_LIB_CRYPTO
      if (out_state.crypt)
        buf = tmp2_buf;
#endif
      out_state.zip_state.z_stream.next_out = buf;
      out_state.zip_state.z_stream.avail_out = ZIP_BUFLEN;
      assert(deflate(&out_state.zip_state.z_stream,
                     Z_PARTIAL_FLUSH) == Z_OK);
      assert(out_state.zip_state.z_stream.avail_out);
      assert(out_state.zip_state.z_stream.avail_in == 0);
      blen = ZIP_BUFLEN - out_state.zip_state.z_stream.avail_out;
    }
#endif

#ifdef HAVE_LIBCRYPTO
    if (out_state.crypt)
    {
      /* encrypt data */
      ret = blen;
      blen = BUFLEN;
      assert( EVP_EncryptUpdate(&out_state.crypt_state.ctx,
                                out_buf, &blen,
                                buf, ret) );
    }
#endif
    
    ret = write(REMOTE_FD, out_buf, blen);
    if (ret <= 0)
    {
      if (ret == -1 && errno == EAGAIN)
        ret = 0;
      else
        exit(1);
    }

    if (ret < blen)
    {
      /* write incomplete, register write cb */
      fds[REMOTE_FD].write_cb = write_net;
      /*  deregister read_cb */
      fds[LOCAL_FD].read_cb = NULL;
      out_ofs = ret;
      out_len = blen - ret;
      return;
    }
  }

  /* read failed */
  if (ret == 0 || errno != EAGAIN)
    exit(1); /* EOF or error */
}

void write_net(void)
{
  int ret;

  if (!out_len)
    exit(1);

  ret = write(REMOTE_FD, out_buf+out_ofs, out_len);

  if (ret == -1 && errno == EAGAIN)
    return;
  else if (ret <= 0)
    exit(1);

  out_len -= ret;

  if (!out_len)
  {
    /* write completed, de-register write cb */
    fds[REMOTE_FD].write_cb = NULL;
    /* reregister read_cb */
    fds[LOCAL_FD].read_cb = read_data;
    out_ofs = 0;
  }
  else
    out_ofs += ret;
}

void read_net(void)
{
  int ret;
  int ret2;
  char *buf = in_buf;
  int  blen;

  if (in_len)
    exit(1);

#if defined(HAVE_LIBCRYPTO) || defined(HAVE_LIBZ)
  if (in_state.crypt || in_state.zip)
    buf = tmp_buf;
#endif

  while ((ret = read(REMOTE_FD, buf, BUFLEN)) > 0)
  {
    blen = ret;
#ifdef HAVE_LIBCRYPTO
    if (in_state.crypt)
    {
      /* decrypt data */
      buf = in_buf;
#ifdef HAVE_LIBZ
      if (in_state.zip)
        buf = tmp2_buf;
#endif
      blen = BUFLEN;
      assert( EVP_DecryptUpdate(&in_state.crypt_state.ctx,
                       buf, &blen,
                       tmp_buf, ret) );
    }
#endif
    
#ifdef HAVE_LIBZ
    if (in_state.zip)
    {
      /* decompress data */
      in_state.zip_state.z_stream.next_in = buf;
      in_state.zip_state.z_stream.avail_in = ret;
      in_state.zip_state.z_stream.next_out = in_buf;
      in_state.zip_state.z_stream.avail_out = ZIP_BUFLEN;
      if ((ret2 = inflate(&in_state.zip_state.z_stream,
                          Z_NO_FLUSH)) != Z_OK)
        exit(ret2);
      assert(in_state.zip_state.z_stream.avail_out);
      assert(in_state.zip_state.z_stream.avail_in == 0);
      blen = ZIP_BUFLEN - in_state.zip_state.z_stream.avail_out;
    }
#endif

    ret = write(LOCAL_FD, in_buf, blen);
    if (ret <= 0)
    {
      if (ret == -1 && errno == EAGAIN)
        ret = 0;
      else
        exit(1);
    }

    if (ret < blen)
    {
      in_ofs = ret;
      in_len = blen - ret;
      /* write incomplete, register write cb */
      fds[LOCAL_FD].write_cb = write_data;
      /* deregister read_cb */
      fds[REMOTE_FD].read_cb = NULL;
      return;
    }
  }

  if (ret == 0 || errno != EAGAIN)
    exit(1); /* EOF or error */
}

void write_data(void)
{
  int ret;

  if (!in_len)
    exit(1);

  ret = write(LOCAL_FD, in_buf+in_ofs, in_len);

  if (ret == -1 && errno == EAGAIN)
    return;
  else if (ret <= 0)
    exit(1);

  in_len -= ret;

  if (!in_len)
  {
    /* write completed, de-register write cb */
    fds[LOCAL_FD].write_cb = NULL;
    /* reregister read_cb */
    fds[REMOTE_FD].read_cb = read_net;
    in_ofs = 0;
  }
  else
    in_ofs += ret;
}
