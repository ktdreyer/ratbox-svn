/*
 *  ircd-ratbox: A slightly useful ircd.
 *  s_user.c: User related functions.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2004 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "tools.h"
#include "s_user.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "sprintf_irc.h"
#include "ircd.h"
#include "listener.h"
#include "msg.h"
#include "numeric.h"
#include "commio.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_log.h"
#include "s_serv.h"
#include "scache.h"
#include "send.h"
#include "supported.h"
#include "whowas.h"
#include "memory.h"
#include "packet.h"
#include "reject.h"
#include "cache.h"
#include "hook.h"

extern char *crypt();

/* table of ascii char letters to corresponding bitmask */

struct flag_item
{
	int mode;
	char letter;
};

/* *INDENT-OFF* */
static struct flag_item user_modes[] = {
	{UMODE_ADMIN,		'a'},
	{UMODE_BOTS,		'b'},
	{UMODE_CCONN,		'c'},
	{UMODE_DEBUG,		'd'},
	{UMODE_FULL,		'f'},
	{UMODE_CALLERID,	'g'},
	{UMODE_INVISIBLE,	'i'},
	{UMODE_SKILL,		'k'},
	{UMODE_LOCOPS,		'l'},
	{UMODE_NCHANGE,		'n'},
	{UMODE_OPER,		'o'},
	{UMODE_REJ,		'r'},
	{UMODE_SERVNOTICE,	's'},
	{UMODE_UNAUTH,		'u'},
	{UMODE_WALLOP,		'w'},
	{UMODE_EXTERNAL,	'x'},
	{UMODE_SPY,		'y'},
	{UMODE_OPERWALL,	'z'},
	{UMODE_OPERSPY,		'Z'},
	{0, 0}
};

/* memory is cheap. map 0-255 to equivalent mode */
int user_modes_from_c_to_bitmask[] = {
	/* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x0F */
	/* 0x10 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x1F */
	/* 0x20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x2F */
	/* 0x30 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x3F */
	0,			/* @ */
	0,			/* A */
	0,			/* B */
	0,			/* C */
	0,			/* D */
	0,			/* E */
	0,			/* F */
	0,			/* G */
	0,			/* H */
	0,			/* I */
	0,			/* J */
	0,			/* K */
	0,			/* L */
	0,			/* M */
	0,			/* N */
	0,			/* O */
	0,			/* P */
	0,			/* Q */
	0,			/* R */
	0,			/* S */
	0,			/* T */
	0,			/* U */
	0,			/* V */
	0,			/* W */
	0,			/* X */
	0,			/* Y */
	UMODE_OPERSPY,		/* Z */
	/* 0x5B */ 0, 0, 0, 0, 0, 0, /* 0x60 */
	UMODE_ADMIN,		/* a */
	UMODE_BOTS,		/* b */
	UMODE_CCONN,		/* c */
	UMODE_DEBUG,		/* d */
	0,			/* e */
	UMODE_FULL,		/* f */
	UMODE_CALLERID,		/* g */
	0,			/* h */
	UMODE_INVISIBLE,	/* i */
	0,			/* j */
	UMODE_SKILL,		/* k */
	UMODE_LOCOPS,		/* l */
	0,			/* m */
	UMODE_NCHANGE,		/* n */
	UMODE_OPER,		/* o */
	0,			/* p */
	0,			/* q */
	UMODE_REJ,		/* r */
	UMODE_SERVNOTICE,	/* s */
	0,			/* t */
	UMODE_UNAUTH,		/* u */
	0,			/* v */
	UMODE_WALLOP,		/* w */
	UMODE_EXTERNAL,		/* x */
	UMODE_SPY,		/* y */
	UMODE_OPERWALL,		/* z */
	/* 0x7B */ 0, 0, 0, 0, 0, /* 0x7F */
	/* 0x80 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x9F */
	/* 0x90 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x9F */
	/* 0xA0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xAF */
	/* 0xB0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xBF */
	/* 0xC0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xCF */
	/* 0xD0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xDF */
	/* 0xE0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xEF */
	/* 0xF0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* 0xFF */
};
/* *INDENT-ON* */

/*
 * show_lusers -
 *
 * inputs	- pointer to client
 * output	-
 * side effects	- display to client user counts etc.
 */
