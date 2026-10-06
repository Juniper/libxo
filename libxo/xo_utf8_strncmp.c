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
 * UTF-8 version of strncmp(3).  The strings are compared as bytes,
 * which for UTF-8 is also codepoint order.  The difference lies at
 * the limit: when 'len' lands inside a character, strncmp sees only
 * the bytes the two characters share and calls them equal.  We
 * finish comparing that character, so up to three bytes past 'len'
 * may be read, but never past a NUL.
 */
int
xo_ustrncmp (const char *s1, const char *s2, size_t len)
{
    const unsigned char *u1 = (const unsigned char *) s1;
    const unsigned char *u2 = (const unsigned char *) s2;
    size_t i;

    if (len == 0)
	return 0;

    for (i = 0; i < len; i++) {
	if (u1[i] != u2[i])
	    return u1[i] - u2[i];
	if (u1[i] == '\0')
	    return 0;
    }

    /*
     * Everything matched up to the limit.  A secondary byte here
     * means the limit split a character, so keep going to its end.
     */
    for ( ; xo_is_utf8_secondary_byte(s1[i])
	      || xo_is_utf8_secondary_byte(s2[i]); i++)
	if (u1[i] != u2[i])
	    return u1[i] - u2[i];

    return 0;
}
