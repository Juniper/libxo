/*
 * Copyright (c) 2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, Sept 2023
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

#define LIBXO_NEED_FILTER
#include "xo.h"
#include "xo_private.h"
#include "xo_buf.h"

#include "xo_xpath.tab.h"
#include "xo_xparse.h"


#define XO_MATCHES_DEF	32	/* Number of states allocated by default */
#define XO_PATHS_DEF	32	/* Number of paths allocated by default */

typedef struct xo_match_s {
    xo_xparse_node_id_t xm_base; /* Start node of this path */
    xo_xparse_node_id_t xm_next; /* Next node we are looking to match */
    uint32_t xm_depth;	         /* Number of open containers past match */
    uint32_t xm_flags;	         /* Flags for this match instance (XMF_*) */
} xo_match_t;

#define xm_next_free xm_depth	/* Reuse field when free */

struct xo_filter_s {		   /* Forward/typdef decl in xo_private.h */
    struct xo_xparse_data_s xf_xd; /* Main parsing structure */
    uint32_t xf_allow;		   /* Number of successful matches */

    xo_xparse_node_id_t *xf_paths;  /* Root of each parse tree */
    uint32_t xf_paths_cur;	/* Current depth of xf_paths[] */
    uint32_t xf_paths_max;	/* Max depth of xf_paths[] */

    xo_match_t *xf_matches;	/* Current states */
    uint32_t xf_matches_cur;	/* Current depth of xf_paths[] */
    uint32_t xf_matches_max;	/* Max depth of xf_paths[] */
    uint32_t xf_matches_free;	/* List of free matches */

    unsigned xf_flags;		/* Flags (XFSF_*) */
};

/* Flags for xf_flags */
#define XFSF_BLOCK	(1<<0)	/* Block emitting data */

/*
 * Return a new match struct, allocating a new one if needed
 */
static xo_match_t *
xo_filter_match_new (xo_filter_t *xfp)
{
    xo_match_t *xmp;

    if (xfp->xf_matches_free) {
	xmp = &xfp->xf_matches[xfp->xf_matches_free];
	xfp->xf_matches_free = xmp->xm_next_free;
	return xmp;
    }

    if (xfp->xf_matches_cur >= xfp->xf_matches_max) {
	uint32_t new_max = xfp->xf_matches_max + XO_MATCHES_DEF;

	xmp = xo_realloc(xfp->xf_matches, new_max * sizeof(*xmp));
	if (xmp == NULL)
	    return NULL;

	xfp->xf_matches = xmp;
	xfp->xf_matches_max = new_max;
    }

    xmp = &xfp->xf_matches[xfp->xf_matches_cur++];
    bzero(xmp, sizeof(*xmp));

    return xmp;
}

/*
 * Free a match, adding it to the free list
 */
static void UNUSED
xo_filter_match_free (xo_filter_t *xfp, uint32_t match)
{
    if (match > xfp->xf_matches_cur)
	return;

    xo_match_t *xmp = &xfp->xf_matches[match];
    bzero(xmp, sizeof(*xmp));

    xmp->xm_next_free = xfp->xf_matches_free;
    xfp->xf_matches_free = match;
}

/*
 * Return a new match struct, allocating a new one if needed
 */
static void UNUSED
xo_filter_path_add (xo_filter_t *xfp, xo_xparse_node_id_t id)
{
    xo_xparse_node_id_t *pp;

    if (xfp->xf_paths_cur >= xfp->xf_paths_max) {
	uint32_t new_max = xfp->xf_paths_max + XO_PATHS_DEF;

	pp = xo_realloc(xfp->xf_paths, new_max * sizeof(*pp));
	if (pp == NULL)
	    return;

	xfp->xf_paths = pp;
	xfp->xf_paths_max = new_max;
    }

    xfp->xf_paths[xfp->xf_paths_cur++] = id;
}

int
xo_filter_blocking (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED)
{
    return 0;
}

int
xo_filter_add_one (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, const char *vp UNUSED)
{
    return 0;
}

int
xo_filter_cleanup (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED)
{
    return 0;
}

