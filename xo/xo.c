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
#include <alloca.h>

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
formatter (xo_handle_t *xop UNUSED, const char *fmt)
{
    return strdup(fmt);
}

int
main (int argc UNUSED, char **argv)
{
    char *fmt = NULL, *cp;

    for (argc = 1; argv[argc]; argc++) {
	cp = argv[argc];

	if (*cp != '-')
	    break;

	if (streq(cp, "--help")) {
	    print_help();
	    return 1;

	} else if (streq(cp, "--version")) {
	    print_version();
	    return 0;
	}
    }

    fmt = argv[argc++];

    if (fmt == NULL)
	return 0;

    xo_set_formatter(NULL, formatter);

    return 0;
}
