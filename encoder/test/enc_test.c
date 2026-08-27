/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2015, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, August 2015
 */

#include "xo.h"
#include "xo_encoder.h"

static void
test_cleanup (char *buf, const char *value)
{
    char *bp;
    const char *cp;

    for (bp = buf, cp = value; cp && *cp; cp++, bp++) {
	unsigned char ch = *cp;
	if (ch >= 0x20 || ch == '\r' || ch == '\n' || ch == '\t')
	    *bp = *cp;
	else
	    *bp = ' ';
    }

    *bp = '\0';
}

static int
test_handler (XO_ENCODER_HANDLER_ARGS)
{
    flags &= ~XOF_UTF8; /* Skip this flag, since it depends on terminal */

    int len = value ? strlen(value) + 1 : 1;
    char *clean = alloca(len);
    test_cleanup(clean, value);

    printf("op %s: [%s] [%s] [%#llx]\n", xo_encoder_op_name(op),
	   name ?: "", clean, (unsigned long long) flags);

    return 0;
}

static int
test_wb_marker (XO_WHITEBOARD_FUNC_ARGS)
{
    printf("marker %s\n", xo_whiteboard_op_name(op));

    return 0;
}

int
xo_encoder_library_init (XO_ENCODER_INIT_ARGS)
{
    arg->xei_version = XO_ENCODER_VERSION;
    arg->xei_handler = test_handler;
    arg->xei_wb_marker = test_wb_marker;

    return 0;
}
