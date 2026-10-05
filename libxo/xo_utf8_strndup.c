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
 * less if that would split a character.  The bytes are not otherwise
 * inspected.  The caller must free(3) the result.
 */
char *
xo_ustrndup (const char *str, size_t len)
{
    size_t n = strnlen(str, len);
    char *res;

    /* If we stopped on a secondary byte, back up over the character */
    while (n > 0 && (str[n] & 0xc0) == 0x80)
	n -= 1;

    res = malloc(n + 1);
    if (res) {
	memcpy(res, str, n);
	res[n] = '\0';
    }

    return res;
}
