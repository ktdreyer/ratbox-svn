/*
 * linebuf.c - a replacement message buffer mechanism
 *
 * Adrian Chadd <adrian@creative.net.au>
 *
 * The idea here is that we should really be maintaining pre-munged
 * buffer "lines" which we can later refcount to save needless copies.
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools.h"
#include "linebuf.h"
#include "blalloc.h"

static int linebuf_initialised;
static BlockHeap *linebuf_bl = NULL;


/*
 * linebuf_init
 *
 * Initialise the dbuf mechanism
 */
void
linebuf_init(void)
{
    assert(!linebuf_initialised);

    linebuf_bl = BlockHeapCreate(sizeof(buf_line_t), BUF_BLOCK_SIZE);

    linebuf_initialised = 1;
}


/*
 * linebuf_new_line
 *
 * Create a new line, and link it to the given linebuf.
 * It will be initially empty.
 */
static buf_line_t *
linebuf_new_line(buf_head_t *bufhead)
{
    buf_line_t *bufline;

    bufline = BlockHeapAlloc(linebuf_bl);

    /* XXX Zero data, I'm being paranoid! -- adrian */
    bufline->len = 0;
    bufline->terminated = 0;
    bufline->overflow = 0;
    bzero(&bufline->buf, BUF_DATA_SIZE);

    /* Stick it at the end of the buf list */
    dlinkAddTail(bufline, &bufline->node, &bufhead->list);

    /* And finally, update the allocated size */
    bufhead->alloclen += BUF_DATA_SIZE;

    return bufline;
}


/*
 * linebuf_done_line
 *
 * We've finished with the given line, so deallocate it
 */
static void
linebuf_done_line(buf_head_t *bufhead, buf_line_t *bufline)
{
    /* Remove it from the linked list */
    dlinkDelete(&bufline->node, &bufhead->list);

    /* Update the allocated size */
    bufhead->alloclen -= BUF_DATA_SIZE;

    /* and finally, deallocate the buf */
    BlockHeapFree(linebuf_bl, bufline);
}


/*
 * skip to end of line or the crlfs, return the number of bytes ..
 */
static int
linebuf_skip_crlf(char *ch, int len)
{
     int cpylen = 0;

     while (len && (*ch != '\012') && (*ch != '\015')) {
         ch++;
         len--;
         cpylen++;
     }
     return cpylen;
}


/*
 * linebuf_newbuf
 *
 * Initialise the new buffer
 */
void
linebuf_newbuf(buf_head_t *bufhead)
{
    /* not much to do :) */
    bzero(bufhead, sizeof(buf_head_t));
}


/*
 * linebuf_donebuf
 *
 * Flush all the lines associated with this buffer
 */
void
linebuf_donebuf(buf_head_t *bufhead)
{
    while (bufhead->list.head != NULL) {
        linebuf_done_line(bufhead, (buf_line_t *)bufhead->list.head->data);
    }
}



/*
 * linebuf_copy_line
 *
 * copy data into the given line. Return the number of bytes copied.
 * It will try to squeeze what it can into the given buffer.
 * If it hits an end-of-buffer before it hits a CRLF, it will flag
 * an overflow, and skip to the next CRLF. If it hits a CRLF before
 * filling the buffer, it will flag it terminated, and skip past
 * the CRLF.
 *
 * Just remember these:
 *
 * - we will *always* return skipped past a CRLF.
 * - if we hit an end-of-buffer before a CRLF is reached, we tag it as
 *   overflowed.
 * - if we hit a CRLF before an end-of-buffer, we terminate it and
 *   skip the CRLF
 * - we *always* null-terminate the buffer! 
 * - My definition of a CRLF is one of CR, LF, CRLF, LFCR. Hrm.
 *   We will attempt to skip multiple CRLFs in a row ..
 *
 * This routine probably isn't as optimal as it could be, but hey .. :)
 *   -- adrian
 */
