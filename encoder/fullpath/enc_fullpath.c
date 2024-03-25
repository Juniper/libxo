/*
 * Copyright (c) 2015-2024, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, March 2024
 */

#include <unistd.h>
#include <string.h>

#include "xo.h"
#include "xo_encoder.h"

typedef struct fullpath_private_s {
    xo_buffer_t fp_data;
    xo_buffer_t fp_leader;
    xo_off_t *fp_stack;
    xo_off_t *fp_stackp;
    uint32_t fp_stack_size;
} fullpath_private_t;

#define XO_FP_DEFAULT_STACK_SIZE 16

/*
 * Create the private data for this handle, initialize it, and record
 * the pointer in the handle.
 */
static int
fullpath_create (xo_handle_t *xop)
{
    fullpath_private_t *fpp = xo_realloc(NULL, sizeof(*fpp));
    if (fpp == NULL)
	return -1;

    bzero(fpp, sizeof(*fpp));
    xo_buf_init(&fpp->fp_data);
    xo_buf_init(&fpp->fp_leader);

    xo_off_t *sp = xo_realloc(NULL, XO_FP_DEFAULT_STACK_SIZE * sizeof(*sp));
    fpp->fp_stackp = fpp->fp_stack = sp;
    if (sp)
	fpp->fp_stack_size = XO_FP_DEFAULT_STACK_SIZE;

    xo_set_private(xop, fpp);

    return 0;
}

static void
fullpath_stack_push (fullpath_private_t *fpp, xo_off_t off)
{
    if (fpp->fp_stackp - fpp->fp_stack >= fpp->fp_stack_size) {
	uint32_t new_size = fpp->fp_stack_size * 2;
	xo_off_t *sp = xo_realloc(fpp->fp_stack, new_size * sizeof(*sp));
	if (sp == NULL)
	    return;
	fpp->fp_stackp = fpp->fp_stackp - fpp->fp_stackp + sp;
	fpp->fp_stack = sp;
	fpp->fp_stack_size = new_size;
    }

    xo_dbg(NULL, "fullpath_stack_push: pushing %u (%u)",
	   off, fpp->fp_stackp - fpp->fp_stack);

    *fpp->fp_stackp++ = off;
}

static xo_off_t
fullpath_stack_pop (fullpath_private_t *fpp)
{
    xo_off_t off = 0;

    if (fpp->fp_stackp != fpp->fp_stack)
	off = *--fpp->fp_stackp;

    xo_dbg(NULL, "fullpath_stack_pop: popping %u (%u)",
	   off, fpp->fp_stackp - fpp->fp_stack);

    return off;
}

/*
 * Clean up and release any data in use by this handle
 */
static void
fullpath_destroy (xo_handle_t *xop UNUSED, fullpath_private_t *fpp)
{
    /* Clean up */
    xo_buf_cleanup(&fpp->fp_data);
    xo_buf_cleanup(&fpp->fp_leader);
}

