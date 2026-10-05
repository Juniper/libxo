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
 * Inspect a string to see if it's valid UTF-8.  Returns either NULL
 * indicating success, or a pointer to the start of invalid character.
 */
char *
xo_utf8_nvalid (char *str, size_t len)
{
    char *cp;
    char *ep;
    xo_codepoint_t wc;

    /*
     * Whiffle thru the string, looking for invalid characters.  We
     * don't need to look at 'len' since xo_utf8_codepoint will check
     * it.
     */
    for (cp = str, ep = str + len; cp < ep; cp += len) {
	len = xo_utf8_len(*cp);
	wc = xo_utf8_codepoint(cp, ep - cp, len, 0);
	if (xo_utf8_iserror(wc))
	    return cp;
    }

    return NULL;
}
