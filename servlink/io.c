/************************************************************************
 *   IRC - Internet Relay Chat, servlink/io.c
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
#include <sys/socket.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef HAVE_LIBCRYPTO
#include <openssl/evp.h>
#endif
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#include "servlink.h"
#include "io.h"
#include "control.h"

static struct ctrl_command cmd = {0, 0, 0, 0, NULL};

#if defined( HAVE_LIBCRYPTO ) || defined( HAVE_LIBZ )
static unsigned char tmp_buf[BUFLEN];
#endif
#if defined( HAVE_LIBZ ) && defined( HAVE_LIBZ )
static unsigned char tmp2_buf[BUFLEN];
#endif

static unsigned char ctrl_buf[256] = "";
static unsigned int  ctrl_len = 0;
static unsigned int  ctrl_ofs = 0;

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

    ret = checkError(ret, IO_WRITE, fd);

    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);

    /* sleep until we can write to the fd */
    while(1)
    {
      ret = select(fd+1, NULL, &wfds, NULL, NULL);
      
      if (ret > 0) /* break out so we can write */
        break;

      if (ret < 0) /* error ? */
        checkError(ret, IO_SELECT, fd); /* exit on fatal errors */
      
      /* loop on non-fatal errors */
    }
  }
}

/*
 * process_sendq:
 * 
 * used before CMD_INIT to pass contents of SendQ from ircd
 * to servlink.  This data must _not_ be encrypted/compressed.
 */
void process_sendq(unsigned char *data, unsigned int datalen)
{
  send_data_blocking(REMOTE_FD_W, data, datalen);
}

/*
 * process_recvq:
 *
 * used before CMD_INIT to pass contents of RecvQ from ircd
 * to servlink.  This data must be decrypted/decopmressed before
 * sending back to the ircd.
 */
void process_recvq(unsigned char *data, unsigned int datalen)
{
  int ret;
  unsigned char *buf;
  unsigned int  blen;

  buf = data;
  blen = datalen;

  assert(datalen <= READLEN);
#ifdef HAVE_LIBCRYPTO
  if (in_state.crypt)
  {
    assert(EVP_DecryptUpdate(&in_state.crypt_state.ctx,
                             tmp_buf, &blen,
                             data, datalen));
    assert(blen == datalen);
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
    in_state.zip_state.z_stream.avail_out = BUFLEN;

    buf = tmp2_buf;
    while(in_state.zip_state.z_stream.avail_in)
    {
      if ((ret = inflate(&in_state.zip_state.z_stream,
                         Z_NO_FLUSH)) != Z_OK)
        send_error("Inflate failed: %d");

      blen = BUFLEN - in_state.zip_state.z_stream.avail_out;

      if (in_state.zip_state.z_stream.avail_in)
      {
        send_data_blocking(LOCAL_FD_W, buf, blen);
        blen = 0;
        in_state.zip_state.z_stream.next_out = buf;
        in_state.zip_state.z_stream.avail_out = BUFLEN;
      }
    }

    if (!blen)
      return;
  }
#endif

  assert(blen);
  
  send_data_blocking(LOCAL_FD_W, buf, blen);
}

