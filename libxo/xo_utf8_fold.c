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
 * Return the case-folded codepoint at the start of the string, with
 * the number of bytes it uses.  An invalid sequence uses one byte and
 * folds to U+FFFD, the replacement character, so that broken input
 * compares in a stable way without matching any real text.
 */
static xo_codepoint_t
xo_utf8_fold_next (const char *str, size_t len, size_t *ulenp)
{
    /* ASCII needs no decoding; 'len' is never zero here */
    if (!xo_is_utf8_byte(*str)) {
	*ulenp = 1;
	xo_codepoint_t ch = (unsigned char) *str;
	return (ch >= 'A' && ch <= 'Z') ? ch + 0x20 : ch;
    }

    int ulen = xo_utf8_len(*str);
    xo_codepoint_t wc = xo_utf8_codepoint(str, len, ulen, 0);

    if (xo_utf8_iserror(wc)) {
	*ulenp = 1;
	return 0xfffd;
    }

    *ulenp = ulen;
    return xo_utf8_wtolower(wc);
}

/**
 * UTF-8 version of strncasecmp(3), but with two lengths
 */
int
xo_ustrncasecmp (const char *s1, size_t s1_len, const char *s2, size_t s2_len)
{
    xo_codepoint_t s1_wchar, s2_wchar;
    size_t s1_wlen, s2_wlen;

    while (s1_len > 0 && s2_len > 0) {
	s1_wchar = xo_utf8_fold_next(s1, s1_len, &s1_wlen);
	s2_wchar = xo_utf8_fold_next(s2, s2_len, &s2_wlen);

	if (s1_wchar != s2_wchar)
	    return (s1_wchar < s2_wchar ? -1 : 1);

	s1 += s1_wlen;
	s1_len -= s1_wlen;
	s2 += s2_wlen;
	s2_len -= s2_wlen;
    }

    /* The shorter string sorts first, as with strcasecmp(3) */
    return (s2_len > 0 ? -1 : s1_len > 0 ? 1 : 0);
}

/**
 * UTF-8 version of strcasestr(3)
 */
char *
xo_ustrcasestr (const char *big, const char *little)
{
    const char *bep = big + strlen(big), *lep = little + strlen(little);
    const char *start, *bp, *lp;
    size_t ulen, bulen, lulen;

    if (little == lep)
	return xo_utf8_unconst(big);

    for (start = big; start < bep; start += ulen) {
	xo_utf8_fold_next(start, bep - start, &ulen);

	for (bp = start, lp = little; bp < bep && lp < lep;
	     bp += bulen, lp += lulen) {
	    if (xo_utf8_fold_next(bp, bep - bp, &bulen)
		    != xo_utf8_fold_next(lp, lep - lp, &lulen))
		break;
	}

	if (lp == lep)
	    return xo_utf8_unconst(start);
    }

    return NULL;
}
