/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that LICENSE.
 *
 * test_18.c: verify that a precompiled cache is ignored when either
 * version does not match.  The cache is built from the real format,
 * then truncated to its first field, so output shows which path ran:
 * the cache emits only "one", while the runtime parse emits "one" and
 * "two".
 *
 * Phil: when accepting the baseline, verify that "cached" and
 * "precompiled" show only "one", and that "layout" and "target" show
 * both "one" and "two".
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "xo.h"
#include "xo_encoder.h"
#include "xo_format.h"  /* xo_parse_format, xo_parse_release, xo_parse_t */

static const char fmt[] = "{:one/%d}{:two/%d}";

/*
 * Parse fmt and build a cache holding only its first field, followed
 * by the zeroed terminator entry that xo_do_emit_fields() requires.
 * The parsed fspecs are returned via fspecsp, since the field's
 * xfi_fspecs points into them.
 */
static xo_format_cache_t *
make_short_cache (xo_field_info_t **fieldsp, xo_fspec_t **fspecsp)
{
    xo_parse_t xpp = { 0 };

    *fieldsp = NULL;
    *fspecsp = NULL;

    if (xo_parse_format(&xpp, fmt) < 0 || xpp.xp_num_fields < 2) {
	fprintf(stderr, "make_short_cache: parse failed for: %s\n", fmt);
	xo_parse_release(&xpp);
	return NULL;
    }

    xo_format_cache_t *fcp = malloc(sizeof(*fcp));
    xo_field_info_t *fields = calloc(2, sizeof(*fields));
    if (fcp == NULL || fields == NULL) {
	free(fcp);
	free(fields);
	xo_parse_release(&xpp);
	return NULL;
    }

    fields[0] = xpp.xp_fields[0];	/* fields[1] stays zeroed */

    fcp->xfc_version = XO_EMIT_CACHE_VERSION;
    fcp->xfc_num_fields = 1;
    fcp->xfc_fields = fields;

    *fieldsp = fields;
    *fspecsp = xpp.xp_fspecs;
    xpp.xp_fspecs = NULL;	/* Keep them; fields[0] points into them */
    xo_parse_release(&xpp);

    return fcp;
}

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_field_info_t *fields;
    xo_fspec_t *fspecs;
    xo_format_cache_t *fcp = make_short_cache(&fields, &fspecs);
    if (fcp == NULL)
	return 1;

    xo_open_container("top");

    /* Both versions match: the cache is used */
    xo_open_container("cached");
    xo_emit_cached(fcp, fmt, 1, 2);
    xo_emit("\n");
    xo_close_container("cached");

    xo_open_container("precompiled");
    xo_emit_precompiled(XO_PRECOMPILED_VERSION, fcp, fmt, 1, 2);
    xo_emit("\n");
    xo_close_container("precompiled");

    /* The target version is wrong: the cache is ignored */
    xo_open_container("target");
    xo_emit_precompiled(XO_PRECOMPILED_VERSION + 1, fcp, fmt, 1, 2);
    xo_emit("\n");
    xo_close_container("target");

    /* The layout version is wrong: the cache is ignored */
    fcp->xfc_version = XO_EMIT_CACHE_VERSION + 1;
    xo_open_container("layout");
    xo_emit_cached(fcp, fmt, 1, 2);
    xo_emit("\n");
    xo_close_container("layout");

    xo_close_container("top");

    xo_finish();

    free(fcp);
    free(fields);
    free(fspecs);

    return 0;
}
