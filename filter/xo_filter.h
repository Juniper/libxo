/*
 * Copyright (c) 2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef XO_FILTER_H
#define XO_FILTER_H

#define XO_FILTER_OPS_VERSION 1	/* Current API version number */

#define XO_FILTER_MISS	1	/* Missing information, might work later */
#define XO_FILTER_FAIL	2	/* Test failed; will never succeed */

struct xo_xparse_data_s;

/*
 * We treat xo_filter_t structure as opaque in the core of libxo, so we
 * just need the opaque definition here.
 */
struct xo_filter_s;
typedef struct xo_filter_s xo_filter_t;

/* Tracking status: how closely are we watching filtering? */
typedef uint32_t xo_filter_status_t;

/* Value for xo_filter_status_t */
#define XO_STATUS_FULL	1	/* Fully open: let's make some output */
#define XO_STATUS_TRACK	2	/* Track open/close/key paths, but no data */
#define XO_STATUS_DEAD	3	/* Nope, it's dead under this hierarchy */

#define XO_FILTER_DEFAULT_ARGS xop, xfp
#define XO_FILTER_DEFAULT_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED

#define XO_FILTER_DEFAULT_TAG_ARGS xop, xfp, tag
#define XO_FILTER_DEFAULT_TAG_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	const char *tag UNUSED

#include "xo_filter_ops.h"

void
xo_set_filter_data (xo_handle_t *xop UNUSED, xo_filter_t *);

struct xo_filter_s *
xo_get_filter_data (xo_handle_t *xop, int create);

xo_filter_t *
xo_filter_create (xo_handle_t *xop);

struct xo_xparse_data_s *
xo_filter_xparse_data (xo_handle_t *xop, xo_filter_t *xfp);

#define XO_FILTER_INIT_RETURN_TYPE int
#define XO_FILTER_INIT_SIGNATURE int version UNUSED, xo_filter_ops_t *ops UNUSED

typedef XO_FILTER_INIT_RETURN_TYPE
    (*xo_filter_init_func_t)(XO_FILTER_INIT_SIGNATURE);

#define XO_FILTER_INIT_FUNC "xo_filter_init"

XO_FILTER_INIT_RETURN_TYPE
xo_filter_init (XO_FILTER_INIT_SIGNATURE);

void
xo_setup_filter_lib_test (int version, xo_filter_ops_t *ops);

void
xo_filter_setup_test (void);

#endif /* XO_FILTER_H */
