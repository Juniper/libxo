/*
 * Copyright (c) 2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef XO_PRIVATE_H
#define XO_PRIVATE_H

/* Thread-local storage macros */

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
#define THREAD_LOCAL(_x) __declspec(_x)
#endif

/*
 * Expose libxo's memory allocation functions
 */
extern xo_realloc_func_t xo_realloc;
extern xo_free_func_t xo_free;

/*
 * Simple string comparison function (without the temptation
 * to forget the "== 0").
 */
static inline int
xo_streq (const char *one, const char *two)
{
    return strcmp(one, two) == 0;
}

/*
 * Simple string comparison function (without the temptation
 * to forget the "== 0").
 */
static inline int
xo_streqn (const char *one, const char *two, ssize_t len_of_two)
{
    return strncmp(one, two, len_of_two + 1) == 0;
}

/* Rather lame that we can't count on these... */
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#ifndef XO_UNUSED
#define XO_UNUSED __attribute__ ((__unused__))
#endif /* XO_UNUSED */

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
    unsigned char *cp = data;

    for (cp += len; len > 0; len--) {
	if (*--cp == (unsigned char) c)
	    return cp;
    }

    return NULL;
}
#endif /* HAVE_MEMRCHR */

void
xo_dbg (xo_handle_t *xop, const char *fmt, ...);

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

const char *
xo_filt_status_name (xo_filter_status_t fstatus);

#endif /* XO_PRIVATE_H */