int
show_lusers(struct Client *source_p)
{
	sendto_one_numeric(source_p, RPL_LUSERCLIENT, form_str(RPL_LUSERCLIENT),
			   (Count.total - Count.invisi),
			   Count.invisi, dlink_list_length(&global_serv_list));

	if(Count.oper > 0)
		sendto_one_numeric(source_p, RPL_LUSEROP, 
				   form_str(RPL_LUSEROP), Count.oper);

	if(dlink_list_length(&unknown_list) > 0)
		sendto_one_numeric(source_p, RPL_LUSERUNKNOWN, 
				   form_str(RPL_LUSERUNKNOWN),
				   dlink_list_length(&unknown_list));

	if(dlink_list_length(&global_channel_list) > 0)
		sendto_one_numeric(source_p, RPL_LUSERCHANNELS, 
				   form_str(RPL_LUSERCHANNELS),
				   dlink_list_length(&global_channel_list));

	sendto_one_numeric(source_p, RPL_LUSERME, form_str(RPL_LUSERME),
			   dlink_list_length(&lclient_list),
			   dlink_list_length(&serv_list));

	sendto_one_numeric(source_p, RPL_LOCALUSERS, 
			   form_str(RPL_LOCALUSERS),
			   dlink_list_length(&lclient_list),
			   Count.max_loc);

	sendto_one_numeric(source_p, RPL_GLOBALUSERS, form_str(RPL_GLOBALUSERS),
			   Count.total, Count.max_tot);

	sendto_one_numeric(source_p, RPL_STATSCONN,
			   form_str(RPL_STATSCONN),
			   MaxConnectionCount, MaxClientCount, 
			   Count.totalrestartcount);

	if(dlink_list_length(&lclient_list) > (unsigned long)MaxClientCount)
		MaxClientCount = dlink_list_length(&lclient_list);

	if((dlink_list_length(&lclient_list) + dlink_list_length(&serv_list)) >
	   (unsigned long)MaxConnectionCount)
		MaxConnectionCount = dlink_list_length(&lclient_list) + 
					dlink_list_length(&serv_list);

	return 0;
}

/*
 * show_isupport
 *
 * inputs	- pointer to client
 * output	- 
 * side effects	- display to client what we support (for them)
 */
void
show_isupport(struct Client *source_p)
{
	char isupportbuffer[512];

	ircsprintf(isupportbuffer, FEATURES, FEATURESVALUES);
	sendto_one_numeric(source_p, RPL_ISUPPORT, form_str(RPL_ISUPPORT), isupportbuffer);

	ircsprintf(isupportbuffer, FEATURES2, FEATURES2VALUES);
	sendto_one_numeric(source_p, RPL_ISUPPORT, form_str(RPL_ISUPPORT), isupportbuffer);

	return;
}



/* 
 * valid_hostname - check hostname for validity
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 *
 * NOTE: this doesn't allow a hostname to begin with a dot and
 * will not allow more dots than chars.
 */
int
valid_hostname(const char *hostname)
{
	const char *p = hostname;
	int found_sep = 0;

	s_assert(NULL != p);

	if(hostname == NULL)
		return NO;

	if('.' == *p || ':' == *p)
		return NO;

	while (*p)
	{
		if(!IsHostChar(*p))
			return NO;
                if(*p == '.' || *p == ':')
  			found_sep++;
		p++;
	}

	if(found_sep == 0)
		return(NO);

	return (YES);
}

/* 
 * valid_username - check username for validity
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 * 
 * Absolutely always reject any '*' '!' '?' '@' in an user name
 * reject any odd control characters names.
 * Allow '.' in username to allow for "first.last"
 * style of username
 */
int
valid_username(const char *username)
{
	int dots = 0;
	const char *p = username;

	s_assert(NULL != p);

	if(username == NULL)
		return NO;

	if('~' == *p)
		++p;

	/* reject usernames that don't start with an alphanum
	 * i.e. reject jokers who have '-@somehost' or '.@somehost'
	 * or "-hi-@somehost", "h-----@somehost" would still be accepted.
	 */
	if(!IsAlNum(*p))
		return NO;

	while (*++p)
	{
		if((*p == '.') && ConfigFileEntry.dots_in_ident)
		{
			dots++;
			if(dots > ConfigFileEntry.dots_in_ident)
				return NO;
			if(!IsUserChar(p[1]))
				return NO;
		}
		else if(!IsUserChar(*p))
			return NO;
	}
	return YES;
}

