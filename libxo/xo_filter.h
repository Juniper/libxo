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

#define XO_FILTER_MISS	1	/* Missing information, might work later */
#define XO_FILTER_FAIL	2	/* Test failed; will never succeed */

struct xo_xparse_data_s;

/*
 * We treat xo_filter_t structure as opaque in the core of libxo, so we
 * just need the opaque definition here.
 */
struct xo_filter_s;
typedef struct xo_filter_s xo_filter_t;

#ifdef LIBXO_NEED_FILTER

int
xo_filter_blocking (xo_handle_t *xop, xo_filter_t *);

int
xo_filter_add_one (xo_handle_t *, const char *vp);

int
xo_filter_cleanup (xo_handle_t *xop, xo_filter_t *);

int
xo_filter_open_container (xo_handle_t *xop, xo_filter_t *, const char *tag);

int
xo_filter_open_instance (xo_handle_t *xop, xo_filter_t *, const char *tag);

int
xo_filter_open_field (xo_handle_t *xop, xo_filter_t *,
		      const char *tag, ssize_t  tlen);

int
xo_filter_key (xo_handle_t *xop, xo_filter_t *,
	       const char *tag, xo_ssize_t tlen,
	       const char *value, xo_ssize_t vlen);

int
xo_filter_close_field (xo_handle_t *xop, xo_filter_t *,
		      const char *tag, ssize_t  tlen);

int
xo_filter_close_instance (xo_handle_t *xop, xo_filter_t *, const char *tag);

int
xo_filter_close_container (xo_handle_t *xop, xo_filter_t *, const char *tag);

int
xo_filter_whiteboard (XO_ENCODER_HANDLER_ARGS, xo_encoder_func_t func,
		      struct xo_filter_s *xfp);

#else /* LIBXO_NEED_FILTER */

static inline int
xo_filter_blocking (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED)
{
    return 0;
}

static inline int
xo_filter_add_one (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		   const char *vp UNUSED)
{
    return 0;
}

static inline int
xo_filter_cleanup (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED)
{
    return 0;
}

static inline int
xo_filter_open_container (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
			  const char *tag UNUSED)
{
    return 0;
}

static inline int
xo_filter_open_instance (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
			 const char *tag UNUSED)
{
    return 0;
}

static inline int
xo_filter_key (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, 
		      const char *tag UNUSED, xo_ssize_t tlen UNUSED,
		      const char *value UNUSED, xo_ssize_t vlen UNUSED)
{
    return 0;
}

static inline int
xo_filter_close_instance (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
			  const char *tag UNUSED)
{
    return 0;
}

static inline int
xo_filter_close_container (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
			   const char *tag UNUSED)
{
    return 0;
}

#endif /* LIBXO_NEED_FILTER */

void
xo_filter_data_set (xo_handle_t *xop UNUSED, xo_filter_t *);

struct xo_filter_s *
xo_filter_data_get (xo_handle_t *xop, int create);

xo_filter_t *
xo_filter_create (xo_handle_t *xop);

struct xo_xparse_data_s *
xo_filter_data (xo_handle_t *xop, xo_filter_t *xfp);

void
xo_filter_destroy (xo_handle_t *xop, xo_filter_t *xfp);

int
xo_filter_key_done (xo_handle_t *xop, xo_filter_t *xfp);

int
xo_filter_allow (xo_handle_t *xop, xo_filter_t *xfp);

/* Responses from xo_filter_allow: */
#define XO_ALLOW_YES	1	/* Yes, let's make some output */
#define XO_ALLOW_NO	2	/* No, not now, but maybe later */
#define XO_ALLOW_DEAD	3	/* Nope, it's dead under this hierarchy */

int
xo_filter_dead (xo_handle_t *xop, xo_filter_t *xfp);

#endif /* XO_FILTER_H */
