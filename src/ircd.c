/*
 *  ircd-ratbox: A slightly useful ircd.
 *  ircd.c: Starts up and runs the ircd.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
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
#include "setup.h"
#include "config.h"

#include "tools.h"
#include "ircd.h"
#include "channel.h"
#include "channel_mode.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "event.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd_signal.h"
#include "sprintf_irc.h"
#include "s_gline.h"
#include "motd.h"
#include "ircd_handler.h"
#include "md5.h"
#include "msg.h"		/* msgtab */
#include "hostmask.h"
#include "numeric.h"
#include "parse.h"
#include "res.h"
#include "restart.h"
#include "s_auth.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_debug.h"
#include "s_log.h"
#include "s_serv.h"		/* try_connections */
#include "s_stats.h"
#include "scache.h"
#include "send.h"
#include "whowas.h"
#include "modules.h"
#include "memory.h"
#include "hook.h"
#include "ircd_getopt.h"
#include "balloc.h"
#include "newconf.h"
#include "patricia.h"
#include "s_newconf.h"
#include "reject.h"
#include "s_conf.h"

/*
 * Try and find the correct name to use with getrlimit() for setting the max.
 * number of files allowed to be open by this process.
 */

/* /quote set variables */
struct SetOptions GlobalSetOptions;

/* configuration set from ircd.conf */
struct config_file_entry ConfigFileEntry;
/* server info set from ircd.conf */
struct server_info ServerInfo;
/* admin info set from ircd.conf */
struct admin_info AdminInfo;

struct Counter Count;
struct ServerState_t server_state;

struct timeval SystemTime;
int ServerRunning;		/* GLOBAL - server execution state */
struct Client me;		/* That's me */
struct LocalUser meLocalUser;	/* That's also part of me */

dlink_list global_client_list;

struct JupedChannel *JupedChannelList = 0;

/* unknown/client pointer lists */
dlink_list unknown_list;	/* unknown clients ON this server only */
dlink_list lclient_list;	/* local clients only ON this server */
dlink_list serv_list;		/* local servers to this server ONLY */
dlink_list global_serv_list;	/* global servers on the network */
dlink_list oper_list;		/* our opers, duplicated in lclient_list */

static unsigned long initialVMTop = 0;	/* top of virtual memory at init */
const char *logFileName = LPATH;
const char *pidFileName = PPATH;

char **myargv;
int dorehash = 0;
int doremotd = 0;
int debuglevel = -1;		/* Server debug level */
const char *debugmode = "";	/*  -"-    -"-   -"-  */
time_t nextconnect = 1;		/* time for next try_connections call */
int kline_queued = 0;

/* Set to zero because it should be initialized later using
 * initialize_server_capabs
 */
int default_server_capabs = CAP_MASK;

int splitmode;
int splitchecking;
int split_users;
int split_servers;

/*
 * get_vm_top - get the operating systems notion of the resident set size
 */
static unsigned long
get_vm_top(void)
{
	/*
	 * NOTE: sbrk is not part of the ANSI C library or the POSIX.1 standard
	 * however it seems that everyone defines it. Calling sbrk with a 0
	 * argument will return a pointer to the top of the process virtual
	 * memory without changing the process size, so this call should be
	 * reasonably safe (sbrk returns the new value for the top of memory).
	 * This code relies on the notion that the address returned will be an 
	 * offset from 0 (NULL), so the result of sbrk is cast to a size_t and 
	 * returned. We really shouldn't be using it here but...
	 */
#ifndef __VMS
	void *vptr = sbrk(0);
	return (unsigned long) vptr;
#else
	return 0;
#endif
}

/*
 * get_maxrss - get the operating systems notion of the resident set size
 */
unsigned long
get_maxrss(void)
{
	return get_vm_top() - initialVMTop;
}

/*
 * print_startup - print startup information
 */
static void
print_startup(int pid)
{
	printf("ircd: version %s\n", ircd_version);
	printf("ircd: pid %d\n", pid);
	printf("ircd: running in %s mode from %s\n",
	       !server_state.foreground ? "background" : "foreground", ConfigFileEntry.dpath);
}

/*
 * init_sys
 *
 * inputs	- boot_daemon flag
 * output	- none
 * side effects	- if boot_daemon flag is not set, don't daemonize
 */
