/************************************************************************
 *   IRC - Internet Relay Chat, servlink/control.h
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

#define CMD_SET_ZIP_OUT_LEVEL           1       /* data */
#define CMD_START_ZIP_OUT               2
/*#define CMD_END_ZIP_OUT                 3*/
#define CMD_START_ZIP_IN                4
/*#define CMD_END_ZIP_IN                  5*/
#define CMD_SET_CRYPT_IN_CIPHER         6       /* data */
#define CMD_SET_CRYPT_IN_KEY            7       /* data */
#define CMD_START_CRYPT_IN              8
/*#define CMD_END_CRYPT_IN                9*/
#define CMD_SET_CRYPT_OUT_CIPHER        10      /* data */
#define CMD_SET_CRYPT_OUT_KEY           11      /* data */
#define CMD_START_CRYPT_OUT             12
/*#define CMD_END_CRYPT_OUT               13*/
#define CMD_INJECT_RECVQ                14      /* data */
#define CMD_INJECT_SENDQ                15      /* data */
#define CMD_INIT                        16

struct ctrl_command
{
  int command;
  int datalen;
  int gotdatalen;
  int readdata;
  unsigned char *data;
};

extern void process_command(struct ctrl_command *);
