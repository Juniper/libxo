/*
 * Copyright (c) 2015, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, August 2015
 */

/*
 * This file is an _internal_ part of the libxo plumbing, not suitable
 * for external use.  It is not considered part of the libxo API and
 * will not be a stable part of that API.  Mine, not your's, dude...
 * The real hope is that something like this will become a standard part
 * of libc and I can kill this off.
 */

#ifndef XO_BUF_H
#define XO_BUF_H

#include "xo_private.h"

#define XO_BUFSIZ		(8*1024) /* Normal buffer size */
#define XO_BUFSIZ_SMALL		(256)    /* Smaller buffer size */
#define XO_BUF_HIGH_WATER	(XO_BUFSIZ - 512) /* When to auto-flush */

typedef ssize_t xo_off_t;	/* Offset within a buffer */

/*
 * xo_buffer_t: a memory buffer that can be grown as needed.  We
 * use them for building format strings and output data.
 */
typedef struct xo_buffer_s {
    char *xb_bufp;		/* Buffer memory */
    char *xb_curp;		/* Current insertion point */
    xo_off_t xb_size;		/* Size of buffer */
} xo_buffer_t;

/*
 * Initialize the contents of an xo_buffer_t.
 */
static inline void
xo_buf_init (xo_buffer_t *xbp)
{
    xbp->xb_size = XO_BUFSIZ;
    xbp->xb_bufp = xo_realloc(NULL, xbp->xb_size);
    xbp->xb_curp = xbp->xb_bufp;
}

/*
 * Reset the buffer to empty
 */
static inline void
xo_buf_reset (xo_buffer_t *xbp)
{
    xbp->xb_curp = xbp->xb_bufp;
}

/*
 * Return the number of bytes left in the buffer
 */
static inline int
xo_buf_left (xo_buffer_t *xbp)
{
    return xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
}

/*
 * See if the buffer to empty
 */
static inline int
xo_buf_is_empty (xo_buffer_t *xbp)
{
    return (xbp->xb_curp == xbp->xb_bufp);
}

/*
 * Return the current offset
 */
static inline xo_off_t
xo_buf_offset (xo_buffer_t *xbp)
{
    return xbp ? (xbp->xb_curp - xbp->xb_bufp) : 0;
}

static inline char *
xo_buf_data (xo_buffer_t *xbp, xo_off_t offset)
{
    if (xbp == NULL)
	return NULL;
    return xbp->xb_bufp + offset;
}

static inline char *
xo_buf_cur (xo_buffer_t *xbp)
{
    if (xbp == NULL)
	return NULL;
    return xbp->xb_curp;
}

static inline xo_ssize_t
xo_buf_len (xo_buffer_t *xbp)
{
    if (xbp == NULL || xbp->xb_bufp == NULL)
	return 0;

    return xbp->xb_curp - xbp->xb_bufp;
}

static inline void
xo_buf_set_len (xo_buffer_t *xbp, xo_off_t len)
{
    if (xbp && xbp->xb_bufp && len < xbp->xb_size)
	xbp->xb_curp = xbp->xb_bufp + len;
}

static inline char *
xo_buf_trim (xo_buffer_t *xbp, xo_ssize_t len)
{
    if (xbp == NULL)
	return NULL;

    if (xbp->xb_bufp - xbp->xb_curp >= len)
	xbp->xb_curp = xbp->xb_bufp;
    else
	xbp->xb_curp -= len;

    return xbp->xb_curp;
}

/*
 * Initialize the contents of an xo_buffer_t.
 */
static inline void
xo_buf_cleanup (xo_buffer_t *xbp)
{
    if (xbp->xb_bufp)
	xo_free(xbp->xb_bufp);
    bzero(xbp, sizeof(*xbp));
}

/*
 * Does the buffer have room for the given number of bytes of data?
 * If not, realloc the buffer to make room.  If that fails, we
 * return 0 to tell the caller they are in trouble.
 */