void send_zipstats(void)
{
  int i = 0;
  int ret;

  assert(in_state.active && out_state.active);
  assert(in_state.zip && out_state.zip);

  ctrl_buf[i++] = RPL_ZIPSTATS;
  ctrl_buf[i++] = 0;
  ctrl_buf[i++] = 16;
  ctrl_buf[i++] = ((in_state.zip_state.z_stream.total_out >> 24) & 0xFF);
  ctrl_buf[i++] = ((in_state.zip_state.z_stream.total_out >> 16) & 0xFF);
  ctrl_buf[i++] = ((in_state.zip_state.z_stream.total_out >>  8) & 0xFF);
  ctrl_buf[i++] = ((in_state.zip_state.z_stream.total_out      ) & 0xFF);

  ctrl_buf[i++] = ((in_state.zip_state.z_stream.total_in >> 24) & 0xFF);
  ctrl_buf[i++] = ((in_state.zip_state.z_stream.total_in >> 16) & 0xFF);
  ctrl_buf[i++] = ((in_state.zip_state.z_stream.total_in >>  8) & 0xFF);
  ctrl_buf[i++] = ((in_state.zip_state.z_stream.total_in      ) & 0xFF);

  ctrl_buf[i++] = ((out_state.zip_state.z_stream.total_in >> 24) & 0xFF);
  ctrl_buf[i++] = ((out_state.zip_state.z_stream.total_in >> 16) & 0xFF);
  ctrl_buf[i++] = ((out_state.zip_state.z_stream.total_in >>  8) & 0xFF);
  ctrl_buf[i++] = ((out_state.zip_state.z_stream.total_in      ) & 0xFF);

  ctrl_buf[i++] = ((out_state.zip_state.z_stream.total_out >> 24) & 0xFF);
  ctrl_buf[i++] = ((out_state.zip_state.z_stream.total_out >> 16) & 0xFF);
  ctrl_buf[i++] = ((out_state.zip_state.z_stream.total_out >>  8) & 0xFF);
  ctrl_buf[i++] = ((out_state.zip_state.z_stream.total_out      ) & 0xFF);

  in_state.zip_state.z_stream.total_in = 0;
  in_state.zip_state.z_stream.total_out = 0;
  out_state.zip_state.z_stream.total_in = 0;
  out_state.zip_state.z_stream.total_out = 0;
  
  ret = checkError(write(CONTROL_FD_W, ctrl_buf, i), IO_WRITE, CONTROL_FD_W);
  if (ret < i)
  {
    /* write incomplete, register write cb */
    fds[CONTROL_FD_W].write_cb = write_ctrl;
    /*  deregister read_cb */
    fds[CONTROL_FD_R].read_cb = NULL;
    ctrl_ofs = ret;
    ctrl_len = i - ret;
    return;
  }
}

/* send_error
 *   - we ran into some problem, make a last ditch effort to 
 *     flush the control fd sendq, then (blocking) send an
 *     error message over the control fd.
 */
void send_error(unsigned char *message, ...)
{
  va_list args;
  static int sending_error = 0;
  struct linger linger_opt = { 1, 30 }; /* wait 30 seconds */                     int len;

  if (!sending_error)
  {
    sending_error = 1;

    if(ctrl_len) /* attempt to flush any data we have... */
    {
      send_data_blocking(CONTROL_FD_W, (ctrl_buf+ctrl_ofs), ctrl_len);
    }

    /* prepare the message, in in_buf, since we won't be using it again.. */
    in_state.buf[0] = RPL_ERROR;
    in_state.buf[1] = 0;
    in_state.buf[2] = 0;
    
    va_start(args, message);
    len = vsprintf(in_state.buf+3, message, args);
    va_end(args);

    in_state.buf[len++] = '\0';
    in_state.buf[1] = len >> 8;
    in_state.buf[2] = len & 0xFF;
    len+=3;

    send_data_blocking(CONTROL_FD_W, in_state.buf, len);

#ifndef MISSING_SOCKPAIR /* only do this to a socket :) */
    /* XXX - is this portable? */
    setsockopt(CONTROL_FD_W, SOL_SOCKET, SO_LINGER, &linger_opt,
               sizeof(struct linger));
#endif
  }

  /* well, we've tried... */
  assert(0);
  exit(1); /* now abort */
}

/* read_ctrl
 *      called when a command is waiting on the control pipe
 */
