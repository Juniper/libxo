/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxo.h"

xo_info_t info[] = {
    { "in-stock", "number", "Number of items in stock" },
    { "name", "string", "Name of the item" },
    { "on-order", "number", "Number of items on order" },
    { "sku", "string", "Stock Keeping Unit" },
    { "sold", "number", "Number of items sold" },
};
int info_count = (sizeof(info) / sizeof(info[0]));

int
main (int argc, char **argv)
{
    struct employee {
	const char *e_first;
	const char *e_last;
    } employees[] = {
	{ "Terry", "Jones" },
	{ "Leslie", "Patterson" },
	{ "Ashley", "Smith" },
	{ NULL, NULL }
    }, *ep = employees;

    xo_open_container("employees");
    xo_open_list("employee");

    for ( ; ep->e_first; ep++) {
	xo_open_instance("employee");
	xo_emit("{:first-name} {:last-name}\n", ep->e_first, ep->e_last);
	xo_close_instance("employee");
    }

    xo_close_list("employee");
    xo_close_container("employees");

    return 0;
}

	