static void
init_sys(void)
{
#if defined(RLIMIT_FD_MAX) && !defined(__VMS) && defined(HAVE_SYS_RLIMIT_H)
	struct rlimit limit;

	if(!getrlimit(RLIMIT_FD_MAX, &limit))
	{

		if(limit.rlim_max < MAXCONNECTIONS)
		{
			fprintf(stderr, "ircd fd table too big\n");
			fprintf(stderr, "Hard Limit: %ld IRC max: %d\n",
				(long) limit.rlim_max, MAXCONNECTIONS);
			fprintf(stderr, "Fix MAXCONNECTIONS\n");
			exit(-1);
		}

		limit.rlim_cur = limit.rlim_max;	/* make soft limit the max */
		if(setrlimit(RLIMIT_FD_MAX, &limit) == -1)
		{
			fprintf(stderr, "error setting max fd's to %ld\n", (long) limit.rlim_cur);
			exit(EXIT_FAILURE);
		}
	}
#endif /* RLIMIT_FD_MAX */
}

static int
make_daemon(void)
{
#ifndef __VMS
	int pid;

	if((pid = fork()) < 0)
	{
		perror("fork");
		exit(EXIT_FAILURE);
	}
	else if(pid > 0)
	{
		print_startup(pid);
		exit(EXIT_SUCCESS);
	}

	setsid();
	/*  fclose(stdin);
	   fclose(stdout);
	   fclose(stderr); */
#else
	/* if we get here, assume we've been detached.
	   better set a process name. */
	$DESCRIPTOR(myname, "IRCD-RATBOX");
	SYS$SETPRN(&myname);
#endif
	return 0;
}

static int printVersion = 0;

struct lgetopt myopts[] = {
	{"dlinefile", &ConfigFileEntry.dlinefile,
	 STRING, "File to use for dlines.conf"},
	{"configfile", &ConfigFileEntry.configfile,
	 STRING, "File to use for ircd.conf"},
	{"klinefile", &ConfigFileEntry.klinefile,
	 STRING, "File to use for klines.conf"},
	{"xlinefile", &ConfigFileEntry.xlinefile,
	 STRING, "File to use for xlines.conf"},
	{"resvfile", &ConfigFileEntry.resvfile,
	 STRING, "File to use for resv.conf"},
	{"logfile", &logFileName,
	 STRING, "File to use for ircd.log"},
	{"pidfile", &pidFileName,
	 STRING, "File to use for process ID"},
	{"foreground", &server_state.foreground,
	 YESNO, "Run in foreground (don't detach)"},
	{"version", &printVersion,
	 YESNO, "Print version and exit"},
	{"help", NULL, USAGE, "Print this text"},
	{NULL, NULL, STRING, NULL},
};

void
set_time(void)
{
	static char to_send[200];
	struct timeval newtime;
	newtime.tv_sec = 0;
	newtime.tv_usec = 0;
	if(gettimeofday(&newtime, NULL) == -1)
	{
		ilog(L_ERROR, "Clock Failure (%d)", errno);
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Clock Failure (%d), TS can be corrupted", errno);

		restart("Clock Failure");
	}
	if(newtime.tv_sec < CurrentTime)
	{
		ircsprintf(to_send,
			   "System clock is running backwards - (%lu < %lu)",
			   (unsigned long) newtime.tv_sec, (unsigned long) CurrentTime);
		report_error(L_ALL, to_send, me.name, 0);
		set_back_events(CurrentTime - newtime.tv_sec);
	}
	SystemTime.tv_sec = newtime.tv_sec;
	SystemTime.tv_usec = newtime.tv_usec;
}

static time_t lasttime = 0;
static long last_recvK = 0;
static long last_sendK = 0;
static int htm_mode = 0;
static float in_curr_bandwidth = 0.0;
static float out_curr_bandwidth = 0.0;

void
get_current_bandwidth(struct Client *source_p, struct Client *target_p)
{
	if(ConfigFileEntry.htm_messages == 0)
	{
		sendto_one(source_p, ":%s NOTICE %s :HTM mode is disabled",
			   me.name, source_p->name);
		return;
	}
	sendto_one(source_p,
		   ":%s NOTICE %s :Current traffic: - In (%.1fk/s) Out (%.1fk/s)",
		   me.name, source_p->name, in_curr_bandwidth, out_curr_bandwidth);

}

