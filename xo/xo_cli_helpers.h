/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2014-2019, 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * Command-line helpers shared by the "xo" and "xo-logger" commands:
 * combining an xo_emit-style format string with the remaining command
 * line arguments, one value at a time, via a custom xo_emit formatter.
 *
 * This is a header of "static inline" functions (and the file-scope
 * statics they close over) rather than a normal library source file,
 * so that each translation unit that includes it gets its own private
 * copy with no linkage conflicts, without hand-duplicating the code.
 *
 * Include after "xo.h" (and after <stdarg.h>, which xo.h itself pulls
 * in indirectly via callers that need va_list for xo_emit_hv, etc.).
 */

#ifndef XO_CLI_HELPERS_H
#define XO_CLI_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include "xo_utf8.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif /* UNUSED */

static int opt_warn;		/* Enable warnings */

static char **save_argv;
static char **checkpoint_argv;

static inline char *
next_arg (void)
{
    char *cp = *save_argv;

    if (cp == NULL)
	xo_errx(1, "missing argument");

    save_argv += 1;
    return cp;
}

static inline char *
get_arg (char *arg, const char *msg)
{
    if (arg == NULL)
        xo_errx(1, "missing arg: %s", msg);

    return arg;
}

static inline void
prep_arg (char *fmt)
{
    char *cp, *fp;

    for (cp = fp = fmt; *cp; cp++, fp++) {
	if (*cp != '\\') {
	    if (cp != fp)
		*fp = *cp;
	    continue;
	}

	/* A trailing backslash has nothing to escape; keep it as is */
	if (cp[1] == '\0') {
	    *fp = '\\';
	    continue;
	}

	switch (*++cp) {
	case 'n':
	    *fp = '\n';
	    break;

	case 'r':
	    *fp = '\r';
	    break;

	case 'b':
	    *fp = '\b';
	    break;

	case 'e':
	    *fp = '\e';
	    break;

	default:
	    *fp = *cp;
	}
    }

    *fp = '\0';
}

static inline void
checkpoint (xo_handle_t *xop UNUSED, va_list vap UNUSED, int restore)
{
    if (restore)
	save_argv = checkpoint_argv;
    else
	checkpoint_argv = save_argv;
}

/*
 * Rewrite the single conversion in "fmt" (ending at "convp") into "dst"
 * so that it matches the argument we really pass.  Our values come from
 * command line strings, which we convert to the widest type for each
 * family (long long, unsigned long long, double, char *), so the user's
 * length modifiers are dropped and replaced by "mods", and the
 * conversion character becomes "conv".  Trailing text is copied as is.
 * "dst" needs room for strlen(fmt) + 3 bytes.
 */
static inline void
fmt_rewrite (char *dst, const char *fmt, const char *convp,
	     const char *mods, char conv)
{
    const char *cp;

    for (cp = fmt; cp < convp; cp++)
	if (strchr("hljtzqL", *cp) == NULL)
	    *dst++ = *cp;

    while (*mods)
	*dst++ = *mods++;

    *dst++ = conv;
    strcpy(dst, convp + 1);
}

/*
 * Our custom formatter is responsible for combining format string pieces
 * with our command line arguments to build strings.  This involves faking
 * some printf-style logic.
 */
static inline xo_ssize_t
formatter (xo_handle_t *xop, char *buf, xo_ssize_t bufsiz,
	   const char *fmt, va_list vap UNUSED)
{
    /*
     * printf-style length modifiers.  Only "h" matters, since we pass
     * the widest type and fmt_rewrite() drops the rest.
     */
    int lflag UNUSED = 0, hflag = 0, jflag UNUSED = 0,
	tflag UNUSED = 0, zflag UNUSED = 0, qflag UNUSED = 0;
    int star1 = 0, star2 = 0;
    int rc = 0;
    int w1 = 0, w2 = 0;
    const char *cp;

    for (cp = fmt + 1; *cp; cp++) {
	if (*cp == 'l')
	    lflag += 1;
	else if (*cp == 'h')
	    hflag += 1;
	else if (*cp == 'j')
	    jflag += 1;
	else if (*cp == 't')
	    tflag += 1;
	else if (*cp == 'z')
	    zflag += 1;
	else if (*cp == 'q')
	    qflag += 1;
	else if (*cp == '*') {
	    if (star1 == 0)
		star1 = 1;
	    else
		star2 = 1;
	} else if (strchr("diouxXDOUeEfFgGaAcCsSp", *cp) != NULL)
	    break;
	else if (*cp == 'n' || *cp == 'v') {
	    if (opt_warn)
		xo_error_h(xop, "unsupported format: '%s'", fmt);
	    return -1;
	}
    }

    char fc = *cp;
    if (fc == '\0')		/* No conversion character */
	return 0;

    /* Handle "%*.*s" */
    if (star1)
	w1 = strtol(next_arg(), NULL, 0);
    if (star2)
	w2 = strtol(next_arg(), NULL, 0);

    char nfmt[strlen(fmt) + 3];	/* See fmt_rewrite() */

    if (strchr("diD", fc) != NULL) {
	long long value = strtoll(next_arg(), NULL, 0);

	/* "h" and "hh" truncate; we do it here since we pass a long long */
	if (hflag == 1)
	    value = (short) value;
	else if (hflag > 1)
	    value = (signed char) value;

	fmt_rewrite(nfmt, fmt, cp, "ll", (fc == 'D') ? 'd' : fc);
	fmt = nfmt;

	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (strchr("ouxXOU", fc) != NULL) {
	unsigned long long value = strtoull(next_arg(), NULL, 0);

	if (hflag == 1)
	    value = (unsigned short) value;
	else if (hflag > 1)
	    value = (unsigned char) value;

	fmt_rewrite(nfmt, fmt, cp, "ll",
		    (fc == 'O') ? 'o' : (fc == 'U') ? 'u' : fc);
	fmt = nfmt;

	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (fc == 'p') {
	void *value = (void *) (uintptr_t) strtoull(next_arg(), NULL, 0);

	fmt_rewrite(nfmt, fmt, cp, "", 'p');
	fmt = nfmt;

	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (strchr("eEfFgGaA", fc) != NULL) {
	double value = strtod(next_arg(), NULL);

	fmt_rewrite(nfmt, fmt, cp, "", fc); /* Drops "L" (long double) */
	fmt = nfmt;

	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (fc == 'C' || fc == 'c' || fc == 'S' || fc == 's') {
	/*
	 * Our arguments are UTF-8 strings, never an int, wint_t or
	 * wchar_t *, so every one of these becomes "%s".  For %c/%C we
	 * pass just the first (possibly multi-byte) character.
	 */
	char *value = next_arg();
	char cbuf[5];

	if (fc == 'c' || fc == 'C') {
	    size_t len = xo_utf8_len(value[0]);
	    size_t slen = strnlen(value, len);

	    memcpy(cbuf, value, slen);
	    cbuf[slen] = '\0';
	    value = cbuf;
	}

	fmt_rewrite(nfmt, fmt, cp, "", 's');
	fmt = nfmt;

	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);
    }

    return rc;
}

#endif /* XO_CLI_HELPERS_H */
