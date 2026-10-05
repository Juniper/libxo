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
 * UTF-8 version of strnlen(3): return the number of characters (not
 * bytes) in the first 'maxlen' bytes of the string.  Each invalid
 * byte counts as one character.
 */
size_t
xo_ustrnlen (const char *str, size_t maxlen)
{
    const char *cp = str, *ep = str + strnlen(str, maxlen);
    size_t count = 0;
    int ulen;

    for ( ; cp < ep; cp += ulen, count++) {
	ulen = xo_utf8_len(*cp);
	if (xo_utf8_iserror(xo_utf8_codepoint(cp, ep - cp, ulen, 0)))
	    ulen = 1;
    }

    return count;
}
