/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * precompile_04.c: round-trip test for "B" (boolean) fields through the
 * xo_precompile IR pass.
 *
 * The precompiled cache holds the display format's fspecs but not the
 * boolean pair info, which xo_format_boolean() recomputes from them.
 * The encoding format isn't cached at all.  This file is compiled with
 * and without the pass, and both binaries must produce identical output
 * for every output format.  Each B field is followed by a "next" field
 * so a field that pops the wrong arguments shows up as a diff.
 */

#include <libxo/xo.h>

static void
emit_booleans (int t)
{
    const char *str = t ? "yes" : NULL;

    xo_emit("{B:str}{:str-next/%d}\n", str, 1);
    xo_emit("{Bf:power/on+off%d}{:power-next/%d}\n", t, 2);
    xo_emit("{B:escaped/a\\+b+100%%%u}{:escaped-next/%d}\n",
            (unsigned) t, 3);
    xo_emit("{B:arg/*%*d}{:arg-next/%d}\n", "si+no", 4, t, 4);
    xo_emit("{B,full-boolean:full/T+F%d/1+0%d}{:full-next/%d}\n", t, 5);
    xo_emit("{Bn,full-boolean:full-n/T+F%d/1+0%d}{:full-n-next/%d}\n",
            t, 6);
    xo_emit("{Bq:quoted/%d}{:quoted-next/%d}\n", t, 7);
}

int
main (int argc, char *argv[])
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    xo_open_container("data");

    xo_open_container("true");
    emit_booleans(1);
    xo_close_container("true");

    xo_open_container("false");
    emit_booleans(0);
    xo_close_container("false");

    xo_close_container("data");
    xo_finish();
    return 0;
}
