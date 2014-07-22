/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "libxo.h"
#include "xoversion.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif /* UNUSED */

#ifndef HAVE_STREQ
static inline int
streq (const char *red, const char *blue)
{
    return (red && blue && *red == *blue && strcmp(red + 1, blue + 1) == 0);
}
#endif /* HAVE_STREQ */

static int opt_warn;		/* Enable warnings */

static void
print_help (void)
{
    fprintf(stderr, "Usage: xo [options] formt [fields]\n");
}

static void
print_version (void)
{
    printf("libxo version %s%s\n", LIBXO_VERSION, LIBXO_VERSION_EXTRA);
}

static char *
check_arg (const char *name, char ***argvp, int has_opt)
{
    char *opt = NULL, *arg;

    if (has_opt) {
	opt = **argvp;
	*argvp += 1;
    }
    arg = **argvp;
    if (!has_opt)
	*argvp += 1;

    if (arg == NULL) {
	if (has_opt)
	    xo_error("missing %s argument for '%s' option", name, opt);
	else
	    xo_error("missing argument for '%s'", name);
	exit(1);
    }

    return arg;
}

static char **save_argv;

static char *
next_arg (void)
{
    char *cp = *save_argv;

    if (cp == NULL) {
	xo_error(NULL, "missing argument\n");
	exit(1);
    }

    save_argv += 1;
    return cp;
}

static void
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

/*
 * Our custom formatter is responsible for combining format string pieces
 * with our command line arguments to build strings.  This involves faking
 * some printf-style logic.
 */
static int
formatter (xo_handle_t *xop, xchar_t *buf, int bufsiz,
	   const xchar_t *fmt, va_list vap UNUSED)
{
    int lflag = 0, hflag = 0, jflag = 0, tflag = 0,
	zflag = 0, qflag = 0, star1 = 0, star2 = 0;
    int rc = 0;
    int w1 = 0, w2 = 0;
    const xchar_t *cp;

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

    xchar_t fc = *cp;

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

int
main (int argc UNUSED, char **argv)
{
    char *fmt = NULL, *cp, *np, *container = NULL;
    int clen = 0;

    for (argv++; *argv; argv++) {
	cp = *argv;

	if (*cp != '-')
	    break;

	if (streq(cp, "--"))
	    break;

	if (streq(cp, "--help")) {
	    print_help();
	    return 1;

	} else if (streq(cp, "--container") || streq(cp, "-c")) {
	    container = check_arg("container", &argv, 1);

	} else if (streq(cp, "--html") || streq(cp, "-H")) {
	    xo_set_style(NULL, XO_STYLE_HTML);

	} else if (streq(cp, "--json") || streq(cp, "-J")) {
	    xo_set_style(NULL, XO_STYLE_JSON);

        } else if (streq(cp, "--pretty") || streq(cp, "-p")) {
	    xo_set_flags(NULL, XOF_PRETTY);

	} else if (streq(cp, "--style") || streq(cp, "-s")) {
	    np = check_arg("style", &argv, 1);

	    if (streq(cp, "xml"))
		xo_set_style(NULL, XO_STYLE_XML);
	    else if (streq(cp, "json"))
		xo_set_style(NULL, XO_STYLE_JSON);
	    else if (streq(cp, "text"))
		xo_set_style(NULL, XO_STYLE_TEXT);
	    else if (streq(cp, "html"))
		xo_set_style(NULL, XO_STYLE_HTML);
	    else {
		xo_error("unknown style: %s", cp);
		exit(1);
	    }

	} else if (streq(cp, "--text") || streq(cp, "-T")) {
	    xo_set_style(NULL, XO_STYLE_TEXT);

	} else if (streq(cp, "--xml") || streq(cp, "-X")) {
	    xo_set_style(NULL, XO_STYLE_XML);

	} else if (streq(cp, "--xpath")) {
	    xo_set_flags(NULL, XOF_XPATH);

	} else if (streq(cp, "--version")) {
	    print_version();
	    return 0;

	} else if (streq(cp, "--warn") || streq(cp, "-W")) {
	    opt_warn = 1;
	    xo_set_flags(NULL, XOF_WARN);
	}
    }

    fmt = check_arg("format string", &argv, 0);
    if (fmt == NULL || *fmt == '\0')
	return 0;

    prep_arg(fmt);
    xo_set_formatter(NULL, formatter);
    xo_set_flags(NULL, XOF_NO_VA_ARG);

    save_argv = argv;

    if (container) {
	clen = strlen(container);
	for (cp = container; cp && *cp; cp = np) {
	    np = strchr(cp, '/');
	    if (np)
		*np = '\0';
	    xo_open_container(cp);
	    if (np)
		*np++ = '/';
	}
    }

    xo_emit(fmt);

    while (container) {
	np = strrchr(container, '/');
	xo_close_container(np ? np + 1 : container);
	if (np)
	    *np = '\0';
	else
	    container = NULL;
    }

    xo_flush();

    return 0;
}
