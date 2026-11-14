/*
 * Copyright 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * The encoding styles use "." as a float's decimal point, whatever
 * the locale's is.  This sets LC_NUMERIC to a locale with a ","
 * decimal point (after xo_no_setlocale, so libxo leaves it alone)
 * and emits floats as encode-only fields, so every style's output is
 * the same in any locale.  On a host with none of these locales, the
 * test runs in the C locale and passes trivially.
 *
 * The cbor encoder reads the values back with strtod, which expects
 * the locale's decimal point, so Ecbor checks that it still encodes
 * them as floats.
 */

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include "xo.h"

static const char *comma_locales[] = {
    "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8",
    "nl_NL.UTF-8", "nl_NL.utf8", NULL
};

int
main (int argc, char **argv)
{
    const char **lp;

    xo_no_setlocale();

    for (lp = comma_locales; *lp; lp++)
	if (setlocale(LC_NUMERIC, *lp) != NULL)
	    break;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_open_container("top");
    xo_open_container("floats");

    xo_emit("{L:Floats}\n");
    xo_emit("{e:float/%.2f}{e:negative/%.1f}{e:exp/%e}{e:general/%g}",
	    1234567.5, -3.5, 1234567.5, 0.5);
    xo_emit("{e:width/%10.3f}{e:hex/%a}{e:count/%d}",
	    3.14159, 1.5, 42);

    xo_close_container("floats");
    xo_close_container("top");
    xo_finish();

    return 0;
}
