/*
 * fdlist.h
 *
 * $Id$
 */
#ifndef INCLUDED_fdlist_h
#define INCLUDED_fdlist_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>         /* time_t */
#define INCLUDED_sys_types_h
#endif

#define FD_DESC_SZ 32

/*
 * priority values used in fdlist code
 */
#define FDL_SERVER   0x01
#define FDL_BUSY     0x02
#define FDL_OPER     0x04
#define FDL_DEFAULT  0x08 
#define FDL_ALL      0xFF

/* FD type values */
enum {
    FD_NONE,
    FD_LOG,
    FD_FILE,
    FD_FILECLOSE,
    FD_SOCKET,
    FD_PIPE,
    FD_UNKNOWN
};

typedef struct _fde fde_t;

/* Callback for completed IO events */
typedef void PF(int, void *);

struct _fde {
    int mask;		/* priority values used by the old-school ircd code */

    /* New-school stuff, again pretty much ripped from squid */
    /*
     * Yes, this gives us only one pending read and one pending write per
     * filedescriptor. Think though: when do you think we'll need more?
     */
    int type;
    char desc[FD_DESC_SZ];
    PF *read_handler;
    void *read_data;
    PF *write_handler;
    void *write_data;
    time_t timeout;
    struct {
        unsigned int open:1;
        unsigned int close_request:1;
        unsigned int write_daemon:1;   
        unsigned int closing:1;
        unsigned int socket_eof:1;
        unsigned int nolinger:1;
        unsigned int nonblocking:1;
        unsigned int ipc:1;
        unsigned int called_connect:1;
    } flags;
};

extern fde_t *fd_table;

void fdlist_add(int fd, unsigned char mask);
void fdlist_delete(int fd, unsigned char mask);
void fdlist_init(void);
void fdlist_check(time_t now);

extern void  fd_open(int, unsigned int, const char *);
extern void  fd_close(int);


#endif /* INCLUDED_fdlist_h */

