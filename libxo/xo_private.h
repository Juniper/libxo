/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2023-2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef XO_PRIVATE_H
#define XO_PRIVATE_H

#include <inttypes.h>
#include "xo_buf.h"
#include "xo_encoder.h"

/*
 * Three styles of specifying thread-local variables are supported.
 * configure.ac has the brains to run each possibility through the
 * compiler and see what works; we are left to define the THREAD_LOCAL
 * macro to the right value.  Most toolchains (clang, gcc) use
 * "before", but some (borland) use "after" and I've heard of some
 * (ms) that use __declspec.  Any others out there?
 */
#define THREAD_LOCAL_before 1
#define THREAD_LOCAL_after 2
#define THREAD_LOCAL_declspec 3

#ifndef HAVE_THREAD_LOCAL
#define THREAD_LOCAL(_x) _x
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_before
#define THREAD_LOCAL(_x) __thread _x
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_after
#define THREAD_LOCAL(_x) _x __thread
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_declspec
#define THREAD_LOCAL(_x) __declspec(thread) _x
#else
#error unknown thread-local setting
#endif /* HAVE_THREAD_LOCAL */

/*
 * Insert the locale's thousands separator into a just-rendered decimal
 * integer field; see the implementation in libxo.c for details.  Exposed
 * here so that other translation units that render their own decimal
 * fields can apply grouping directly.  A formatter set by
 * xo_set_formatter() must not call it, since libxo groups the
 * formatter's output itself.
 */
xo_off_t
xo_grouping_fixup (xo_handle_t *xop, xo_buffer_t *xbp,
		    xo_off_t start_off, xo_off_t rc);

#ifdef PRId64
#define XO_PRId64 PRId64
#else
#define XO_PRId64 "lld"
#endif /* PRId64 */

#ifdef PRIu64
#define XO_PRIu64 PRIu64
#else
#define XO_PRIu64 "llu"
#endif /* PRIu64 */

/*
 * strdup() by way of xo_realloc, so the result can be given to
 * xo_free().  The two allocators differ once the application calls
 * xo_set_allocator(), and then a string from strdup() cannot be
 * released with xo_free().
 */
static inline char *
xo_strdup (const char *str)
{
    size_t len = strlen(str) + 1;
    /* Sadly, c++ needs this cast and we are included by c++ */
    char *cp = (char *) xo_realloc(NULL, len);

    if (cp)
	memcpy(cp, str, len);

    return cp;
}

/*
 * strndup() by way of xo_realloc, for the same reason, with a twist:
 * len < 0 means len = strlen(str).
 */
static inline char *
xo_strndup (const char *str, ssize_t len)
{
    if (len < 0)
	len = strlen(str);

    /* Sadly, c++ needs this cast and we are included by c++ */
    char *cp = (char *) xo_realloc(NULL, len + 1);

    if (cp) {
	memcpy(cp, str, len);
	cp[len] = '\0';
    }

    return cp;
}

/* Rather lame that we can't count on these... */
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#ifndef UNUSED
#define UNUSED XO_UNUSED
#endif /* UNUSED */

/* Allows us to turn off all debug overhead */
#define XO_HAS_DEBUG(_xop) ((_xop) && xo_get_flags(_xop) & XOF_DEBUG)

#ifdef XO_XPARSE_DEBUG
#define XO_DBG(_xop, _fmt...) \
    do { if (XO_HAS_DEBUG(_xop)) xo_dbg(_xop, _fmt);} while(0)
#else /* XO_XPARSE_DEBUG */
#define XO_DBG(_xop, _fmt...) do { } while (0)
#endif /* XO_XPARSE_DEBUG */

#define SNPRINTF(_start, _end, _fmt...) \
    do { \
        (_start) += snprintf((_start), (_end) - (_start), _fmt); \
        if ((_start) > (_end)) \
            (_start) = (_end); \
    } while (0)

#ifdef HAVE_MEMRCHR
#define xo_memrchr memrchr
#else /* HAVE_MEMRCHR */
static inline void *
xo_memrchr (void *data, int c, xo_ssize_t len)
{
    /* Sadly, c++ needs this cast and we are included by c++ */
    unsigned char *cp = (unsigned char *) data;

    for (cp += len; len > 0; len--) {
	if (*--cp == (unsigned char) c)
	    return cp;
    }

    return NULL;
}
#endif /* HAVE_MEMRCHR */

void
xo_dbg (xo_handle_t *xop, const char *fmt, ...) XO_PRINTFLIKE(2, 3);

void
xo_dbg_v (xo_handle_t *xop UNUSED, const char *fmt UNUSED, va_list vap UNUSED);

/*
 * The base libxo code needs to know just a little about filtering.
 * Anything it needs goes here.
 */

/* Tracking status: how closely are we watching filtering? */
typedef uint32_t xo_filter_status_t;

/* Value for xo_filter_status_t */
#define XO_STATUS_ZERO	0	/* not on/working/loaded/enabled */
#define XO_STATUS_FULL	1	/* Fully open: let's make some output */
#define XO_STATUS_TRACK	2	/* Track open/close/key paths, but no data */
#define XO_STATUS_PRED	3	/* Looking for a predicate */
#define XO_STATUS_DEAD	4	/* Nope, it's dead under this hierarchy */
#define XO_STATUS_COND	5	/* FULL, if a pending predicate passes */
#define XO_STATUS_LIVE	6	/* A pending predicate passed (return only) */

const char *
xo_filt_status_name (xo_filter_status_t fstatus);

#endif /* XO_PRIVATE_H */