static int
fullpath_handler (XO_ENCODER_HANDLER_ARGS)
{
    int rc = 0;
    fullpath_private_t *fpp = private;

    xo_dbg(xop, "fullpath: op %s: [%s] [%s]",
	   xo_encoder_op_name(op), name ?: "", value ?: "");
    fflush(stdout);

    /* If we don't have private data, we're sunk */
    if (fpp == NULL && op != XO_OP_CREATE)
	return -1;

    xo_buffer_t *fp_xbp = fpp ? &fpp->fp_data : NULL; /* Our internal buf */
    xo_buffer_t *leader = fpp ? &fpp->fp_leader : NULL; /* Leading string */
    xo_buffer_t *xbp = bufp ?: fp_xbp;		      /* The whiteboard buf */

    if (leader)
	xo_dbg(xop, "fullpath (enter) op %s: '%s' leader '%s'",
	       xo_encoder_op_name(op), name ?: "", leader->xb_bufp ?: "");

    switch (op) {
    case XO_OP_CREATE:		/* Called when the handle is init'd */
	rc = fullpath_create(xop);
	break;

    case XO_OP_OPTIONS:
	break;

    case XO_OP_OPTIONS_PLUS:
	break;

    case XO_OP_OPEN_LIST:
    case XO_OP_CLOSE_LIST:
    case XO_OP_OPEN_LEAF_LIST:
    case XO_OP_CLOSE_LEAF_LIST:
	break;				/* Ignore these ops */

    case XO_OP_OPEN_CONTAINER:
    case XO_OP_OPEN_INSTANCE:
	fullpath_stack_push(fpp, xo_buf_len(leader));
	xo_buf_append_str(leader, name);
	xo_buf_append_val(leader, "/", 1);
	xo_buf_force_nul(leader);
	break;

    case XO_OP_CLOSE_CONTAINER:
    case XO_OP_CLOSE_INSTANCE:
	xo_buf_set_len(leader, fullpath_stack_pop(fpp));
	xo_buf_force_nul(leader);
	xo_dbg(xop, "fullpath: new leader '%s'", leader->xb_bufp);

#if 0
	xo_buf_trim(leader, 1);	/* Trim trailing '/' */

	char *base = xo_buf_data(leader, 0);
	xo_ssize_t len = xo_buf_len(leader);
	char *str = xo_memrchr(base, '/', len);

	if (str)
	    len -= (str - base) + 1;
	xo_buf_trim(leader, len);
#endif
	break;

    case XO_OP_STRING:		   /* Quoted UTF-8 string */
    case XO_OP_CONTENT:		   /* Other content */
	if (flags & XFF_DISPLAY_ONLY)
	    break;

	if (flags & XFF_KEY) {	 /* Keys turn into predicates */
	    xo_buf_trim(leader, 1); /* Trim trailing '/' */
	    xo_buf_append_val(leader, "[", 1);
	    xo_buf_append_str(leader, name);
	    xo_buf_append_str(leader, " == '");
	    xo_buf_append_str(leader, value);
	    xo_buf_append_str(leader, "']/");
	    xo_buf_force_nul(leader);
	    break;
	}

	/* Non-keys are values */
	xo_buf_append_buf(xbp, leader); /* Start with our leading string */
	xo_buf_append_str(xbp, name);
	xo_buf_append_str(xbp, " = '");
	xo_buf_append_str(xbp, value);
	xo_buf_append_str(xbp, "'\n");
#if 0
	break;
#else
__attribute__((__fallthrough__));
#endif

    case XO_OP_FLUSH:		   /* Clean up function */
	rc = write(1, fp_xbp->xb_bufp, fp_xbp->xb_curp - fp_xbp->xb_bufp);
	if (rc > 0)
	    rc = 0;

	xo_buf_reset(fp_xbp);
	break;

    case XO_OP_FINISH:		   /* Clean up function */
	break;

    case XO_OP_DESTROY:		   /* Clean up function */
	fullpath_destroy(xop, fpp);
	break;

    case XO_OP_ATTRIBUTE:	   /* Attribute name/value */
	break;

    case XO_OP_VERSION:		/* Version string */
	break;

    case XO_OP_DEADEND:
#if 0
	xo_buf_set_len(leader, fullpath_stack_pop(fpp));
	xo_buf_force_nul(leader);
	xo_dbg(NULL, "fullpath: new leader '%s'", leader->xb_bufp);
#endif
	break;

    }

    if (leader)
	xo_dbg(xop, "fullpath (exit) op %s: '%s' leader '%s'",
	       xo_encoder_op_name(op), name ?: "", leader->xb_bufp ?: "");

    return rc;
}

static int
fullpath_wb_marker (XO_WHITEBOARD_FUNC_ARGS)
{
    printf("marker %s\n", xo_whiteboard_op_name(op));

    return 0;
}

int
xo_encoder_library_init (XO_ENCODER_INIT_ARGS)
{
    arg->xei_version = XO_ENCODER_VERSION;
    arg->xei_handler = fullpath_handler;
    arg->xei_wb_marker = fullpath_wb_marker;

    return 0;
}
