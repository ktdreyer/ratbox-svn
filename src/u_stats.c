/* $Id$ */
#include "stdinc.h"
#include "rserv.h"
#include "conf.h"

const char *
get_duration(time_t seconds)
{
        static char buf[BUFSIZE];
        int days, hours, minutes;

        days = (int) (seconds / 86400);
        seconds %= 86400;
        hours = (int) (seconds / 3600);
        hours %= 3600;
        minutes = (int) (seconds / 60);
        seconds %= 60;

        snprintf(buf, sizeof(buf), "%d day%s, %d:%02d:%02ld",
                 days, (days == 1) ? "" : "s", hours,
                 minutes, seconds);

        return buf;
}
