/*
 * Copyright (c) 2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/param.h>

#include <libxo/xo.h>
#include <libxo/xo_encoder.h>
#include <libxo/xo_buf.h>

#include "xo_xpath.tab.h"
#include "xo_xparse.h"

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    for (argc = 1; argv[argc] && argv[argc][0] == '-'; argc++) {
	if (xo_streq(argv[argc], "--debug"))
	    xo_set_flags(NULL, XOF_DEBUG);
	else if (xo_streq(argv[argc], "--yydebug"))
	    xo_xpath_yydebug = 1;
    }

    if (argc == 0)
	return 2;

    xo_xparse_data_t xd;
    xo_xparse_node_t *xnp UNUSED;

    xo_xparse_init(&xd);

    strncpy(xd.xd_filename, "test", sizeof(xd.xd_filename));
    xd.xd_buf = strdup(argv[1]);
    xd.xd_len = strlen(xd.xd_buf);

    for (;;) {
	int rc = xo_xpath_yyparse(&xd);
	if (rc <= 0)
	    break;
	break;
    }

    xo_xparse_dump(&xd);

    int bad_horse[] = { C_DESCENDANT, 0 };

    xo_xpath_feature_warn("test", &xd, bad_horse, "+");

    xo_xparse_clean(&xd);

    return 0;
}
