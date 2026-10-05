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
 * Return the codepoint to a UTF-8 character
 */
xo_codepoint_t
xo_utf8_codepoint (const char *buf, size_t bufsiz, int len,
		   xo_codepoint_t on_err)
{
    char b1 = *buf, b2, b3, b4;
    xo_codepoint_t wc = 0;

    /*
     * The caller should really ensure this, but it seems odd to not
     * take a 'bufsiz' parameter, and if we take it, we should test it.
     */
    if (len <= 0 || (size_t) len > bufsiz)
	return on_err ?: XO_UTF8_ERR_TRUNCATED;

    /* Are we looking at a secondary byte? */
    if ((b1 & 0xc0) == 0x80)
	return on_err ?: XO_UTF8_ERR_SECONDARY;

    /*
     * The length must be the one the first byte calls for.  Callers
     * that use xo_utf8_len() pass 1 for a first byte with bad length
     * bits (0xf8 and up), which would otherwise decode as ASCII.
     */
    if (xo_utf8_rlen(b1) != len)
	return on_err ?: XO_UTF8_ERR_BAD_LEN;

    /*
     * For determining the "shortest form", consider the following table::
     *
     *  Scalar Value               | Byte 1   | Byte 2   | Byte 3   | Byte 4
     *  00000000 00000000 0aaaaaaa | 0aaaaaaa |          |          |
     *  00000000 00000bbb baaaaaaa | 110bbbba | 10aaaaaa |          |
     *  00000000 ccccbbbb baaaaaaa | 1110cccc | 10cbbbba | 10aaaaaa |
     *  000ddddd dccccbbb baaaaaaa | 11110ddd | 10dddccc | 10cbbbba | 10aaaaaa
     *

     * For each length, 'zeroes' is set to the bits that should be zero.
     */

    /*
     * 'test' is the bits we extract
     * 'match' is the (const) value it must match
     * 'zeros' is the bits that must _not_ be zeros; if they are zero, then
     * the value is encoded in a "non-shortest form", which we reject as
     * directed by Unicode TR-36.
     */
    xo_codepoint_t test, match, zeros;

    b1 &= xo_utf8_data_bits(len);
    if (len == 1) {
	wc = (unsigned char) b1; /* Make the simple case simple */

    } else {
	if (len == 2) {
	    b2 = buf[1];
	    test = b2 & 0xc0;
	    match = 0x80;
	    wc = (b1 << 6) | (b2 & 0x3f);
	    zeros = b1 & 0x1e; /* 0b0001_1110 */

	} else if (len == 3) {
	    b2 = buf[1];
	    b3 = buf[2];
	    test = (b2 & 0xc0) << 8 | (b3 & 0xc0);
	    match = 0x8080;
	    wc = b1 << 12 | (b2 & 0x3f) << 6 | (b3 & 0x3f);
	    zeros = (b1 & 0x0f /* 0b0000_1111 */) << 8
		| (b2 & 0x20 /* 0b0010_0000 */);

	} else if (len == 4) {
	    b2 = buf[1];
	    b3 = buf[2];
	    b4 = buf[3];
	    test = (b2 & 0xc0) << 16 | (b3 & 0xc0) << 8 | (b4 & 0xc0);
	    match = 0x808080;
	    wc = b1 << 18 | (b2 & 0x3f) << 12 | (b3 & 0x3f) << 6 | (b4 & 0x3f);
	    zeros = (b1 & 0x07 /* 0b0000_0111 */) << 8
		| (b2 & 0x30 /* 0b0011_0000 */);

	} else return on_err ?: XO_UTF8_ERR_BAD_LEN;

	if (test != match)
	    return on_err ?: XO_UTF8_ERR_TRAILING;
	if (zeros == 0)
	    return on_err ?: XO_UTF8_ERR_NON_SHORT;
	if (wc >= 0xd800 && wc <= 0xdfff)
	    return on_err ?: XO_UTF8_ERR_SURROGATE;
	if (wc > 0x10ffff)
	    return on_err ?: XO_UTF8_ERR_RANGE;
    }

    return wc;
}

/**
 * Return a text message describing the error in 'wc'
 */
const char *
xo_utf8_wchar_errmsg (xo_codepoint_t wc)
{
    switch (wc) {
    case XO_UTF8_ERR_BAD_LEN:
	return "incorrect length bits in first byte";
    case XO_UTF8_ERR_TRAILING:
	return "incorrect high bits in secondary bytes";
    case XO_UTF8_ERR_NON_SHORT:
	return "representation is not the shortest possible form";
    case XO_UTF8_ERR_TRUNCATED:
	return "missing trailing bytes (truncated input)";
    case XO_UTF8_ERR_SECONDARY:
	return "secondary byte seen; missing first byte";
    case XO_UTF8_ERR_SURROGATE:
	return "surrogate codepoint is not valid in UTF-8";
    case XO_UTF8_ERR_RANGE:
	return "codepoint is beyond U+10FFFF";
    default:
	return "unknown error";
    }
}
