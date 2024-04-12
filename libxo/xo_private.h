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

#endif /* XO_PRIVATE_H */
