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
 * Return non-zero if the codepoint appears in the charset
 */
static int
xo_utf8_in_charset (xo_codepoint_t wc, const char *charset)
{
    const char *cp = charset, *ep = charset + strlen(charset);
    int ulen;

    for ( ; cp < ep; cp += ulen) {
	ulen = xo_utf8_len(*cp);
	if (xo_utf8_codepoint(cp, ep - cp, ulen, 0) == wc)
	    return 1;
    }

    return 0;
}

/*
 * Return the number of bytes at the start of the string made of
 * characters that are in the charset ('want' is 1) or that are not
 * in it ('want' is 0).  An invalid byte is in no charset.
 */
static size_t
xo_utf8_span (const char *str, const char *charset, int want)
{
    const char *cp = str, *ep = str + strlen(str);
    int ulen, found;
    xo_codepoint_t wc;

    for ( ; cp < ep; cp += ulen) {
	if (!xo_is_utf8_byte(*cp)) {
	    /* ASCII needs no decoding, and can't match part of a character */
	    ulen = 1;
	    found = (strchr(charset, *cp) != NULL);
	} else {
	    ulen = xo_utf8_len(*cp);
	    wc = xo_utf8_codepoint(cp, ep - cp, ulen, 0);
	    if (xo_utf8_iserror(wc)) {
		ulen = 1;
		found = 0;
	    } else
		found = xo_utf8_in_charset(wc, charset);
	}

	if (found != want)
	    break;
    }

    return cp - str;
}

/**
 * UTF-8 version of strspn(3).  The return value is a count of bytes,
 * not characters, so it can be used as an offset into the string.
 */
size_t
xo_ustrspn (const char *str, const char *charset)
{
    return xo_utf8_span(str, charset, 1);
}

/**
 * UTF-8 version of strcspn(3).  The return value is a count of bytes,
 * not characters, so it can be used as an offset into the string.
 */
size_t
xo_ustrcspn (const char *str, const char *charset)
{
    return xo_utf8_span(str, charset, 0);
}

/**
 * UTF-8 version of strpbrk(3)
 */
char *
xo_ustrpbrk (const char *str, const char *charset)
{
    const char *cp = str + xo_utf8_span(str, charset, 0);

    return *cp ? xo_utf8_unconst(cp) : NULL;
}

/**
 * UTF-8 version of strsep(3).  Each character of 'delim' is a
 * delimiter, and may be multi-byte; the whole delimiter character is
 * removed, where strsep would cut it apart.
 */
char *
xo_ustrsep (char **stringp, const char *delim)
{
    char *str = *stringp;

    if (str == NULL)
	return NULL;

    char *cp = str + xo_utf8_span(str, delim, 0);

    if (*cp == '\0') {
	*stringp = NULL;	/* No delimiter; this is the last token */
    } else {
	*stringp = cp + xo_utf8_len(*cp);
	*cp = '\0';
    }

    return str;
}
