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
 * Convert a string to lower case.
 */
void
xo_utf8_ntolower (char *str, size_t len)
{
    int ulen;

    char *cp = str, *ep = cp + len;
    for ( ; cp < ep; cp += ulen) {
	ulen = xo_utf8_len(*cp);
	xo_codepoint_t wc = xo_utf8_codepoint(cp, ep - cp, ulen, ' ');
	xo_codepoint_t lc = xo_utf8_wtolower(wc);
	if (wc == lc)		/* Is it already lower? */
	    continue;

	if (ulen != xo_utf8_to_len(lc)) /* Sanity check that lengths match */
	    continue;

	xo_utf8_to_bytes(cp, ulen, lc);
    }
}
