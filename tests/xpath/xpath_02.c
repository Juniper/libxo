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
    int i;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    for (i = 1; argv[i] && argv[i][0] == '-'; i++) {
	if (xo_streq(argv[i], "--debug"))
	    xo_set_flags(NULL, XOF_DEBUG);
	else if (xo_streq(argv[i], "--yydebug"))
	    xo_xpath_yydebug = 1;
    }

    for (; argv[i]; i++) {
	xo_xparse_data_t xd;

	xo_xparse_init(&xd);

	strncpy(xd.xd_filename, "test", sizeof(xd.xd_filename));
	xd.xd_buf = strdup(argv[i]);
	xd.xd_len = strlen(xd.xd_buf);

	int rc = xo_xpath_yyparse(&xd);
	printf("rc = %d\n", rc);

	int debug = xo_isset_flags(NULL, XOF_DEBUG);
	xo_set_flags(NULL, XOF_DEBUG);
	xo_xparse_dump(&xd);

	int bad_horse[] = { C_DESCENDANT, 0 };

	xo_xpath_feature_warn("test", &xd, bad_horse, "+");
	if (!debug)
	    xo_clear_flags(NULL, XOF_DEBUG);

	xo_xparse_clean(&xd);
    }

    return 0;
}
