/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2014-2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * precompile_01.c: round-trip test for the xo_precompile IR pass.
 *
 * This file is compiled twice by the test Makefile:
 *   - plain:  no IR pass, calls xo_emit() directly
 *   - pass:   -fpass-plugin=xo_precompile.so rewrites calls to xo_emit_cached()
 *
 * Both binaries must produce identical output for every output format.
 * It also serves as the IR inspection target: the pass-compiled .ll should
 * contain .xo_fcache / .xo_fields globals and calls to xo_emit_cached.
 */

#include <libxo/xo.h>

static void
emit_fields (void)
{
    /* plain string value */
    xo_emit("{:name/%s}\n", "alice");

    /* integer with explicit format */
    xo_emit("{:count/%d}\n", 42);

    /* two fields in one format string */
    xo_emit("{:first/%s} {:last/%s}\n", "john", "doe");

    /* encode-only field paired with display field */
    xo_emit("{e:id/%d}{:label/%s}\n", 7, "item");

    /* anchor pair with width from va_arg */
    xo_emit("{[:/%d}{:value/%s}{]:}\n", 20, "padded");
}

int
main (int argc, char *argv[])
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    xo_open_container("data");
    emit_fields();
    xo_close_container("data");
    xo_finish();
    return 0;
}
