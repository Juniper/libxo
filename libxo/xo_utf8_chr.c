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

/*
 * Write the NUL-terminated UTF-8 form of a codepoint into 'buf',
 * which must hold five bytes.  Returns the length, or -1 if the
 * codepoint cannot be encoded.
 */
static ssize_t
xo_utf8_encode (char *buf, xo_codepoint_t wc)
{
    ssize_t len = xo_utf8_to_len(wc);

    if (len <= 0)		/* No UTF-8 form, so nothing to find */
	return -1;

    xo_utf8_to_bytes(buf, len, wc);
    buf[len] = '\0';

    return len;
}

/**
 * UTF-8 version of strchr(3), for any codepoint.  Since no UTF-8
 * character can appear inside another, a byte search is sufficient.
 */
char *
xo_ustrchr_long (const char *str, xo_codepoint_t c)
{
    char buf[5];

    if (c < 0x80)		/* Also handles looking for the NUL */
	return strchr(str, c);

    if (xo_utf8_encode(buf, c) < 0)
	return NULL;

    return strstr(str, buf);
}

/**
 * UTF-8 version of strrchr(3), for any codepoint
 */
char *
xo_ustrrchr_long (const char *str, xo_codepoint_t c)
{
    char buf[5];
    char *cp, *last = NULL;

    if (c < 0x80)
	return strrchr(str, c);

    ssize_t len = xo_utf8_encode(buf, c);
    if (len < 0)
	return NULL;

    for (cp = strstr(str, buf); cp; cp = strstr(cp + len, buf))
	last = cp;

    return last;
}

/**
 * UTF-8 version of strchrnul(3), for any codepoint
 */
char *
xo_ustrchrnul_long (const char *str, xo_codepoint_t c)
{
    char *cp = xo_ustrchr_long(str, c);

    return cp ?: xo_utf8_unconst(str + strlen(str));
}
