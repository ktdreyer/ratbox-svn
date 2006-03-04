/* $Id$ */
#ifndef INCLUDED_email_h
#define INCLUDED_email_h

int PRINTFLIKE(3, 4) send_email(const char *address, const char *subject, const char *format, ...);

#endif
