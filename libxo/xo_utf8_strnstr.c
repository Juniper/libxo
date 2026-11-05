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
 * UTF-8 version of strnstr(3): find 'little' in the first 'len' bytes
 * of 'big'.  Since no UTF-8 character can appear inside another, a
 * byte search is sufficient.
 */
char *
xo_ustrnstr (const char *big, const char *little, size_t len)
{
    size_t blen = strnlen(big, len);
    size_t llen = strlen(little);
    const char *cp, *ep;

    if (llen == 0)
	return xo_utf8_unconst(big);

    if (llen > blen)
	return NULL;

    for (cp = big, ep = big + blen - llen; cp <= ep; cp++)
	if (*cp == *little && memcmp(cp, little, llen) == 0)
	    return xo_utf8_unconst(cp);

    return NULL;
}