static void
check_htm(void)
{
	long htm_trigger = (long) ConfigFileEntry.htm_trigger;
	long htm_check = (long) ConfigFileEntry.htm_interval;
	long htm_combined = htm_trigger * htm_check;

	if(CurrentTime - lasttime < ConfigFileEntry.htm_interval)
		return;

	lasttime = CurrentTime;
	in_curr_bandwidth = (float) me.localClient->receiveK - (float) (last_recvK / htm_check);
	out_curr_bandwidth = (float) me.localClient->sendK - (float) (last_sendK / htm_check);



	if((long) me.localClient->receiveK - htm_combined > last_recvK ||
	   (long) me.localClient->sendK - htm_combined > last_sendK)

	{
		if(!htm_mode)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Entering high-traffic mode - In (%.1fk/s) Out(%.1fk/s)",
					     in_curr_bandwidth, out_curr_bandwidth);
		}
		else
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Still in high-traffic mode - In(%.1fk/s) Out(%.1fk/s)",
					     in_curr_bandwidth, out_curr_bandwidth);
		}
		htm_mode = 1;
	}
	else
	{
		if(htm_mode)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL, "Exiting high traffic mode");
			htm_mode = 0;
		}
	}
	last_recvK = (long) me.localClient->receiveK;
	last_sendK = (long) me.localClient->sendK;
}

static void
io_loop(void)
{
	unsigned long empty_cycles = 0, st = 0;
	time_t delay;
#ifndef __vms
	while (ServerRunning)

	{
		/* Run pending events, then get the number of seconds to the next
		 * event
		 */

		delay = eventNextTime();
		if(delay <= CurrentTime)
			eventRun();


		comm_select(250);
		if(ConfigFileEntry.htm_messages)
			check_htm();

		/*
		 * Check to see whether we have to rehash the configuration ..
		 */
		if(dorehash)
		{
			rehash(1);
			dorehash = 0;
		}
		if(doremotd)
		{
			ReadMessageFile(&ConfigFileEntry.motd);
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Got signal SIGUSR1, reloading ircd motd file");
			doremotd = 0;
		}
	}
#else
	comm_select(0);
#endif
}

/*
 * initalialize_global_set_options
 *
 * inputs       - none
 * output       - none
 * side effects - This sets all global set options needed 
 */
static void
initialize_global_set_options(void)
{
	memset(&GlobalSetOptions, 0, sizeof(GlobalSetOptions));
	/* memset( &ConfigFileEntry, 0, sizeof(ConfigFileEntry)); */

	GlobalSetOptions.maxclients = MAX_CLIENTS;
	GlobalSetOptions.autoconn = 1;

	GlobalSetOptions.spam_time = MIN_JOIN_LEAVE_TIME;
	GlobalSetOptions.spam_num = MAX_JOIN_LEAVE_COUNT;

	if(ConfigFileEntry.default_floodcount)
		GlobalSetOptions.floodcount = ConfigFileEntry.default_floodcount;
	else
		GlobalSetOptions.floodcount = 10;

	split_servers = ConfigChannel.default_split_server_count;
	split_users = ConfigChannel.default_split_user_count;

	if(split_users && split_servers
	   && (ConfigChannel.no_create_on_split || ConfigChannel.no_join_on_split))
	{
		splitmode = 1;
		splitchecking = 1;
	}

	GlobalSetOptions.ident_timeout = IDENT_TIMEOUT;

	DupString(GlobalSetOptions.operstring, ConfigFileEntry.default_operstring);
	/* memset( &ConfigChannel, 0, sizeof(ConfigChannel)); */

	/* End of global set options */

}

/*
 * initialize_message_files
 *
 * inputs       - none
 * output       - none
 * side effects - Set up all message files needed, motd etc.
 */
static void
initialize_message_files(void)
{
	InitMessageFile(USER_MOTD, MPATH, &ConfigFileEntry.motd);
	InitMessageFile(OPER_MOTD, OPATH, &ConfigFileEntry.opermotd);
	InitMessageFile(USER_LINKS, LIPATH, &ConfigFileEntry.linksfile);

	ReadMessageFile(&ConfigFileEntry.motd);
	ReadMessageFile(&ConfigFileEntry.opermotd);
	ReadMessageFile(&ConfigFileEntry.linksfile);
}

/*
 * initialize_server_capabs
 *
 * inputs       - none
 * output       - none
 */
static void
initialize_server_capabs(void)
{
	default_server_capabs &= ~CAP_ZIP;
}


/*
 * write_pidfile
 *
 * inputs       - filename+path of pid file
 * output       - none
 * side effects - write the pid of the ircd to filename
 */