int
xo_filter_open_container (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, const char *tag UNUSED)
{
    if (xfp == NULL)
	return 0;

    /*
     * Whiffle thru the states to see if we have any open paths.  We
     * do this first since we'll be pushing new paths.
     */
    uint32_t cur = xfp->xf_matches_cur;
    uint32_t i;
    xo_xparse_data_t *xdp = &xfp->xf_xd;
    xo_match_t *xmp = xfp->xf_matches;
    xo_xparse_node_t *xnp;

    for (i = 0; i < cur; i++, xmp++) {	/* For each active match */
	if (xmp->xm_next == 0) {	/* Already matched */
	    xmp->xm_depth += 1;
	    continue;
	}

	xnp = xo_xparse_node(xdp, xmp->xm_next);
	if (xnp->xn_type != C_ELEMENT) /* Only thing we handle right now */
	    continue;

	const char *str = xo_xparse_str(xdp, xnp->xn_str);
	if (str == NULL || !xo_streq(str, tag))
	    continue;

	/* A succesful match */
	xo_dbg(NULL, "filter: open container: progress match '%s' [%u/%u]",
	       tag, xmp->xm_base, xnp->xn_next);

	xmp->xm_next = xnp->xn_next;
	if (xnp->xn_next == 0) {
	    xfp->xf_allow += 1;
	}
    }

    xo_xparse_node_id_t *paths = xfp->xf_paths;
    cur = xfp->xf_paths_cur;
    for (i = 0; i < cur; i++, paths++) {
	xnp = xo_xparse_node(xdp, *paths);
	if (xnp == NULL)
	    continue;

	if (xnp->xn_type != C_ELEMENT) /* Only type supported */
	    continue;

	const char *str = xo_xparse_str(xdp, xnp->xn_str);
	if (str == NULL || !xo_streq(str, tag))
	    continue;

	/* A succesful match! Grab a new match struct and fill it in  */
	xmp = xo_filter_match_new(xfp);
	if (xmp == NULL)
	    continue;

	xo_dbg(NULL, "filter: open container: new match '%s' [%u/%u]",
	       tag, *paths, xnp->xn_next);

	xmp->xm_base = *paths;
	xmp->xm_next = xnp->xn_next;
	if (xnp->xn_next == 0) {
	    xfp->xf_allow += 1;
	}
    }

    return 0;
}

int
xo_filter_open_instance (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, const char *tag UNUSED)
{
    return 0;
}

int
xo_filter_key (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		      const char *tag UNUSED, xo_ssize_t tlen UNUSED,
		      const char *value UNUSED, xo_ssize_t vlen UNUSED)
{
    return 0;
}

int
xo_filter_close_instance (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, const char *tag UNUSED)
{
    return 0;
}

int
xo_filter_close_container (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, const char *tag UNUSED)
{
    return 0;
}

#ifdef TEST_FILTER

int
main (int argc, char **argv)
{
    const char *output = "/dev/stdout";

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        return 1;

    for (argc = 1; argv[argc] && argv[argc][0] == '-'; argc++) {
	if (xo_streq(argv[argc], "--debug"))
	    xo_set_flags(NULL, XOF_DEBUG);
	else if (xo_streq(argv[argc], "--yydebug"))
	    xo_xpath_yydebug = 1;
    }

    xo_xparse_node_t *xnp UNUSED;

    xo_filter_t xf;
    bzero(&xf, sizeof(xf));

    xo_xparse_init(&xf.xf_xd);

    strncpy(xf.xf_xd.xd_filename, "test", sizeof(xf.xf_xd.xd_filename));
    xf.xf_xd.xd_buf = strdup(argv[1]);
    xf.xf_xd.xd_len = strlen(xf.xf_xd.xd_buf);

    xo_set_flags(NULL, XOF_DEBUG);

    for (;;) {
	int rc = xo_xpath_yyparse(&xf.xf_xd);
	if (rc <= 0)
	    break;
	break;
    }

    xo_filter_path_add(&xf, xf.xf_xd.xd_result);

    xo_xparse_dump(&xf.xf_xd, xf.xf_xd.xd_result);

    if (argv[2])
	output = argv[2];

    FILE *fp = fopen(output, "w+");
    if (fp == NULL)
	xo_errx(1, "open failed");

    xo_handle_t *xop = xo_create_to_file(fp, XO_STYLE_XML, XOF_PRETTY);
    if (xop == NULL)
	xo_errx(1, "create failed");

    xo_set_flags(xop, XOF_DEBUG);

    xo_filter_open_container(xop, &xf, "one");
    xo_filter_open_container(xop, &xf, "two");
    xo_filter_open_container(xop, &xf, "three");
    xo_filter_open_container(xop, &xf, "four");

    xo_emit_h(xop, "{:success}\n", "yes!");

    xo_filter_close_container(xop, &xf, "four");
    xo_filter_close_container(xop, &xf, "three");
    xo_filter_close_container(xop, &xf, "two");
    xo_filter_close_container(xop, &xf, "one");

    xo_finish_h(xop);

    xo_xparse_clean(&xf.xf_xd);

    return 0;
}

#endif /* TEST_FILTER */
