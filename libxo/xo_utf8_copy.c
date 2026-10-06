/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2022-2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, October 2022
 */

#include "xo_config.h"
#include "xo.h"
#include "xo_utf8.h"

/*
 * Copy UTF-8 text from 'src' to 'dst', reading at most 'srclen' bytes
 * and writing at most 'room' bytes, not counting the NUL that we
 * always add.  A character is copied whole or not at all, so the
 * copy stops early when the next character will not fit or when the
 * input ends in the middle of one.  An invalid byte is copied as a
 * space.  Returns a pointer to the NUL.
 */
static char *
xo_utf8_copy (char *dst, size_t room, const char *src, size_t srclen)
{
    const char *sp = src, *sep = src + strnlen(src, srclen);
    char *cp = dst, *ep = dst + room;
    int ulen;
    xo_codepoint_t wc;

    while (cp < ep && sp < sep) {
	if (!xo_is_utf8_byte(*sp)) {
	    *cp++ = *sp++;	/* ASCII */
	    continue;
	}

	ulen = xo_utf8_len(*sp);
	wc = xo_utf8_codepoint(sp, sep - sp, ulen, 0);
	if (wc == XO_UTF8_ERR_TRUNCATED)
	    break;

	if (xo_utf8_iserror(wc)) {
	    *cp++ = ' ';
	    sp += 1;		/* We only consume one byte */
	    continue;
	}

	if (ulen > ep - cp)	/* No room for the whole character */
	    break;

	/* A valid character's own bytes are what we'd make from it */
	memcpy(cp, sp, ulen);
	cp += ulen;
	sp += ulen;
    }

    *cp = '\0';			/* Always NUL terminate */

    return cp;
}

/**
 * UTF-8 version of strncat(3) with strlcat safety, because why not be
 * safe?  We take care not to leave a half-made UTF-8 character at the
 * end, and turn invalid bytes into spaces.  At most 'count' bytes of
 * 'append' are used.  As with strlcat(3), the return value is the
 * length of the string we tried to create; a value of 'dstsize' or
 * more means the result was truncated.
 */
size_t
xo_ustrlncat (char * restrict dst, const char * restrict append,
	      size_t dstsize, size_t count)
{
    size_t dstlen = strnlen(dst, dstsize);
    size_t applen = strnlen(append, count);

    /* With no NUL inside dstsize, there is nothing we can safely touch */
    if (dstlen == dstsize)
	return dstsize + applen;

    xo_utf8_copy(dst + dstlen, dstsize - dstlen - 1, append, applen);

    return dstlen + applen;
}

/**
 * UTF-8 version of strlcpy(3).  'dstsize' is the size of 'dst'.  The
 * return value is the length of 'src'; a value of 'dstsize' or more
 * means the result was truncated.
 */
size_t
xo_ustrlcpy (char * restrict dst, const char * restrict src, size_t dstsize)
{
    size_t srclen = strlen(src);

    if (dstsize > 0)
	xo_utf8_copy(dst, dstsize - 1, src, srclen);

    return srclen;
}

/**
 * UTF-8 version of stpncpy(3).  Unlike stpncpy, the result is always
 * NUL terminated and is not padded, so 'dst' must have room for
 * 'len' + 1 bytes.  Returns a pointer to the NUL.
 */
char *
xo_ustpncpy (char * restrict dst, const char * restrict src, size_t len)
{
    return xo_utf8_copy(dst, len, src, len);
}

/**
 * UTF-8 version of strncpy(3).  Unlike strncpy, the result is always
 * NUL terminated and is not padded, so 'dst' must have room for
 * 'len' + 1 bytes.
 */
char *
xo_ustrncpy (char * restrict dst, const char * restrict src, size_t len)
{
    xo_utf8_copy(dst, len, src, len);

    return dst;
}
