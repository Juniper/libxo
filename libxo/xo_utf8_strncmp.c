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
 * Return non-zero if the character holding str[off] is one that
 * 'len' cuts short.  The first byte of a character is at most three
 * bytes back, and says how long the character is.  Only bytes inside
 * 'len' are examined.
 */
static inline int
xo_utf8_inside_char (const char *str, size_t off, size_t len)
{
    size_t start = off;

    while (start > 0 && off - start < 3
	   && xo_is_utf8_secondary_byte(str[start]))
	start -= 1;

    if (!xo_is_utf8_len_byte(str[start]))
	return 0;		/* ASCII, or a stray secondary byte */

    return (start + xo_utf8_len(str[start]) > len);
}

/**
 * UTF-8 version of strncmp(3).  The strings are compared as bytes,
 * which for UTF-8 is also codepoint order.  The difference lies at
 * the limit: a character that 'len' cuts short is discarded, as if
 * its string ended before it, where strncmp would compare the bytes
 * of it that fall inside 'len'.  Nothing at or beyond 'len' is read,
 * so the strings need not be NUL-terminated.
 */
int
xo_ustrncmp (const char *s1, const char *s2, size_t len)
{
    const unsigned char *u1 = (const unsigned char *) s1;
    const unsigned char *u2 = (const unsigned char *) s2;
    size_t i;

    for (i = 0; i < len; i++) {
	if (u1[i] != u2[i]) {
	    /*
	     * A string whose character is cut short ends here.  When
	     * the bytes differ inside one character, both are cut and
	     * the strings are equal; when they differ at the start of
	     * a character, either side may be the one that is cut.
	     */
	    int c1 = xo_utf8_inside_char(s1, i, len) ? 0 : u1[i];
	    int c2 = xo_utf8_inside_char(s2, i, len) ? 0 : u2[i];

	    return c1 - c2;
	}

	if (u1[i] == '\0')
	    break;
    }

    /*
     * Everything matched.  If the last character is cut short, it is
     * cut the same way in both strings, so they are still equal.
     */
    return 0;
}
