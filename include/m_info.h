/************************************************************************
 *   IRC - Internet Relay Chat, ircd/m_info.h
 *   Copyright (C) 1990 Jarkko Oikarinen
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
 * $Id$
 */
#ifndef INCLUDED_m_info_h
#define INCLUDED_m_info_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif

typedef struct Information
{
  char* name;        /* name of item */
  char* strvalue;    /* value of item if it's a boolean */
  int   intvalue;    /* value of item if it's an integer */
  char* desc;        /* short description of item */
} Info;

/*
 * only define MyInformation if we are compiling m_info.c
 */
#ifdef DEFINE_M_INFO_DATA

Info MyInformation[] = {

  { "ANTI_DRONE_FLOOD", "ON", 0, "Anti Flood for Drones" },
  { "ANTI_SPAMBOT", "ON", 0, "Spam Bot Detection" },

#ifdef ANTI_SPAMBOT_WARN_ONLY
  { "ANTI_SPAMBOT_WARN_ONLY", "ON", 0, "Warn Operators of Possible Spam Bots" },
#else
  { "ANTI_SPAMBOT_WARN_ONLY", "OFF", 0, "Warn Operators of Possible Spam Bots" },
#endif /* ANTI_SPAMBOT_WARN_ONLY */

#ifdef ANTI_SPAM_EXIT_MESSAGE
  { "ANTI_SPAM_EXIT_MESSAGE", "ON", 0, "Do not broadcast Spam Bots' exit messages" },
#else
  { "ANTI_SPAM_EXIT_MESSAGE", "OFF", 0, "Do not broadcast Spam Bots' exit messages" },
#endif /* ANTI_SPAM_EXIT_MESSAGE */

#ifdef ANTI_SPAM_EXIT_MESSAGE_TIME
  { "ANTI_SPAM_EXIT_MESSAGE_TIME", "", ANTI_SPAM_EXIT_MESSAGE_TIME, "Delay before Allowing Spam Bot Exit Messages" },
#else
  { "ANTI_SPAM_EXIT_MESSAGE_TIME", "NONE", 0, "Delay before Allowing Spam Bot Exit Messages" },
#endif /* ANTI_SPAM_EXIT_MESSAGE_TIME */

  { "BUFFERPOOL", "", BUFFERPOOL, "Maximum size of all SendQs" },

#ifdef CHROOTDIR
  { "CHROOTDIR", "ON", 0, "chroot() before reading Configuration File" },
#else
  { "CHROOTDIR", "OFF", 0, "chroot() before reading Configuration File" },
#endif

#ifdef CLIENT_FLOOD
  { "CLIENT_FLOOD", "", CLIENT_FLOOD, "Client Excess Flood Threshold" },
#else
  { "CLIENT_FLOOD", "OFF", 0, "Client Excess Flood Threshold" },
#endif /* CLIENT_FLOOD */

#ifdef CMDLINE_CONFIG
  { "CMDLINE_CONFIG", "ON", 0, "Allow Command Line Specification of Config File" },
#else
  { "CMDLINE_CONFIG", "OFF", 0, "Allow Command Line Specification of Config File" },
#endif /* CMDLINE_CONFIG */

#ifdef CPATH
  { "CPATH", CPATH, 0, "Path to Main Configuration File" },
#else
  { "CPATH", "NONE", 0, "Path to Main Configuration File" },
#endif /* CPATH */

#ifdef CRYPT_OPER_PASSWORD
  { "CRYPT_OPER_PASSWORD", "ON", 0, "Encrypt Operator Passwords" },
#else
  { "CRYPT_OPER_PASSWORD", "OFF", 0, "Encrypt Operator Passwords" },
#endif /* CRYPT_OPER_PASSWORD */

#ifdef CRYPT_LINK_PASSWORD
  { "CRYPT_LINK_PASSWORD", "ON", 0, "Encrypt Server Passwords" },
#else
  { "CRYPT_LINK_PASSWORD", "OFF", 0, "Encrypt Server Passwords" },
#endif /* CRYPT_LINK_PASSWORD */

#ifdef CUSTOM_ERR
  { "CUSTOM_ERR", "ON", 0, "Customized error messages" },
#else
  { "CUSTOM_ERR", "OFF", 0, "Customized error messages" },
#endif /* CUSTOM_ERR */

#ifdef DEBUGMODE
  { "DEBUGMODE", "ON", 0, "Debugging Mode" },
#else
  { "DEBUGMODE", "OFF", 0, "Debugging Mode" },
#endif /* DEBUGMODE */

#ifdef DNS_DEBUG
  { "DNS_DEBUG", "ON", 0, "Dns Debugging" },
#else
  { "DNS_DEBUG", "OFF", 0, "Dns Debugging" },
#endif /* DNS_DEBUG */

#ifdef DO_IDENTD
  { "DO_IDENTD", "ON", 0, "Perform identd checks" },
#else
  { "DO_IDENTD", "OFF", 0, "Perform identd checks" },
#endif /* DO_IDENTD */

#ifdef DPATH
  { "DPATH", DPATH, 0, "Directory Containing Configuration Files" },
#else
  { "DPATH", "NONE", 0, "Directory Containing Configuration Files" },
#endif /* DPATH */

#ifdef DLPATH
  { "DLPATH", DLPATH, 0, "Path to D-line File" },
#else
  { "DLPATH", "NONE", 0, "Path to D-line File" },
#endif /* DLPATH */

  { "FLUD", "ON", 0, "CTCP Flood Detection and Protection" },

  { "FLUD_NUM", "", FLUD_NUM, "Number of Messages to Trip Alarm" },

  { "FLUD_TIME", "", FLUD_TIME, "Time Window in which a Flud occurs" },

  { "FLUD_BLOCK", "", FLUD_BLOCK, "Seconds to Block Fluds" },

#ifdef GLINE_TIME
  { "GLINE_TIME", "", GLINE_TIME, "Expire Time for Glines" },
#else
  { "GLINE_TIME", "NONE", 0, "Expire Time for Glines" },
#endif /* GLINE_TIME */

#ifdef GLINEFILE
  { "GLINEFILE", GLINEFILE, 0, "Path to G-line File" },
#else
  { "GLINEFILE", "NONE", 0, "Path to G-line File" },
#endif /* GLINEFILE */

  { "HARD_FDLIMIT_", "", HARD_FDLIMIT_, "Maximum Number of File Descriptors Available" },

#ifdef HPATH
  { "HPATH", HPATH, 0, "Path to Operator Help File" },
#else
  { "HPATH", "NONE", 0, "Path to Operator Help File" },
#endif /* HPATH */

#ifdef SOMAXCONN
  { "HYBRID_SOMAXCONN", "", SOMAXCONN, "Maximum Queue Length of Pending Connections" },
#else
  { "HYBRID_SOMAXCONN", "", HYBRID_SOMAXCONN, "Maximum Queue Length of Pending Connections" },
#endif /* SOMAXCONN */

#ifdef IDLE_FROM_MSG
  { "IDLE_FROM_MSG", "ON", 0, "Reset idle time after a PRIVMSG" },
#else
  { "IDLE_FROM_MSG", "OFF", 0, "Reset idle time after a PRIVMSG" },
#endif /* IDLE_FROM_MSG */

  { "INIT_MAXCLIENTS", "", INIT_MAXCLIENTS, "Maximum Clients" },
  { "INITIAL_DBUFS", "", INITIAL_DBUFS, "Number of Dbufs to PreAllocate" },

  { "JOIN_LEAVE_COUNT_EXPIRE_TIME", "", JOIN_LEAVE_COUNT_EXPIRE_TIME, "Anti SpamBot Parameter" },

  { "KILLCHASETIMELIMIT", "", KILLCHASETIMELIMIT, "Nick Change Tracker for KILL" },

#ifdef KPATH
  { "KPATH", KPATH, 0, "Path to K-line File" },
#else
  { "KPATH", "NONE", 0, "Path to K-line File" },
#endif /* KPATH */

#ifdef LIMIT_UH
  { "LIMIT_UH", "ON", 0, "Make Y: lines limit username instead of hostname" },
#else
  { "LIMIT_UH", "OFF", 0, "Make Y: lines limit username instead of hostname" },
#endif /* LIMIT_UH */

  { "LITTLE_I_LINES", "ON", 0, "\"i\" lines prevent matching clients from channel opping" },

#ifdef LPATH
  { "LPATH", LPATH, 0, "Path to Log File" },
#else
  { "LPATH", "NONE", 0, "Path to Log File" },
#endif /* LPATH */

#ifdef LWALLOPS
  { "LWALLOPS", "ON", 0, "Local Wallops Support" },
#else
  { "LWALLOPS", "OFF", 0, "Local Wallops Support" },
#endif /* LWALLOPS */

  { "MAX_BUFFER", "", MAX_BUFFER, "Maximum Buffer Connections Allowed" },

  { "MAX_JOIN_LEAVE_COUNT", "", MAX_JOIN_LEAVE_COUNT, "Anti SpamBot Parameter" },

  { "MAXCHANNELSPERUSER", "", MAXCHANNELSPERUSER, "Maximum Channels per User" },
  { "MAXIMUM_LINKS", "", MAXIMUM_LINKS, "Maximum Links for Class 0" },

  { "MIN_JOIN_LEAVE_TIME", "", MIN_JOIN_LEAVE_TIME, "Anti SpamBot Parameter" },

#ifdef MPATH
  { "MPATH", MPATH, 0, "Path to MOTD File" },
#else
  { "MPATH", "NONE", 0, "Path to MOTD File" },
#endif /* MPATH */

  { "NICKNAMEHISTORYLENGTH", "", NICKNAMEHISTORYLENGTH, "Size of WHOWAS Array" },

#ifdef NO_DEFAULT_INVISIBLE
  { "NO_DEFAULT_INVISIBLE", "ON", 0, "Do not Give Clients +i Mode Upon Connection" },
#else
  { "NO_DEFAULT_INVISIBLE", "OFF", 0, "Do not Give Clients +i Mode Upon Connection" },
#endif /* NO_DEFAULT_INVISIBLE */

#ifdef NO_OPER_FLOOD
  { "NO_OPER_FLOOD", "ON", 0, "Disable Flood Control for Operators" },
#else
  { "NO_OPER_FLOOD", "OFF", 0, "Disable Flood Control for Operators" },
#endif /* NO_OPER_FLOOD */

#ifdef NOISY_HTM
  { "NOISY_HTM", "ON", 0, "Notify Operators of HTM (De)activation" },
#else
  { "NOISY_HTM", "OFF", 0, "Notify Operators of HTM (De)activation" },
#endif /* NOISY_HTM */

#ifdef OLD_Y_LIMIT
  { "OLD_Y_LIMIT", "ON", 0, "Use Old Y: line Limit Behavior" },
#else
  { "OLD_Y_LIMIT", "OFF", 0, "Use Old Y: line Limit Behavior" },
#endif /* OLD_Y_LIMIT */

#ifdef OPATH
  { "OPATH", OPATH, 0, "Path to Operator MOTD File" },
#else
  { "OPATH", "NONE", 0, "Path to Operator MOTD File" },
#endif /* OPATH */

  { "OPER_SPAM_COUNTDOWN", "", OPER_SPAM_COUNTDOWN, "Anti SpamBot Parameter" },

#ifdef PPATH
  { "PPATH", PPATH, 0, "Path to Pid File" },
#else
  { "PPATH", "NONE", 0, "Path to Pid File" },
#endif /* PPATH */

#ifdef REJECT_HOLD
  { "REJECT_HOLD", "ON", 0, "Do not Dump a K-lined Client immediately" },
#else
  { "REJECT_HOLD", "OFF", 0, "Do not Dump a K-lined Client immediately" },
#endif /* REJECT_HOLE */

#ifdef REJECT_HOLD_TIME
  { "REJECT_HOLD_TIME", "", REJECT_HOLD_TIME, "Amount of Time to Hold a K-lined Client" },
#else
  { "REJECT_HOLD_TIME", "OFF", 0, "Amount of Time to Hold a K-lined Client" },
#endif /* REJECT_HOLD_TIME */

#ifdef REPORT_DLINE_TO_USER
  { "REPORT_DLINE_TO_USER", "ON", 0, "Inform Clients They are D-lined" },
#else
  { "REPORT_DLINE_TO_USER", "OFF", 0, "Inform Clients They are D-lined" },
#endif /* REPORT_DLINE_TO_USER */

#ifdef SEND_FAKE_KILL_TO_CLIENT
  { "SEND_FAKE_KILL_TO_CLIENT", "ON", 0, "Make Client think they were KILLed" },
#else
  { "SEND_FAKE_KILL_TO_CLIENT", "OFF", 0, "Make Client think they were KILLed" },
#endif /* SEND_FAKE_KILL_TO_CLIENT */

#ifdef SENDQ_ALWAYS
  { "SENDQ_ALWAYS", "ON", 0, "Put All OutBound data into a SendQ" },
#else
  { "SENDQ_ALWAYS", "OFF", 0, "Put All OutBound data into a SendQ" },
#endif /* SENDQ_ALWAYS */

#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  { "SEPARATE_QUOTE_KLINES_BY_DATE", "ON", 0, "Read/Write K-lines According to Date" },
#else
  { "SEPARATE_QUOTE_KLINES_BY_DATE", "OFF", 0, "Read/Write K-lines According to Date" },
#endif /* SEPARATE_QUOTE_KLINES_BY_DATE */

#ifdef SHOW_INVISIBLE_LUSERS
  { "SHOW_INVISIBLE_LUSERS", "ON", 0, "Show Invisible Clients in LUSERS" },
#else
  { "SHOW_INVISIBLE_LUSERS", "OFF", 0, "Show Invisible Clients in LUSERS" },
#endif /* SHOW_INVISIBLE_LUSERS */

#ifdef SLAVE_SERVERS
  { "SLAVE_SERVERS", "ON", 0, "Send LOCOPS and K-lines to U: lined Servers" },
#else
  { "SLAVE_SERVERS", "OFF", 0, "Send LOCOPS and K-lines to U: lined Servers" },
#endif /* SLAVE_SERVERS */

#ifdef SPATH
  { "SPATH", SPATH, 0, "Path to Server Executable" },
#else
  { "SPATH", "NONE", 0, "Path to Server Executable" },
#endif /* SPATH */

#ifdef STATS_NOTICE
  { "STATS_NOTICE", "ON", 0, "Show Operators when a Client uses STATS" },
#else
  { "STATS_NOTICE", "OFF", 0, "Show Operators when a Client uses STATS" },
#endif /* STATS_NOTICE */

  { "TIMESEC", "", TIMESEC, "Time Interval to Wait Before Checking Pings" },

  { "TOPIC_INFO", "ON", 0, "Show Who Set a Topic and When" },

  { "TS_MAX_DELTA_DEFAULT", "", TS_MAX_DELTA_DEFAULT, "Maximum Allowed TS Delta from another Server" },
  { "TS_WARN_DELTA_DEFAULT", "", TS_WARN_DELTA_DEFAULT, "Maximum TS Delta before Sending Warning" },

#ifdef USE_RCS
  { "USE_RCS", "ON", 0, "Use \"ci\" to Keep RCS Control" },
#else
  { "USE_RCS", "OFF", 0, "Use \"ci\" to Keep RCS Control" },
#endif /* USE_RCS */

#ifdef USE_SYSLOG
  { "USE_SYSLOG", "ON", 0, "Log Errors to syslog file" },
#else
  { "USE_SYSLOG", "OFF", 0, "Log Errors to syslog file" },
#endif /* USE_SYSLOG */

#ifdef WHOIS_NOTICE
  { "WHOIS_NOTICE", "ON", 0, "Show Operators when they are WHOIS'd" },
#else
  { "WHOIS_NOTICE", "OFF", 0, "Show Operators when they are WHOIS'd" },
#endif /* WHOIS_NOTICE */

#ifdef WINTRHAWK
  { "WINTRHAWK", "ON", 0, "Enable Wintrhawk Styling" },
#else
  { "WINTRHAWK", "OFF", 0, "Enable Wintrhawk Styling" },
#endif /* WINTRHAWK */

  /*
   * since we don't want to include the world here, NULL probably
   * isn't defined by the time we read this, just use plain 0 instead
   * 0 is guaranteed by the language to be assignable to ALL built
   * in types with the correct results.
   */
  { 0, 0, 0, 0 }
};


#endif /* DEFINE_M_INFO_DATA */
#endif /* INCLUDED_m_info_h */