static void
write_pidfile(const char *filename)
{
	FBFILE *fb;
	char buff[32];
	if((fb = fbopen(filename, "w")))
	{
		unsigned int pid = (unsigned int) getpid();

		ircsprintf(buff, "%u\n", pid);
		if((fbputs(buff, fb) == -1))
		{
			ilog(L_ERROR, "Error writing %u to pid file %s (%s)",
			     pid, filename, strerror(errno));
		}
		fbclose(fb);
		return;
	}
	else
	{
		ilog(L_ERROR, "Error opening pid file %s", filename);
	}
}

/*
 * check_pidfile
 *
 * inputs       - filename+path of pid file
 * output       - none
 * side effects - reads pid from pidfile and checks if ircd is in process
 *                list. if it is, gracefully exits
 * -kre
 */
static void
check_pidfile(const char *filename)
{
	FBFILE *fb;
	char buff[32];
	pid_t pidfromfile;

	/* Don't do logging here, since we don't have log() initialised */
	if((fb = fbopen(filename, "r")))
	{
		if(fbgets(buff, 20, fb) == NULL)
		{
			/*
			   log(L_ERROR, "Error reading from pid file %s (%s)", filename,
			   strerror(errno));
			 */
		}
		else
		{
			pidfromfile = atoi(buff);
			if(!kill(pidfromfile, 0))
			{
				/* log(L_ERROR, "Server is already running"); */
				printf("ircd: daemon is already running\n");
				exit(-1);
			}
		}
		fbclose(fb);
	}
	else if(errno != ENOENT)
	{
		/* log(L_ERROR, "Error opening pid file %s", filename); */
	}
}

/*
 * setup_corefile
 *
 * inputs       - nothing
 * output       - nothing
 * side effects - setups corefile to system limits.
 * -kre
 */
static void
setup_corefile(void)
{
#if !defined(__VMS) && defined(HAVE_SYS_RESOURCE_H)
	struct rlimit rlim;	/* resource limits */

	/* Set corefilesize to maximum */
	if(!getrlimit(RLIMIT_CORE, &rlim))
	{
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}
#endif
}

