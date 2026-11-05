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
 * Ensure that a string is valid UTF-8 by replacing any invalid bytes
 * with the replacement byte.  If the replacement character is NUL (0),
 * then replacement will terminate with the first replacement.  Returns
 * the number of times a replacement is made.
 */
int
xo_utf8_nmakevalid (char *str, size_t len, char replacement)
{
    char *cp;
    char *ep;
    xo_codepoint_t wc;
    int rc = 0;

    /*
     * Whiffle thru the string, looking for invalid characters.  We
     * don't need to look at 'len' since xo_utf8_codepoint will check
     * it.
     */
    for (cp = str, ep = cp + len; cp < ep; cp += len) {
	if (!xo_is_utf8_byte(*cp)) {
	    len = 1;		/* ASCII is always valid */
	    continue;
	}

	len = xo_utf8_len(*cp);
	wc = xo_utf8_codepoint(cp, ep - cp, len, 0);
	if (!xo_utf8_iserror(wc))
	    continue;

	rc += 1;
	len = 1;		/* We only consume one byte */
	*cp = replacement;

	if (replacement == 0)
	    break;
    }

    return rc;
}
