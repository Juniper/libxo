/*	$NetBSD: humanize_number.c,v 1.8 2004/07/27 01:56:24 enami Exp $	*/

/*
 * SPDX-License-Identifier: BSD-4-Clause
 * Copyright (c) 1997, 1998, 1999, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center, by Luke Mewburn and by Tomas Svensson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the NetBSD
 *      Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif /* HAVE_INTTYPES_H */

/* humanize_number(3) */
#define HN_DECIMAL		0x01
#define HN_NOSPACE		0x02
#define HN_B			0x04
#define HN_DIVISOR_1000		0x08
#define HN_IEC_PREFIXES         0x10

#define HN_GETSCALE		0x10
#define HN_AUTOSCALE		0x20


static int
xo_humanize_number (char *buf, size_t len, int64_t quotient,
		    const char *suffix, int scale, int flags)
{
	const int maxscale = 6;

	const char *prefixes, *sep;
	const char *sign;
	int	i, r, remainder, s1, s2;
	int	divisordeccut;
	int64_t	divisor, max;
	uint64_t magnitude, rounded;
	size_t	baselen;

	/* validate args */
	if (buf == NULL || suffix == NULL)
		return -1;

	/* Since so many callers don't check -1, NUL terminate the buffer */
	if (len > 0)
		buf[0] = '\0';

	if (scale < 0)
		return -1;
	else if (scale > maxscale &&
	    ((scale & ~(HN_AUTOSCALE|HN_GETSCALE)) != 0))
		return -1;
	if ((flags & HN_DIVISOR_1000) && (flags & HN_IEC_PREFIXES))
		return -1;

	/* setup parameters */
	remainder = 0;

	if (flags & HN_IEC_PREFIXES) {
		baselen = 2;
		/*
		 * Use the prefixes for power of two recommended by
		 * the International Electrotechnical Commission
		 * (IEC) in IEC 80000-3 (i.e. Ki, Mi, Gi...).
		 *
		 * HN_IEC_PREFIXES implies a divisor of 1024 here
		 * (use of HN_DIVISOR_1000 would have triggered
		 * an assertion earlier).
		 */
		divisor = 1024;
		divisordeccut = 973;	/* ceil(.95 * 1024) */
		if (flags & HN_B)
			prefixes = "B\0\0Ki\0Mi\0Gi\0Ti\0Pi\0Ei";
		else
			prefixes = "\0\0\0Ki\0Mi\0Gi\0Ti\0Pi\0Ei";
	} else {
		baselen = 1;
		if (flags & HN_DIVISOR_1000) {
			divisor = 1000;
			divisordeccut = 950;
			if (flags & HN_B)
				prefixes = "B\0\0k\0\0M\0\0G\0\0T\0\0P\0\0E";
			else
				prefixes = "\0\0\0k\0\0M\0\0G\0\0T\0\0P\0\0E";
		} else {
			divisor = 1024;
			divisordeccut = 973;	/* ceil(.95 * 1024) */
			if (flags & HN_B)
				prefixes = "B\0\0K\0\0M\0\0G\0\0T\0\0P\0\0E";
			else
				prefixes = "\0\0\0K\0\0M\0\0G\0\0T\0\0P\0\0E";
		}
	}

#define	SCALE2PREFIX(scale)	(&prefixes[(scale) * 3])

	/*
	 * The magnitude is kept unsigned, since the negation of
	 * INT64_MIN does not fit in an int64_t, and the sign is kept
	 * as a string, so it is not lost when the scaled value is zero
	 * before the decimal point ("-0.5K").
	 */
	if (quotient < 0) {
		sign = "-";
		magnitude = -(uint64_t) quotient;
		baselen += 2;		/* sign, digit */
	} else {
		sign = "";
		magnitude = quotient;
		baselen += 1;		/* digit */
	}
	if (flags & HN_NOSPACE)
		sep = "";
	else {
		sep = " ";
		baselen++;
	}
	baselen += strlen(suffix);

	/* Check if enough room for `x y' + suffix + `\0' */
	if (len < baselen + 1)
		return -1;

	if (scale & (HN_AUTOSCALE | HN_GETSCALE)) {
		/* See if there is additional columns can be used. */
		for (max = 1, i = len - baselen; i-- > 0;)
			max *= 10;

		/*
		 * Divide the number until it fits the given column.
		 * If there will be an overflow by the rounding below,
		 * divide once more.
		 */
		for (i = 0;
		    (magnitude >= (uint64_t) max
		    || (magnitude == (uint64_t) max - 1 &&
		    (remainder >= divisordeccut || remainder >=
		    divisor / 2))) && i < maxscale; i++) {
			remainder = magnitude % divisor;
			magnitude /= divisor;
		}

		if (scale & HN_GETSCALE)
			return i;
	} else {
		for (i = 0; i < scale && i < maxscale; i++) {
			remainder = magnitude % divisor;
			magnitude /= divisor;
		}
	}

	/* If a value <= 9.9 after rounding and ... */
	/*
	 * XXX - should we make sure there is enough space for the decimal
	 * place and if not, don't do HN_DECIMAL?
	 */
	if (((magnitude == 9 && remainder < divisordeccut) || magnitude < 9) &&
	    i > 0 && flags & HN_DECIMAL) {
		s1 = (int)magnitude + ((remainder * 10 + divisor / 2) /
		    divisor / 10);
		s2 = ((remainder * 10 + divisor / 2) / divisor) % 10;
		r = snprintf(buf, len, "%s%d%s%d%s%s%s",
		    (s1 || s2) ? sign : "", s1,
		    localeconv()->decimal_point, s2,
		    sep, SCALE2PREFIX(i), suffix);
	} else {
		rounded = magnitude + (remainder + divisor / 2) / divisor;
		r = snprintf(buf, len, "%s%" XO_PRIu64 "%s%s%s",
		    rounded ? sign : "", rounded,
		    sep, SCALE2PREFIX(i), suffix);
	}

	return r;
}
