/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that LICENSE.
 *
 * test_cached.c: verify xo_emit_cached() produces identical output to
 * xo_emit().  Two structural blocks are emitted:
 *   "cached"   — uses xo_emit_cached() with a valid xo_format_cache_t
 *   "fallback" — uses xo_emit_cached() with a NULL cache (falls back to
 *                xo_do_emit, identical to xo_emit)
 *
 * Phil: when accepting the baseline, verify that the "cached" and
 * "fallback" blocks are byte-for-byte identical in every output format.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "xo.h"
#include "xo_encoder.h"
#include "xo_format.h"  /* xo_parse_format, xo_parse_release, xo_parse_t */

/*
 * Parse fmt into a heap-allocated xo_format_cache_t.  The returned
 * fspecsp is the backing store that the cache's xfi_fspecs pointers
 * point into; the caller owns both and must pass them to free_cache().
 */
static xo_format_cache_t *
make_cache (const char *fmt, xo_fspec_t **fspecsp)
{
    xo_parse_t xpp = { 0 };

    *fspecsp = NULL;

    if (xo_parse_format(&xpp, fmt) < 0) {
	fprintf(stderr, "make_cache: parse failed for: %s\n", fmt);
	return NULL;
    }

    xo_format_cache_t *fcp = malloc(sizeof(*fcp));
    if (fcp == NULL) {
	xo_parse_release(&xpp);
	return NULL;
    }

    fcp->xfc_version = XO_EMIT_CACHE_VERSION;
    fcp->xfc_num_fields = xpp.xp_num_fields;
    fcp->xfc_fields = (const struct xo_field_info_s *) xpp.xp_fields;
    *fspecsp = xpp.xp_fspecs;

    /*
     * Transfer ownership of both arrays; xfi_fspecs pointers in
     * xp_fields point into xp_fspecs, so both must survive the release
     * or die together, not one while the other lives.
     */
    xpp.xp_fields = NULL;
    xpp.xp_fspecs = NULL;
    xo_parse_release(&xpp);

    return fcp;
}

static void
free_cache (xo_format_cache_t *fcp, xo_fspec_t *fspecs)
{
    if (fcp) {
	free((void *)(uintptr_t) fcp->xfc_fields);
	free(fcp);
    }
    free(fspecs);
}

/*
 * Test format strings.  Cover the main xo_field_info_t code paths:
 *   fmt1: plain %s format string
 *   fmt2: default format (no explicit format → XO_FOFF_DEFAULT → "%s")
 *   fmt3: label/text role (content in format string; no va_arg)
 *   fmt4: two integer fields
 *   fmt5: encode-only field + regular field (xfi_encoding path)
 *   fmt6: anchor fields (xfi_format width via va_arg)
 */
static const char fmt1[] = "{:name/%s}\n";
static const char fmt2[] = "{:name}\n";
static const char fmt3[] = "{L:static label}\n";
static const char fmt4[] = "{:count/%d} of {:total/%d}\n";
static const char fmt5[] = "{e:enc/%s}{:vis/%s}\n";
static const char fmt6[] = "{[:/%d}{:addr/%p}..{:port/%u}{]:}\n";

static void
emit_all (xo_format_cache_t *c1, xo_format_cache_t *c2,
	  xo_format_cache_t *c3, xo_format_cache_t *c4,
	  xo_format_cache_t *c5, xo_format_cache_t *c6)
{
    xo_emit_cached(c1, fmt1, "hello");
    xo_emit_cached(c2, fmt2, "world");
    xo_emit_cached(c3, fmt3);
    xo_emit_cached(c4, fmt4, 3, 10);
    xo_emit_cached(c5, fmt5, "enc-val", "vis-val");
    xo_emit_cached(c6, fmt6, 16, NULL, 80);
}

int
main (int argc, char **argv)
{
    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_fspec_t *s1, *s2, *s3, *s4, *s5, *s6;
    xo_format_cache_t *c1 = make_cache(fmt1, &s1);
    xo_format_cache_t *c2 = make_cache(fmt2, &s2);
    xo_format_cache_t *c3 = make_cache(fmt3, &s3);
    xo_format_cache_t *c4 = make_cache(fmt4, &s4);
    xo_format_cache_t *c5 = make_cache(fmt5, &s5);
    xo_format_cache_t *c6 = make_cache(fmt6, &s6);

    /* Block 1: valid cache — exercises the cached code path */
    xo_open_container("cached");
    emit_all(c1, c2, c3, c4, c5, c6);
    xo_close_container("cached");

    /* Block 2: NULL cache — falls back to xo_do_emit; must match block 1 */
    xo_open_container("fallback");
    emit_all(NULL, NULL, NULL, NULL, NULL, NULL);
    xo_close_container("fallback");

    xo_finish();

    free_cache(c1, s1);
    free_cache(c2, s2);
    free_cache(c3, s3);
    free_cache(c4, s4);
    free_cache(c5, s5);
    free_cache(c6, s6);

    return 0;
}
