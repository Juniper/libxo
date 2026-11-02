/*
 * Copyright 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Exercises the RTOON encoder's "dense" option (dense, dense=auto,
 * dense=N).  The program itself never changes; test_19.fmts runs it
 * with each setting.  Each case lives in its own top-level container:
 *
 *   a: a flat list (auto makes it dense)
 *   b: a list whose first instance has a nested container (auto leaves
 *      it sparse; dense=1 flattens it)
 *   c: a list holding a nested list, with a third level under the
 *      second inner instance (dense=1 drops the inner lists; dense=2
 *      makes the inner lists dense and drops the third level)
 *   d: a depth-1 list using the "dense" field modifier (always dense)
 *   e: a list made dense by XOF_DENSE whose second instance opens a
 *      nested list (always the loud xo_failure() message)
 *   f: a flat list whose second instance opens a nested list (in auto
 *      mode, the single "discarded" warning)
 *   g: a list whose first instance has no fields (auto leaves it
 *      sparse)
 *
 * Case f comes before c, so in auto mode the one warning names f's
 * list and c's drop is silent.
 */

#include <stdio.h>
#include "xo.h"

int
main (int argc, char **argv)
{
    int i, j;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    xo_open_container("dense-test");

    /* a: flat list, with a leaf-list and a string that looks typed */
    xo_open_container("a-flat");
    xo_open_list("a-disk");
    for (i = 0; i < 3; i += 1) {
	xo_open_instance("a-disk");
	xo_emit("{k:name}{:size/%d}{:note}{l:tag}{l:tag}\n",
		i == 0 ? "sda" : i == 1 ? "sdb" : "sdc", (i + 1) * 100,
		i == 1 ? "true" : "ok", "x", i == 2 ? "z,w" : "y");
	xo_close_instance("a-disk");
    }
    xo_close_list("a-disk");
    xo_close_container("a-flat");

    /* b: nested container in the first instance */
    xo_open_container("b-nested-container");
    xo_open_list("b-route");
    for (i = 0; i < 2; i += 1) {
	xo_open_instance("b-route");
	xo_emit("{k:dest}{:proto}\n",
		i == 0 ? "10.0.0.0/24" : "10.0.1.0/24",
		i == 0 ? "static" : "direct");
	xo_open_container("via");
	xo_emit("{:interface}{:gateway}\n",
		i == 0 ? "eth0" : "eth1", i == 0 ? "10.0.0.1" : "10.0.1.1");
	xo_close_container("via");
	xo_close_instance("b-route");
    }
    xo_close_list("b-route");
    xo_close_container("b-nested-container");

    /* f: flat first instance, nested list in the second */
    xo_open_container("f-late-nested");
    xo_open_list("f-user");
    xo_open_instance("f-user");
    xo_emit("{k:name}{:uid/%d}\n", "root", 0);
    xo_close_instance("f-user");
    xo_open_instance("f-user");
    xo_emit("{k:name}{:uid/%d}\n", "phil", 1001);
    xo_open_list("f-group");
    xo_open_instance("f-group");
    xo_emit("{k:gname}\n", "wheel");
    xo_close_instance("f-group");
    xo_close_list("f-group");
    xo_close_instance("f-user");
    xo_close_list("f-user");
    xo_close_container("f-late-nested");

    /* c: list of lists, with a third level under one inner instance */
    xo_open_container("c-nested-list");
    xo_open_list("c-host");
    for (i = 0; i < 3; i += 1) {
	xo_open_instance("c-host");
	xo_emit("{k:host}{:state}\n",
		i == 0 ? "alpha" : i == 1 ? "beta" : "gamma",
		i == 1 ? "down" : "up");
	xo_open_list("c-port");
	for (j = 0; j < 2; j += 1) {
	    xo_open_instance("c-port");
	    xo_emit("{k:port/%d}{:proto}\n", 20 + j + i * 100,
		    j == 0 ? "tcp" : "udp");
	    if (i == 0 && j == 1) {
		xo_open_list("c-conn");
		xo_open_instance("c-conn");
		xo_emit("{k:peer}\n", "10.9.9.9");
		xo_close_instance("c-conn");
		xo_close_list("c-conn");
	    }
	    xo_close_instance("c-port");
	}
	xo_close_list("c-port");
	xo_close_instance("c-host");
    }
    xo_close_list("c-host");
    xo_close_container("c-nested-list");

    /* d: the "dense" field modifier at depth 1 */
    xo_open_container("d-modifier");
    xo_open_list("d-cpu");
    for (i = 0; i < 3; i += 1) {
	xo_open_instance("d-cpu");
	xo_emit("{k:num/%d}{,dense:load/%d}\n", i, i * 7);
	xo_close_instance("d-cpu");
    }
    xo_close_list("d-cpu");
    xo_close_container("d-modifier");

    /* e: XOF_DENSE, then a nested list in the second instance */
    xo_open_container("e-xof-dense");
    xo_open_list_hf(NULL, XOF_DENSE, "e-item");
    xo_open_instance("e-item");
    xo_emit("{k:sku}{:qty/%d}\n", "A1", 2);
    xo_close_instance("e-item");
    xo_open_instance("e-item");
    xo_emit("{k:sku}{:qty/%d}\n", "B2", 1);
    xo_open_list("e-part");
    xo_open_instance("e-part");
    xo_emit("{k:part}\n", "bolt");
    xo_close_instance("e-part");
    xo_close_list("e-part");
    xo_close_instance("e-item");
    xo_close_list("e-item");
    xo_close_container("e-xof-dense");

    /* g: first instance has no fields */
    xo_open_container("g-empty-first");
    xo_open_list("g-entry");
    xo_open_instance("g-entry");
    xo_close_instance("g-entry");
    xo_open_instance("g-entry");
    xo_emit("{k:name}\n", "second");
    xo_close_instance("g-entry");
    xo_close_list("g-entry");
    xo_close_container("g-empty-first");

    xo_close_container("dense-test");

    xo_finish();
    return 0;
}
