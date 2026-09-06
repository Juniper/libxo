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
 * Our custom formatter is responsible for combining format string pieces
 * with our command line arguments to build strings.  This involves faking
 * some printf-style logic.
 */
static inline xo_ssize_t
formatter (xo_handle_t *xop, char *buf, xo_ssize_t bufsiz,
	   const char *fmt, va_list vap UNUSED)
{
    /* printf-style formatting flags, currently ignored */
    int lflag UNUSED = 0, hflag UNUSED = 0, jflag UNUSED = 0,
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

    /* Handle "%*.*s" */
    if (star1)
	w1 = strtol(next_arg(), NULL, 0);
    if (star2 > 1)
	w2 = strtol(next_arg(), NULL, 0);

    if (fc == 'D' || fc == 'O' || fc == 'U')
	lflag = 1;

    if (strchr("diD", fc) != NULL) {
	long long value = strtoll(next_arg(), NULL, 0);
	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (strchr("ouxXOUp", fc) != NULL) {
	unsigned long long value = strtoull(next_arg(), NULL, 0);
	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (strchr("eEfFgGaA", fc) != NULL) {
	double value = strtold(next_arg(), NULL);
	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (fc == 'C' || fc == 'c' || fc == 'S' || fc == 's') {
	char *value = next_arg();
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