int
main(int argc, char *argv[])
{
	/* Check to see if the user is running us as root, which is a nono */
	if(geteuid() == 0)
	{
		fprintf(stderr, "Don't run ircd as root!!!\n");
		return -1;
	}

	/*
	 * save server boot time right away, so getrusage works correctly
	 */
	set_time();
	/*
	 * Setup corefile size immediately after boot -kre
	 */
	setup_corefile();

	/*
	 * set initialVMTop before we allocate any memory
	 */
	initialVMTop = get_vm_top();

	ServerRunning = 0;
	/* It ain't random, but it ought to be a little harder to guess */
	srand(SystemTime.tv_sec ^ (SystemTime.tv_usec | (getpid() << 20)));
	memset(&me, 0, sizeof(me));
	memset(&meLocalUser, 0, sizeof(meLocalUser));
	me.localClient = &meLocalUser;

	/* Make sure all lists are zeroed */
	memset(&unknown_list, 0, sizeof(unknown_list));
	memset(&lclient_list, 0, sizeof(lclient_list));
	memset(&serv_list, 0, sizeof(serv_list));
	memset(&global_serv_list, 0, sizeof(global_serv_list));
	memset(&oper_list, 0, sizeof(oper_list));

	dlinkAddTail(&me, &me.node, &global_client_list);

	memset((void *) &Count, 0, sizeof(Count));
	memset((void *) &server_state, 0, sizeof(server_state));

	Count.server = 1;	/* us */
	memset((void *) &ServerInfo, 0, sizeof(ServerInfo));
	memset((void *) &AdminInfo, 0, sizeof(AdminInfo));

	/* Initialise the channel capability usage counts... */
	init_chcap_usage_counts();

	ConfigFileEntry.dpath = DPATH;
	ConfigFileEntry.configfile = CPATH;	/* Server configuration file */
	ConfigFileEntry.klinefile = KPATH;	/* Server kline file */
	ConfigFileEntry.dlinefile = DLPATH;	/* dline file */
	ConfigFileEntry.glinefile = GPATH;	/* gline log file */
	ConfigFileEntry.xlinefile = XPATH;
	ConfigFileEntry.resvfile = RESVPATH;
	ConfigFileEntry.connect_timeout = 30;	/* Default to 30 */
	myargv = argv;
	umask(077);		/* better safe than sorry --SRB */

	parseargs(&argc, &argv, myopts);

	if(printVersion)
	{
		printf("ircd: version %s\n", ircd_version);
		exit(EXIT_SUCCESS);
	}

	if(chdir(ConfigFileEntry.dpath))
	{
		perror("chdir");
		exit(EXIT_FAILURE);
	}

#ifdef __CYGWIN__
	server_state.foreground = 1;
#endif

	if(!server_state.foreground)
	{
		make_daemon();
	}
	else
	{
		print_startup(getpid());
	}

	setup_signals();
	/* We need this to initialise the fd array before anything else */
	fdlist_init();
	init_netio();		/* This needs to be setup early ! -- adrian */
	/* Check if there is pidfile and daemon already running */
#ifndef __vms
	check_pidfile(pidFileName);
#endif
	/* Init the event subsystem */
	eventInit();
	init_sys();

	if(!server_state.foreground)
	{
		close_all_connections();
	}
	init_log(logFileName);
	initBlockHeap();
	init_dlink_nodes();
	init_patricia();
	newconf_init();
	initialize_message_files();
	linebuf_init();		/* set up some linebuf stuff to control paging */
	init_hash();
	id_init();
	clear_scache_hash_table();	/* server cache name table */
	init_host_hash();
	clear_hash_parse();
	init_client();
	init_conf();
	initUser();
	init_channels();
	initclass();
	initwhowas();
	init_stats();
	init_hooks();
	init_reject();
	load_all_modules(1);
#ifndef STATIC_MODULES
	load_core_modules(1);
#endif
	initServerMask();
	init_auth();		/* Initialise the auth code */
	init_resolver();	/* Needs to be setup before the io loop */
	read_conf_files(YES);	/* cold start init conf files */
#ifndef STATIC_MODULES

	mod_add_path(IRCD_PREFIX "/modules");
	mod_add_path(IRCD_PREFIX "/modules/autoload");
#endif

	initialize_server_capabs();	/* Set up default_server_capabs */
	initialize_global_set_options();

	if(ServerInfo.name == NULL)
	{
		fprintf(stderr, "ERROR: No server name specified in serverinfo block.\n");
		ilog(L_CRIT, "No server name specified in serverinfo block.");
		exit(EXIT_FAILURE);
	}
	strlcpy(me.name, ServerInfo.name, sizeof(me.name));

	/* serverinfo{} description must exist.  If not, error out. */
	if(ServerInfo.description == NULL)
	{
		fprintf(stderr, "ERROR: No server description specified in serverinfo block.\n");
		ilog(L_CRIT, "ERROR: No server description specified in serverinfo block.");
		exit(EXIT_FAILURE);
	}
	strlcpy(me.info, ServerInfo.description, sizeof(me.info));

	me.from = &me;
	me.servptr = &me;
	SetMe(&me);
	make_server(&me);
	me.serv->up = me.name;
	me.lasttime = me.since = me.firsttime = CurrentTime;
	add_to_client_hash(me.name, &me);

	add_server_to_list(&me);	/* add ourselves to global_serv_list */

	check_class();
	write_pidfile(pidFileName);

	ilog(L_NOTICE, "Server Ready");

	eventAddIsh("cleanup_channels", cleanup_channels, NULL, CLEANUP_CHANNELS_TIME);

	eventAddIsh("cleanup_glines", cleanup_glines, NULL, CLEANUP_GLINES_TIME);

	eventAddIsh("cleanup_temps_min", cleanup_temps_min, NULL, 60);
	eventAddIsh("cleanup_temps_hour", cleanup_temps_hour, NULL, 3600);
	eventAddIsh("cleanup_temps_day", cleanup_temps_day, NULL, 86400);
	eventAddIsh("cleanup_temps_week", cleanup_temps_week, NULL, 604800);

	/* We want try_connections to be called as soon as possible now! -- adrian */
	/* No, 'cause after a restart it would cause all sorts of nick collides */
	eventAddIsh("try_connections", try_connections, NULL, STARTUP_CONNECTIONS_TIME);

	eventAddIsh("collect_zipstats", collect_zipstats, NULL, ZIPSTATS_TIME);

	/* Setup the timeout check. I'll shift it later :)  -- adrian */
	eventAddIsh("comm_checktimeouts", comm_checktimeouts, NULL, 1);

	if(ConfigServerHide.links_delay > 0)
		eventAddIsh("write_links_file", write_links_file, NULL,
			    ConfigServerHide.links_delay);
	else
		ConfigServerHide.links_disabled = 1;

	if(splitmode)
		eventAddIsh("check_splitmode", check_splitmode, NULL, 60);

	ServerRunning = 1;
	io_loop();
	return 0;
}
