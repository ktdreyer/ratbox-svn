/************************************************************************
 *   IRC - Internet Relay Chat, servlink/io.h
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

extern void write_data(void);
extern void read_data(void);
extern void write_ctrl(void);
extern void read_ctrl(void);
extern void write_net(void);
extern void read_net(void);
extern void process_recvq(unsigned char *, int);
extern void process_sendq(unsigned char *, int);
extern void send_data_blocking(int fd, unsigned char *data, int datalen);
