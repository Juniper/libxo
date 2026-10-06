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

/**
 * Return the number of characters (not bytes) in a string, looking
 * at no more than 'len' bytes; a negative 'len' means the string's
 * NUL is the only limit.  Each invalid byte counts as one character.
 */
ssize_t
xo_utf8_buf_clen (const char *buf, ssize_t len)
{
    if (buf == NULL)
	return 0;

    const char *cp = buf;
    ssize_t count = 0, left = len;
    int ulen, avail;

    for ( ; left != 0 && *cp != '\0'; cp += ulen, count++) {
	if (!xo_is_utf8_byte(*cp)) {
	    ulen = 1;		/* ASCII needs no decoding */
	    if (left > 0)
		left -= 1;
	    continue;
	}

	ulen = xo_utf8_len(*cp);

	/*
	 * Find how many of the character's bytes are really there, so
	 * that we never look beyond the NUL or the caller's limit.
	 */
	for (avail = 1; avail < ulen; avail++)
	    if (avail == left || cp[avail] == '\0')
		break;

	if (xo_utf8_iserror(xo_utf8_codepoint(cp, avail, ulen, 0)))
	    ulen = 1;

	if (left > 0)
	    left -= ulen;
    }

    return count;
}