/*
 * user_mode - set get current users mode
 *
 * m_umode() added 15/10/91 By Darren Reed.
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 */
int
user_mode(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	int flag;
	int i;
	const char **p;
	char *m;
	const char *pm;
	struct Client *target_p;
	int what, setflags;
	int badflag = NO;	/* Only send one bad flag notice */
	char buf[BUFSIZE];

	what = MODE_ADD;

	if(parc < 2)
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS), me.name, source_p->name, "MODE");
		return 0;
	}

	if((target_p = find_person(parv[1])) == NULL)
	{
		if(MyConnect(source_p))
			sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL,
					   form_str(ERR_NOSUCHCHANNEL), parv[1]);
		return 0;
	}

	/* Dont know why these were commented out..
	 * put them back using new sendto() funcs
	 */

	if(IsServer(source_p))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "*** Mode for User %s from %s", parv[1], source_p->name);
		return 0;
	}

	if(source_p != target_p || target_p->from != source_p->from)
	{
		sendto_one(source_p, form_str(ERR_USERSDONTMATCH), me.name, source_p->name);
		return 0;
	}


	if(parc < 3)
	{
		m = buf;
		*m++ = '+';

		for (i = 0; user_modes[i].letter && (m - buf < BUFSIZE - 4); i++)
			if(source_p->umodes & user_modes[i].mode)
				*m++ = user_modes[i].letter;
		*m = '\0';
		sendto_one(source_p, form_str(RPL_UMODEIS), me.name, source_p->name, buf);
		return 0;
	}

	/* find flags already set for user */
	setflags = source_p->umodes;

	/*
	 * parse mode change string(s)
	 */
	for (p = &parv[2]; p && *p; p++)
		for (pm = *p; *pm; pm++)
			switch (*pm)
			{
			case '+':
				what = MODE_ADD;
				break;
			case '-':
				what = MODE_DEL;
				break;

			case 'o':
				if(what == MODE_ADD)
				{
					if(IsServer(client_p) && !IsOper(source_p))
					{
						++Count.oper;
						SetOper(source_p);
					}
				}
				else
				{
					/* Only decrement the oper counts if an oper to begin with
					 * found by Pat Szuta, Perly , perly@xnet.com 
					 */

					if(!IsOper(source_p))
						break;

					ClearOper(source_p);
					source_p->umodes &= ~ConfigFileEntry.oper_only_umodes;

					Count.oper--;

					if(MyConnect(source_p))
					{
						source_p->flags2 &= ~OPER_FLAGS;

						MyFree(source_p->localClient->opername);
						source_p->localClient->opername = NULL;

						dlinkFindDestroy(&oper_list, source_p);
					}
				}
				break;

				/* we may not get these,
				 * but they shouldnt be in default
				 */
			case ' ':
			case '\n':
			case '\r':
			case '\t':
				break;

			default:
				if((flag = user_modes_from_c_to_bitmask[(unsigned char) *pm]))
				{
					if(MyConnect(source_p)
					   && !IsOper(source_p)
					   && (ConfigFileEntry.oper_only_umodes & flag))
					{
						badflag = YES;
					}
					else
					{
						if(what == MODE_ADD)
							source_p->umodes |= flag;
						else
							source_p->umodes &= ~flag;
					}
				}
				else
				{
					if(MyConnect(source_p))
						badflag = YES;
				}
				break;
			}

	if(badflag)
		sendto_one(source_p, form_str(ERR_UMODEUNKNOWNFLAG), me.name, source_p->name);

	if((source_p->umodes & UMODE_NCHANGE) && !IsOperN(source_p))
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :*** You need oper and N flag for +n", me.name, parv[0]);
		source_p->umodes &= ~UMODE_NCHANGE;	/* only tcm's really need this */
	}

	if(MyConnect(source_p) && (source_p->umodes & UMODE_ADMIN) &&
	   (!IsOperAdmin(source_p) || IsOperHiddenAdmin(source_p)))
	{
		sendto_one(source_p,
			   ":%s NOTICE %s :*** You need oper and A flag for +a", me.name, parv[0]);
		source_p->umodes &= ~UMODE_ADMIN;
	}


	if(!(setflags & UMODE_INVISIBLE) && IsInvisible(source_p))
		++Count.invisi;
	if((setflags & UMODE_INVISIBLE) && !IsInvisible(source_p))
		--Count.invisi;
	/*
	 * compare new flags with old flags and send string which
	 * will cause servers to update correctly.
	 */
	send_umode_out(client_p, source_p, setflags);

	return (0);
}

/*
 * send the MODE string for user (user) to connection client_p
 * -avalon
 */
void
send_umode(struct Client *client_p, struct Client *source_p, int old, int sendmask, char *umode_buf)
{
	int i;
	int flag;
	char *m;
	int what = 0;

	/*
	 * build a string in umode_buf to represent the change in the user's
	 * mode between the new (source_p->flag) and 'old'.
	 */
	m = umode_buf;
	*m = '\0';

	for (i = 0; user_modes[i].letter; i++)
	{
		flag = user_modes[i].mode;

		if(MyClient(source_p) && !(flag & sendmask))
			continue;
		if((flag & old) && !(source_p->umodes & flag))
		{
			if(what == MODE_DEL)
				*m++ = user_modes[i].letter;
			else
			{
				what = MODE_DEL;
				*m++ = '-';
				*m++ = user_modes[i].letter;
			}
		}
		else if(!(flag & old) && (source_p->umodes & flag))
		{
			if(what == MODE_ADD)
				*m++ = user_modes[i].letter;
			else
			{
				what = MODE_ADD;
				*m++ = '+';
				*m++ = user_modes[i].letter;
			}
		}
	}
	*m = '\0';
	if(*umode_buf && client_p)
		sendto_one(client_p, ":%s MODE %s :%s", source_p->name, source_p->name, umode_buf);
}

/*
 * send_umode_out
 *
 * inputs	-
 * output	- NONE
 * side effects - 
 */
void
send_umode_out(struct Client *client_p, struct Client *source_p, int old)
{
	struct Client *target_p;
	char buf[BUFSIZE];
	dlink_node *ptr;

	send_umode(NULL, source_p, old, SEND_UMODES, buf);

	DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = ptr->data;

		if((target_p != client_p) && (target_p != source_p) && (*buf))
		{
			sendto_one(target_p, ":%s MODE %s :%s",
				   get_id(source_p, target_p), 
				   get_id(source_p, target_p), buf);
		}
	}

	if(client_p && MyClient(client_p))
		send_umode(client_p, source_p, old, ALL_UMODES, buf);
}


