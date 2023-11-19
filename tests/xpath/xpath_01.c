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

    if (argc == 0)
	return 2;

    xo_xparse_data_t xd;
    xo_xparse_node_id_t id;
    xo_xparse_node_t *xnp;

    xo_xparse_init(&xd);

    strncpy(xd.xd_filename, "me", sizeof(xd.xd_filename));

    if (argv[1] == NULL)
	exit(1);

    xd.xd_buf = strdup(argv[1]);
    xd.xd_len = strlen(xd.xd_buf);

    for (;;) {
	int rc = xo_xpath_yylex(&xd, &id);
	if (rc <= 0)
	    break;

	xnp = xo_xparse_node(&xd, id);
	if (xnp) {
	    xo_dbg(NULL, "parse: type: %u (%p), str: %lu (%p), "
		   "left: %lu (%p), right %lu (%p)",
		   xnp->xn_type,
		   xnp->xn_str, xo_xparse_str(&xd, xnp->xn_str),
		   xnp->xn_contents, xo_xparse_node(&xd, xnp->xn_contents),
		   xnp->xn_next, xo_xparse_node(&xd, xnp->xn_next));
	}
    }

    return 0;
}
