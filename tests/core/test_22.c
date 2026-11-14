/*
 * Copyright 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * A handle made by xo_create_to_file(stderr) flushes at each newline,
 * as stderr does, so its output appears even though the program ends
 * with _exit(), which skips both atexit and stdio's flushing.  The
 * default handle is finished explicitly first.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "xo.h"

int
main (int argc, char **argv)
{
    xo_handle_t *xop;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_open_container("top");
    xo_emit("{:stdout/%s}\n", "before");
    xo_close_container("top");
    xo_finish();
    fflush(stdout);

    xop = xo_create_to_file(stderr, XO_STYLE_TEXT, 0);
    if (xop == NULL)
	return 1;

    xo_emit_h(xop, "{L:Lines}: {:lines/%d}\n", 42);
    xo_emit_h(xop, "{L:Words}: {:words/%d}\n", 314);

    _exit(0);
}
