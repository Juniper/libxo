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

#define LIBXO_NEED_FILTER

#include "xo_xpath.tab.h"
#include "xo_xparse.h"
#include "xo_filter.h"

int
main (int argc, char **argv)
{
    const char *output = "output.temp";

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    for (argc = 1; argv[argc] && argv[argc][0] == '-'; argc++) {
	if (xo_streq(argv[argc], "--debug"))
	    xo_set_flags(NULL, XOF_DEBUG);
	else if (xo_streq(argv[argc], "--yydebug"))
	    xo_xpath_yydebug = 1;
    }

    if (argv[2])
	output = argv[2];

    FILE *fp = fopen(output, "w+");
    if (fp == NULL)
	xo_errx(1, "open failed");

    xo_handle_t *xop = xo_create_to_file(fp, XO_STYLE_XML, XOF_PRETTY);
    if (xop == NULL)
	xo_errx(1, "create failed");

    xo_filter_t *xfp = xo_filter_create(xop);
    if (xfp == NULL)
	xo_errx(1, "allocation of filter failed");

    xo_xparse_data_t *xdp = xo_filter_data(xop, xfp);

    strncpy(xdp->xd_filename, "test", sizeof(xdp->xd_filename));
    xdp->xd_buf = strdup(argv[1]);
    xdp->xd_len = strlen(xdp->xd_buf);

    xo_set_flags(NULL, XOF_DEBUG);

    for (;;) {
	int rc = xo_xpath_yyparse(xdp);
	if (rc <= 0)
	    break;
	break;
    }
    
    xo_xparse_dump(xdp);


    xo_filter_open_container(xop, xfp, "one");
    xo_filter_open_container(xop, xfp, "two");
    xo_filter_open_container(xop, xfp, "three");
    xo_filter_open_container(xop, xfp, "four");

    xo_emit_h(xop, "{:success}\n", "yes!");

    xo_filter_close_container(xop, xfp, "four");
    xo_filter_close_container(xop, xfp, "three");
    xo_filter_close_container(xop, xfp, "two");
    xo_filter_close_container(xop, xfp, "one");

    xo_finish_h(xop);

    xo_xparse_clean(xdp);

    return 0;
}