static inline int
xo_buf_has_some_room (xo_buffer_t *xbp, ssize_t len, ssize_t bufsiz)
{
    if (xbp->xb_curp + len >= xbp->xb_bufp + xbp->xb_size) {
	/*
	 * Find out how much new space we need, round it up to bufsiz
	 */
	ssize_t sz = (xbp->xb_curp + len) - xbp->xb_bufp;
	sz = (sz + bufsiz - 1) & ~(bufsiz - 1);

	char *bp = xo_realloc(xbp->xb_bufp, sz);
	if (bp == NULL)
	    return 0;

	xbp->xb_curp = bp + (xbp->xb_curp - xbp->xb_bufp);
	xbp->xb_bufp = bp;
	xbp->xb_size = sz;
    }

    return 1;
}

static inline int
xo_buf_has_room (xo_buffer_t *xbp, ssize_t len)
{
    return xo_buf_has_some_room(xbp, len, XO_BUFSIZ);
}

static inline int
xo_buf_has_small_room (xo_buffer_t *xbp, ssize_t len)
{
    return xo_buf_has_some_room(xbp, len, XO_BUFSIZ_SMALL);
}

static inline int
xo_buf_make_some_room (xo_buffer_t *xbp, ssize_t size, ssize_t bufsiz)
{
    if (size >= xbp->xb_size) {
	/*
	 * Find out how much new space we need, round it up to bufsiz
	 */
	ssize_t sz = (xbp->xb_curp + size) - xbp->xb_bufp;
	sz = (sz + bufsiz - 1) & ~(bufsiz - 1);

	char *bp = xo_realloc(xbp->xb_bufp, sz);
	if (bp == NULL)
	    return 0;

	xbp->xb_curp = bp + (xbp->xb_curp - xbp->xb_bufp);
	xbp->xb_bufp = bp;
	xbp->xb_size = sz;
    }

    return 1;
}

static inline int
xo_buf_make_room (xo_buffer_t *xbp, ssize_t size)
{
    return xo_buf_make_some_room(xbp, size, XO_BUFSIZ);
}

static inline int
xo_buf_make_small_room (xo_buffer_t *xbp, ssize_t size)
{
    return xo_buf_make_some_room(xbp, size, XO_BUFSIZ_SMALL);
}

/*
 * Append the given string to the given buffer
 */
static inline char *
xo_buf_append_val (xo_buffer_t *xbp, const char *str, ssize_t len)
{
    if (str == NULL || len == 0 || !xo_buf_has_room(xbp, len))
	return NULL;

    char *val = xbp->xb_curp;	/* The "new" value */

    memcpy(xbp->xb_curp, str, len);
    xbp->xb_curp += len;

    return val;
}

static inline void
xo_buf_append (xo_buffer_t *xbp, const char *str, ssize_t len)
{
    (void) xo_buf_append_val(xbp, str, len);
}

/*
 * Append the given NUL-terminated string to the given buffer
 */
static inline char *
xo_buf_append_str_val (xo_buffer_t *xbp, const char *str)
{
    ssize_t len = strlen(str);

    if (!xo_buf_has_room(xbp, len))
	return NULL;

    char *val = xbp->xb_curp;	/* The "new" value */

    memcpy(xbp->xb_curp, str, len);
    xbp->xb_curp += len;

    return val;
}

static inline void
xo_buf_append_str (xo_buffer_t *xbp, const char *str)
{
    (void) xo_buf_append_str_val(xbp, str);
}

static inline void
xo_buf_append_buf (xo_buffer_t *dst, xo_buffer_t *src)
{
    char *str = src->xb_bufp;
    ssize_t len = src->xb_curp - str;

    (void) xo_buf_append_val(dst, str, len);
}

static inline void
xo_buf_force_nul (xo_buffer_t *dst)
{
    if (xo_buf_append_val(dst, "", 1))
	dst->xb_curp -= 1;
}

#endif /* XO_BUF_H */