void read_ctrl(void)
{
  int ret;
  unsigned char tmp[2];
  unsigned char *len;

  if (cmd.command == 0) /* we don't have a command yet */
  {
    cmd.gotdatalen = 0;
    cmd.readdata = 0;
    cmd.data = NULL;

    /* read the command */
    if (!(ret = checkError(read(CONTROL_FD_R, &cmd.command, 1),
                           IO_READ, CONTROL_FD_R)))
      return;
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
      if (cmd.gotdatalen < 2)
      {
        len = tmp;
        if (!(ret = checkError(read(CONTROL_FD_R, len,
                                    (2 - cmd.gotdatalen)),
                               IO_READ, CONTROL_FD_R)))
          return;

        if (cmd.gotdatalen == 0)
        {
          cmd.datalen = *len << 8;
          cmd.gotdatalen++;
          ret--;
          len++;
        }
        if (ret && (cmd.gotdatalen == 1))
        {
          cmd.datalen |= *len;
          cmd.gotdatalen++;
          if (cmd.datalen > 0)
            cmd.data = calloc(cmd.datalen, 1);
        }
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
    case CMD_ZIPSTATS:
#endif
    case CMD_INIT:
      cmd.datalen = -1;
      break;
    default:
      /* invalid command */
      send_error("Unsupported command (servlink/ircd out of sync?): %d",
                   cmd.command);
      break;
  }

  if (cmd.readdata < cmd.datalen) /* try to get any remaining data */
  {
    if (!(ret = checkError(read(CONTROL_FD_R,
                                (cmd.data + cmd.readdata),
                                cmd.datalen - cmd.readdata),
                           IO_READ, CONTROL_FD_R)))
      return;

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

void write_ctrl(void)
{
  int ret;

  assert(ctrl_len);

  if (!(ret = checkError(write(CONTROL_FD_W, (ctrl_buf + ctrl_ofs),
                               ctrl_len),
                         IO_WRITE, CONTROL_FD_W)))
    return; /* no data waiting */

  ctrl_len -= ret;

  if (!ctrl_len)
  {
    /* write completed, de-register write cb */
    fds[CONTROL_FD_W].write_cb = NULL;
    /* reregister read_cb */
    fds[CONTROL_FD_R].read_cb = read_ctrl;
    ctrl_ofs = 0;
  }
  else
    ctrl_ofs += ret;
}

void read_data(void)
{
  int ret;
  unsigned char *buf = out_state.buf;
  int  blen;
  
  assert(!out_state.len);

#if defined(HAVE_LIBZ) || defined(HAVE_LIBCRYPTO)
  if (out_state.zip || out_state.crypt)
    buf = tmp_buf;
#endif
    
  while ((ret = checkError(read(LOCAL_FD_R, buf, READLEN),
                           IO_READ, LOCAL_FD_R)))
  {
    blen = ret;
#ifdef HAVE_LIBZ
    if (out_state.zip)
    {
      out_state.zip_state.z_stream.next_in = buf;
      out_state.zip_state.z_stream.avail_in = ret;

      buf = out_state.buf;
#ifdef HAVE_LIB_CRYPTO
      if (out_state.crypt)
        buf = tmp2_buf;
#endif
      out_state.zip_state.z_stream.next_out = buf;
      out_state.zip_state.z_stream.avail_out = BUFLEN;
      assert(deflate(&out_state.zip_state.z_stream,
                     Z_PARTIAL_FLUSH) == Z_OK);
      assert(out_state.zip_state.z_stream.avail_out);
      assert(out_state.zip_state.z_stream.avail_in == 0);
      blen = BUFLEN - out_state.zip_state.z_stream.avail_out;
      assert(blen);
    }
#endif

#ifdef HAVE_LIBCRYPTO
    if (out_state.crypt)
    {
      /* encrypt data */
      ret = blen;
      assert( EVP_EncryptUpdate(&out_state.crypt_state.ctx,
                                out_state.buf, &blen,
                                buf, ret) );
      assert(blen == ret);
    }
#endif
    
    assert(blen);
    ret = checkError(write(REMOTE_FD_W, out_state.buf, blen),
                     IO_WRITE, REMOTE_FD_W);
    if (ret < blen)
    {
      /* write incomplete, register write cb */
      fds[REMOTE_FD_W].write_cb = write_net;
      /*  deregister read_cb */
      fds[LOCAL_FD_R].read_cb = NULL;
      out_state.ofs = ret;
      out_state.len = blen - ret;
      return;
    }
#if defined(HAVE_LIBZ) || defined(HAVE_LIBCRYPTO)
    if (out_state.zip || out_state.crypt)
      buf = tmp_buf;
#endif
  }
}

void write_net(void)
{
  int ret;

  assert(out_state.len);

  if (!(ret = checkError(write(REMOTE_FD_W,
                               (out_state.buf + out_state.ofs),
                               out_state.len),
                         IO_WRITE, REMOTE_FD_W)))
    return; /* no data waiting */

  out_state.len -= ret;

  if (!out_state.len)
  {
    /* write completed, de-register write cb */
    fds[REMOTE_FD_W].write_cb = NULL;
    /* reregister read_cb */
    fds[LOCAL_FD_R].read_cb = read_data;
    out_state.ofs = 0;
  }
  else
    out_state.ofs += ret;
}

void read_net(void)
{
  int ret;
  int ret2;
  unsigned char *buf = in_state.buf;
  int  blen;

  assert(!in_state.len);

#if defined(HAVE_LIBCRYPTO) || defined(HAVE_LIBZ)
  if (in_state.crypt || in_state.zip)
    buf = tmp_buf;
#endif

  while ((ret = checkError(read(REMOTE_FD_R, buf, READLEN),
                           IO_READ, REMOTE_FD_R)))
  {
    blen = ret;
#ifdef HAVE_LIBCRYPTO
    if (in_state.crypt)
    {
      /* decrypt data */
      buf = in_state.buf;
#ifdef HAVE_LIBZ
      if (in_state.zip)
        buf = tmp2_buf;
#endif
      assert(EVP_DecryptUpdate(&in_state.crypt_state.ctx,
                               buf, &blen,
                               tmp_buf, ret));
      assert(blen == ret);
    }
#endif
    
#ifdef HAVE_LIBZ
    if (in_state.zip)
    {
      /* decompress data */
      in_state.zip_state.z_stream.next_in = buf;
      in_state.zip_state.z_stream.avail_in = ret;
      in_state.zip_state.z_stream.next_out = in_state.buf;
      in_state.zip_state.z_stream.avail_out = BUFLEN;

      while (in_state.zip_state.z_stream.avail_in)
      {
        if ((ret2 = inflate(&in_state.zip_state.z_stream,
                            Z_NO_FLUSH)) != Z_OK)
          send_error("inflate failed: %d", ret2);

        blen = BUFLEN - in_state.zip_state.z_stream.avail_out;

        if (in_state.zip_state.z_stream.avail_in)
        {
          if (blen)
          {
            send_data_blocking(LOCAL_FD_W, in_state.buf, BUFLEN);
            blen = 0;
          }

          in_state.zip_state.z_stream.next_out = in_state.buf;
          in_state.zip_state.z_stream.avail_out = BUFLEN;
        }
      }

      if (!blen)
        return; /* that didn't generate any decompressed input.. */
    }
#endif

    ret = checkError(write(LOCAL_FD_W, in_state.buf, blen),
                     IO_WRITE, LOCAL_FD_W);

    if (ret < blen)
    {
      in_state.ofs = ret;
      in_state.len = blen - ret;
      /* write incomplete, register write cb */
      fds[LOCAL_FD_W].write_cb = write_data;
      /* deregister read_cb */
      fds[REMOTE_FD_R].read_cb = NULL;
      return;
    }
#if defined(HAVE_LIBCRYPTO) || defined(HAVE_LIBZ)
    if (in_state.crypt || in_state.zip)
      buf = tmp_buf;
#endif
  }
}

void write_data(void)
{
  int ret;

  assert(in_state.len);

  if (!(ret = checkError(write(LOCAL_FD_W,
                               (in_state.buf + in_state.ofs),
                               in_state.len),
                         IO_WRITE, LOCAL_FD_W)))
    return;

  in_state.len -= ret;

  if (!in_state.len)
  {
    /* write completed, de-register write cb */
    fds[LOCAL_FD_W].write_cb = NULL;
    /* reregister read_cb */
    fds[REMOTE_FD_R].read_cb = read_net;
    in_state.ofs = 0;
  }
  else
    in_state.ofs += ret;
}
