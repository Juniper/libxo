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

#include <stdlib.h>

#include "xo_config.h"
#include "xo.h"
#include "xo_utf8.h"

/**
 * UTF-8 version of strndup(3).  At most 'len' bytes are duplicated,
 * less if the copy would end with part of a character, whether
 * 'len' lands inside one or the string itself stops short.  The
 * bytes are not otherwise inspected.  The caller must free(3) the
 * result.
 */
char *
xo_ustrndup (const char *str, size_t len)
{
    size_t n = strnlen(str, len), i = n;
    char *res;

    /*
     * Find the first byte of the last character; if that byte asks
     * for more bytes than we have, the character is incomplete and
     * we drop it.
     */
    while (i > 0 && n - i < 3 && xo_is_utf8_secondary_byte(str[i - 1]))
	i -= 1;

    if (i > 0 && xo_is_utf8_len_byte(str[i - 1])) {
	i -= 1;
	if ((size_t) xo_utf8_len(str[i]) > n - i)
	    n = i;
    }

    res = malloc(n + 1);
    if (res) {
	memcpy(res, str, n);
	res[n] = '\0';
    }

    return res;
}