static int
linebuf_copy_line(buf_head_t *bufhead, buf_line_t *bufline,
  char *data, int len)
{
    int cpylen = 0;	/* how many bytes we've copied */
    char *ch = data;	/* Pointer to where we are in the read data */
    char *bufch = &bufline->buf[bufline->len];
			/* Start of where to put new data */

    /* If its full or terminated, ignore it */
    if ((bufline->len == BUF_DATA_SIZE) || (bufline->terminated == 1))
        return 0;

    /* Next, lets enter the copy loop */
    while (1) {
        /* Are we out of data? */
        if (len == 0) {
            /* Yes, so we mark this buffer as unterminated, and return */
            bufline->terminated = 0; /* XXX it should be anyway :) */
            goto finish;
        }

	/* Are we out of space to PUT this ? */
	if (bufline->len == BUF_DATA_SIZE) {
            /*
             * ok, we null terminate, set overflow, and loop until the
             * next CRLF. We then skip that, and return.
             */
            bufline->terminated = 0;
            bufline->overflow = 1;
            cpylen += linebuf_skip_crlf(ch, len);
            /* NOTE: We're finishing, so ignore updating len */
            goto finish;
        }

        /* Is this a CR or LF? */
        if ((*ch == '\012') || (*ch == '\015')) {
            /* Skip */
            cpylen += linebuf_skip_crlf(ch, len);
            /* NOTE: We're finishing, so ignore updating len */
            goto finish;
        }

        /*
         * phew! we can copy a byte. Do it, and update the counters.
         * this definitely blows our register sets on most sane archs,
         * but hey, someone can recode this later on if the want to.
         */
        *bufch = *ch; 
        bufch++;
        ch++;
        cpylen++;
        len--;
        bufline->len++;
        bufhead->len++;
    }
finish:
    /* I hate gotos, but this is nice and common .. */
    /* terminate string and then return number of bytes copied .. */
    *bufch = '\000';
    return cpylen;
}


/*
 * linebuf_parse
 *
 * Take a given buffer and break out as many buffers as we can.
 * If we find a CRLF, we terminate that buffer and create a new one.
 * If we don't find a CRLF whilst parsing a buffer, we don't mark it
 * 'finished', so the next loop through we can continue appending ..
 *
 * A few notes here, which you'll need to understand before continuing.
 *
 * - right now I'm only dealing with single sized buffers. Later on,
 *   I might consider chaining buffers together to get longer "lines"
 *   but seriously, I don't see the advantage right now.
 *
 * - This *is* designed to turn into a reference-counter-protected setup
 *   to dodge copious copies.
 */
void
linebuf_parse(buf_head_t *bufhead, char *data, int len)
{
    buf_line_t *bufline;
    int cpylen;

    /* First, if we have a partial buffer, try to squeze data into it */
    if (bufhead->list.tail != NULL) {
        /* Check we're doing the partial buffer thing */
        bufline = bufhead->list.tail->data;
        /* just try, the worst it could do is *reject* us .. */
        cpylen = linebuf_copy_line(bufhead, bufline, data, len);
        /* If we've copied the same as what we've got, quit now */
        if (cpylen == len)
            return; /* all the data done so soon? */

        /* Skip the data and update len .. */
        len -= cpylen;
        data += cpylen;
    }
    /* Next, the loop */
    while (len > 0) {
        /* We obviously need a new buffer, so .. */
        bufline = linebuf_new_line(bufhead);
        
        /* And parse */
        cpylen = linebuf_copy_line(bufhead, bufline, data, len);
        len -= cpylen;
        data += cpylen;
    }
}


/*
 * linebuf_get
 *
 * get the next buffer from our line. For the time being it will copy
 * data into the given buffer and free the underlying linebuf.
 */
int
linebuf_get(buf_head_t *bufhead, char *buf, int buflen)
{
    buf_line_t *bufline;
    int cpylen;

    /* make sure we have a line */
    if (bufhead->list.head == NULL)
        return 0; /* Obviously not.. hrm. */

    /* make sure we've got the space, including the NULL */
    bufline = bufhead->list.head->data; 
    cpylen = bufline->len;
    assert(cpylen + 1 <= buflen);

    /* Copy it */
    memcpy(buf, bufline->buf, cpylen);

    /* Deallocate the line */
    linebuf_done_line(bufhead, bufline);

    /* return how much we copied */
    return cpylen;
}
