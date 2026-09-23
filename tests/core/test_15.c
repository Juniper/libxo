/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * test_15.c: exercise "--libxo group", which inserts the locale's
 * thousands separator into decimal integer fields.  run-tests.sh always
 * exports LC_ALL=en_US.UTF-8, so libxo's own locale-init (xo_init_handle())
 * picks up a grouping locale without this test calling setlocale() itself;
 * see test_15.fmts for the format specs that actually turn "group" on
 * -- the default TEST_FORMATS runs this test with grouping off, which
 * should leave every field here untouched.
 */

#include <stdlib.h>
#include <stdint.h>

#include "xo.h"

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_open_container("data");

    xo_emit("plain int {:count/%d}\n", 1234567);
    xo_emit("unsigned {:count/%u}\n", 7654321u);
    xo_emit("negative {:count/%d}\n", -1234567);
    xo_emit("precision {:count/%.10d}\n", 42);
    xo_emit("long {:count/%ld}\n", 1234567890L);
    xo_emit("long long {:count/%lld}\n", 123456789012345LL);
    xo_emit("star width {:count/%*d}\n", 20, 1234567);
    xo_emit("bit-width 64 {:count/%!64d}\n", (int64_t) 9876543210LL);
    xo_emit("small {:count/%d}\n", 42);
    xo_emit("zero {:count/%d}\n", 0);
    xo_emit("field forced, short {i:count/%d}\n", 3456789);
    xo_emit("field forced, long {,int-group:count/%d}\n", 4567890);
    xo_emit("hex unaffected {:count/%x}\n", 0xABCDEF);
    xo_emit("octal unaffected {:count/%o}\n", 012345670);

    xo_close_container("data");

    xo_finish();

    return 0;
}
