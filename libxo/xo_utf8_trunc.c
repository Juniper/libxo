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
 * Truncate a UTF-8 string at the given length while keeping the
 * string UTF-8 valid.
 */
size_t
xo_utrunc (char *str, size_t len)
{
    char *ep = str + len;
    char ch = *ep;
    *ep = '\0';

    /*
     * Okay, we've done the minimal action: we've truncated the
     * string, but now we need to ensure that we didn't nibble off
     * half a multi-byte UTF-8 character.  Since we know the string
     * was valid to start with, we can look at the character we nuked
     * to know if we're in the middle.  First characters have both high
     * bits set, while 2nd, 3rd, and 4th characters have only the high
     * bit set.
     */
    while ((ch & 0xc0) == 0x80 && ep > str) {
	ep -= 1;
	ch = *ep;
	*ep = '\0';
    }

    return ep - str;
}
