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
#include <inttypes.h>
#include <string.h>
#include <sys/param.h>

#define LIBXO_NEED_FILTERS

#include "xo.h"
#include "xo_private.h"
#include "xo_buf.h"
#include "xo_encoder.h"
#include "xo_xpath.tab.h"
#include "xo_xparse.h"
#include "xo_filter.h"

typedef double xo_float_t;	/* Our floating point type */

/*
 * We extensively return aggregates (structures) in the file, under the
 * contraint that they are less than 128 bits and can be returned via
 * registers without impacting performance.  So we turn off warnings
 * for aggregate returns.
 */
#pragma GCC   diagnostic ignored "-Waggregate-return"

#define XO_MATCHES_DEF	32	/* Number of states allocated by default */

/*
 * We maintain a set of filters (xo_filter_t), representing each
 * defined XPath.  The filter holds the output of the parser, and we
 * use the set of paths (xd_paths) defined by that parse.
 *
 * We maintain a set of active matches (xo_match_t), created when we
 * find an open tag matching one of those paths.  The match holds the
 * current state of that active matching effort.
 *
 * Each match include a stack (xo_stack_t) that references each node
 * in that xpath as we match it.  So "one/two/three" would be three
 * distinct items in the stack.
 *
 * xs_match is the current node we are matching on, with xs_predicates
 * holding any predicates for that node.
 *
 * We use xs_state to track the current state of the top of the stack:
 * XSS_INIT: Initial state (zero)
 * XSS_NEED: Looking for match (on xs_match)
 *    we are looking for a node to match xs_match
 *    when we find a match, we check for predicates:
 *    if there are predicates, set xs_predicates
 *    otherwise push the next element of the path
 * XSS_PRED: Looking for predicate; xs_match is match, but has a predicate
 *    we have matched the tag and are trying to test the predicates
 * XSS_DEEP: Found or not, we go deeper in hierarchy
 *    we are at the end of the patch and allow/deny the xpath
 * XSS_DEADEND: Failed match; permanently, so we don't care about other keys
 *
 * This means that the first node on the stack will always be the
 * first node of the path, even if it's not strictly needed.
 */

typedef struct xo_stack_s {
    uint32_t xs_state;		 /* Explict state (XSS_*) */
    xo_xparse_node_id_t xs_match; /* Node that we are matching */
    xo_xparse_node_id_t xs_predicates; /* Predicate node */
    char *xs_keys;	         /* Keys stored as "key\0val\0k2\0v2\0\0"*/
    xo_ssize_t xs_keys_len; 	 /* Length of xs_keys */
    uint32_t xs_allow;		 /* Any 'allow' increment */
    uint32_t xs_deny;		 /* Any 'deny' increment */
    xo_off_t xs_offset;		 /* WB marker */
    uint32_t xs_flags;		 /* Flags (XSF_*) */
} xo_stack_t;

/*
 * Each stack element has it's own state, which is resumed when the
 * layer above it is popped.
 */
#define XSS_INIT	0	/* Initial state */
#define XSS_FIRST	1	/* Top of stack; don't really need it but... */
#define XSS_NEED	2	/* Looking for match */
#define XSS_PRED	3	/* Looking for predicate */
#define XSS_FOUND	4	/* Found a matching open */
#define XSS_DEEP	5	/* Found or not, we go deeper in hierarchy */
#define XSS_DEADEND	6	/* Dead hierarchy */

/* Flags for xs_flags */
#define XSF_DEAD	(1<<0)	/* Frame is dead */

typedef struct xo_match_s {
    struct xo_match_s *xm_next;	 /* Next match */
    xo_xparse_node_id_t xm_base; /* Start node of this path */
    uint32_t xm_depth;	         /* Number of open containers past match */
    uint32_t xm_flags;	         /* Flags for this match instance (XMF_*) */
    xo_buffer_t xm_whiteboard;	 /* Whiteboard */
    uint32_t xm_stack_size;	 /* Number of entries in the stack */
    xo_stack_t *xm_stackp;	 /* Stack pointer (xo_stack) */
    xo_stack_t xm_stack[0];	 /* Stack of nodes */
} xo_match_t;

/* Flags fpr xm_flags */
#define XMF_NOT		(1<<0)	 /* Not expression ("!a") */

struct xo_filter_s {		 /* Forward/typdef decl in xo_private.h */
    struct xo_xparse_data_s xf_xd; /* Main parsing structure */
    xo_filter_status_t xf_status; /* Current status: (see XO_STATUS_*) */
    uint32_t xf_depth;		 /* Depth of hierarchy seen (zero == top) */
    uint32_t xf_allow;		 /* Number of successful matches */
    uint32_t xf_deny;		 /* Number of successful not matches */
    xo_match_t *xf_matches;	 /* Current states */
    unsigned xf_flags;		 /* Flags (XFSF_*) */
    uint32_t xf_total_depth;	 /* Total depth ('opens' minus 'closes') */
};

/* Flags for xf_flags */
#define XFSF_BLOCK	(1<<0)	/* Block emitting data */

static void
xo_filter_dump_matches (xo_handle_t *xop, xo_filter_t *xfp);

int
xo_encoder_wb_marker (xo_handle_t *xop, xo_whiteboard_op_t op,
		      xo_buffer_t *wbp, xo_off_t *offp)
{
    xo_whiteboard_func_t func = xo_get_wb_marker(xop);

    if (func == NULL)
	return -1;

    void *private = xo_get_private(xop);
    return func(xop, op, wbp, offp, private);
}

/*
 * Create and initialize a filter, attaching it to a handle
 */
xo_filter_t *
xo_filter_create (xo_handle_t *xop)
{
    xo_filter_t *xfp = xo_realloc(NULL, sizeof(*xfp));
    if (xfp == NULL)
	return NULL;

    bzero(xfp, sizeof(*xfp));

    xo_xparse_init(&xfp->xf_xd);

    xo_filter_data_set(xop, xfp);

    return xfp;
}

/*
 * The filter code is layered on top of the xpath parsing code, but
 * sometimes we need to pull out the xparse data structure, mostly for
 * our test jigs.
 */
xo_xparse_data_t *
xo_filter_xparse_data (xo_handle_t *xop UNUSED, xo_filter_t *xfp)
{
    return &xfp->xf_xd;
}

/*
 * Completely destroy and release a filter
 */
void
xo_filter_destroy (xo_handle_t *xop, xo_filter_t *xfp)
{
    xo_xparse_clean(&xfp->xf_xd);

    if (xfp->xf_matches) {
	/* Whiffle down the match list, freeing as we go */
	xo_match_t *xmp = xfp->xf_matches, *next;
	for (; xmp; xmp = next) {
	    next = xmp->xm_next;
	    xo_free(xmp);
	}
    }

    xo_filter_data_set(xop, NULL);
    xo_free(xfp);
}

/*
 * We size our stack for the "worst case" scenario, rather than resize
 * them, calculating that size from the contents of the expression.
 */
static int
xo_stack_max (xo_handle_t *xop, xo_filter_t *xfp, xo_xparse_node_id_t id)
{
    int rc = 1;
    xo_xparse_node_t *xnp;

    for (; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);

	if (xnp->xn_type == C_PATH) {
	    rc += xo_stack_max(xop, xfp, xnp->xn_contents);

	} else if (xnp->xn_type == C_ELEMENT || xnp->xn_type == C_ABSOLUTE)
	    rc += 1;
    }

    XO_DBG(xop, "xo_stack_max: id %u -> %d", id, rc);

    return rc;
}

/*
 * Return a new match struct, allocating a new one if needed
 */
static xo_match_t *
xo_filter_match_new (xo_handle_t *xop, xo_filter_t *xfp, xo_xparse_node_id_t id)
{
    int stack_size = xo_stack_max(xop, xfp, id);
    int sz = sizeof(xo_match_t) + sizeof(xo_stack_t) * stack_size;

    xo_match_t *xmp = xo_realloc(NULL, sz);
    if (xmp == NULL)
	return NULL;

    bzero(xmp, sz);

    xmp->xm_base = id;
    xmp->xm_stackp = xmp->xm_stack;
    xmp->xm_stack_size = stack_size;

    xmp->xm_next = xfp->xf_matches;
    xfp->xf_matches = xmp;

    return xmp;
}

/*
 * When we no longer need the recorded keys, we can release them
 */
static void
xo_filter_stack_free_keys (xo_filter_t *xfp UNUSED, xo_stack_t *xsp)
{
    if (xsp->xs_keys) {
	xo_free(xsp->xs_keys);
	xsp->xs_keys = NULL;
	xsp->xs_keys_len = 0;
    }
}

/*
 * Release a "match", that is a pattern which we are currently
 * processing, typically because we've popped the top element.
 */
static void
xo_filter_match_free (xo_filter_t *xfp, xo_match_t *xmp)
{
    xo_match_t **prev, **next;
    xo_stack_t *xsp;

    for (prev = next = &xfp->xf_matches; *prev; ) {
	if (*prev == xmp) {
	    *prev = xmp->xm_next;

	    /* Release any saved key/value pairs */
	    for (xsp = xmp->xm_stack; xsp <= xmp->xm_stackp; xsp++)
		xo_filter_stack_free_keys(xfp, xsp);

	    xo_buf_cleanup(&xmp->xm_whiteboard);
	    xo_free(xmp);

	} else {
	    prev = &(*prev)->xm_next;
	}
    }
}

/*
 * Turn internal states into printable names (for debug output)
 */
static const char *
xo_filter_state_name (uint32_t state)
{
    static const char *names[] = {
        /* XSS_INIT */ "INIT",
	/* XSS_FIRST */ "FIRST",
        /* XSS_NEED */ "NEED",
        /* XSS_PRED */ "PRED",
        /* XSS_FOUND */ "FOUND",
        /* XSS_DEEP */ "DEEP",
        /* XSS_DEADEND */ "DEADEND",
    };

    if (state > sizeof(names) / sizeof(names[0]))
	return "unknown";

    return names[state];
}

/*
 * Add a filter (xpath) to our filtering mechanism
 */
int
xo_filter_add_one (xo_handle_t *xop, const char *input)
{
    xo_filter_t *xfp = xo_filter_data_get(xop, TRUE);
    if (xfp == NULL)
	return -1;

    xo_xparse_data_t *xdp = xo_filter_xparse_data(xop, xfp);

    int rc = xo_xparse_parse_string(xop, xdp, input);

    

    return rc ? -1 : 0;
}

/*
 * Indicate if all the matches are XSS_DEADEND, meaning there's no
 * point in future exporation.
 */
static int
xo_filter_all_dead (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED)
{
    int rc = FALSE;
    xo_match_t *xmp;

    /* For each active match, find one that's not dead */
    for (xmp = xfp->xf_matches; xmp; xmp = xmp->xm_next) {
	xo_stack_t *xsp = xmp->xm_stackp; /* Look at the top of the stack */

	if (xsp->xs_state != XSS_DEADEND) {
	    rc = FALSE;
	    break;		/* Short circuit: don't need to continue */
	}

	rc = TRUE;		/* Found at least one */
    }

    XO_DBG(xop, "filter: all-dead: -> %d", rc);

    return rc; /* Either zero active matches or no DEADENDS */
}

xo_filter_status_t
xo_filter_get_status (xo_handle_t *xop UNUSED, xo_filter_t *xfp)
{
    return xfp->xf_status;
}

/*
 * Turn a xo_filter_status_t into a string for debug output
 */
const char *
xo_filter_status_name (xo_filter_status_t rc)
{
    return (rc == 0) ? "zero" :
	(rc == XO_STATUS_TRACK) ? "track" :
	(rc == XO_STATUS_FULL) ? "full" :
	(rc == XO_STATUS_DEAD) ? "dead" : "unknown";
}

/*
 * Update the status field.  Called when something may have affected it.
 * The "why" variable tracks why we are in this state, for debug output,
 * and maybe it should really be part of the status, but that would mean
 * status would be two parts, and since the "why" doesn't matter past the
 * lifetime of this function, we don't hold on to it.
 *
 * This isn't a cheap activity, so calls should be limited to avoid
 * performance issues (one reason why we cache the status).
 */
static xo_filter_status_t
xo_filter_change_status (xo_handle_t *xop, xo_filter_t *xfp,
			 const char *tag UNUSED)
{
    const char *why UNUSED;
    int rc;

    /* No filters means always allow */
    if (xfp == NULL || xfp->xf_xd.xd_paths_cur == 0) {
	why = "no-filters";
	rc = XO_STATUS_FULL;

    } else if (xfp->xf_deny) {
	why = "deny-is-set";
	rc = XO_STATUS_TRACK;		/* No means no */

    } else if (xfp->xf_allow) {
	why = "allow-is-set";
	rc = XO_STATUS_FULL;

    } else if (xfp->xf_xd.xd_flags & XDF_ALL_NOTS) {
	why = "all-nots";
	rc = XO_STATUS_FULL;

    } else if (xo_filter_all_dead(xop, xfp)) {
	if ((xfp->xf_xd.xd_flags & XDF_ALL_ABS) && xfp->xf_total_depth != 1) {
	    why = "all-dead";
	    rc = XO_STATUS_DEAD;
	} else {
	    why = "dead-but-still-tracking";
	    rc = XO_STATUS_TRACK;
	}

    } else {
	why = "default-to-no";
	rc = XO_STATUS_TRACK;
    }

    XO_DBG(xop, "xo_filter_update_status (%s) returns %s/%d "
	   "why: %s (was %s/%d)",
	   tag, xo_filter_status_name(rc), rc, why,
	   xo_filter_status_name(xfp->xf_status), xfp->xf_status);

    xfp->xf_status = rc;	/* Record new value */

    return rc;
}

xo_filter_status_t
xo_filter_update_status (xo_handle_t *xop, xo_filter_t *xfp)
{
    return xo_filter_change_status(xop, xfp, "caller");
}

/*
 * Inspect the children of a node (given by `id`) to see if it
 * contains any predicates.
 */
static int
xo_filter_has_predicates (xo_filter_t *xfp, xo_xparse_node_id_t id)
{
    xo_xparse_node_t *xnp;

    for (; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	if (xnp->xn_type == C_PREDICATE)
	    return TRUE;
    }

    return FALSE;
}

/*
 * Adjust the allow/deny numbers for a new match stack element
 */
static const char *
xo_filter_match_adjust (xo_handle_t *xop, xo_filter_t *xfp, xo_match_t *xmp,
			xo_stack_t *xsp, uint32_t state)
{
    const char *label;

    xsp->xs_state = state;

    if (xmp->xm_flags & XMF_NOT) {
	xfp->xf_deny += 1;
	xsp->xs_deny += 1;
	label = " deny++";
    } else {
	xfp->xf_allow += 1;
	xsp->xs_allow += 1;
	label = " allow++";
    }

    xo_filter_update_status(xop, xfp);

    return label;
}

static xo_stack_t *
xo_filter_stack_push (xo_filter_t *xfp UNUSED, xo_match_t *xmp, uint32_t state,
		xo_xparse_node_id_t match, xo_xparse_node_id_t predicate)
{
    xo_stack_t *xsp = ++xmp->xm_stackp;

    xsp->xs_state = state;
    xsp->xs_match = match;
    xsp->xs_predicates = predicate;

    return xsp;
}

static void
xo_filter_stack_pop (xo_filter_t *xfp UNUSED, xo_match_t *xmp)
{
    xo_stack_t *xsp = xmp->xm_stackp;

    if (xsp == xmp->xm_stack)	/* Should not occur */
	return;

    xo_filter_stack_free_keys(xfp, xsp); /* Make sure the keys are released */

    bzero(xsp, sizeof(*xsp));	/* Just to be sure */

    xmp->xm_stackp -= 1;
}

/*
 * XSS_DEADEND means that we've lost the hierarchy we're looking for
 * and we can ignore everything until we come out of that hierarchy.
 */
static void
xo_filter_deadend (xo_handle_t *xop UNUSED, xo_filter_t *xfp, xo_match_t *xmp,
		   xo_stack_t *xsp, int call_op UNUSED)
{
    xsp->xs_state = XSS_DEADEND;
    xo_filter_stack_free_keys(xfp, xsp);
    xmp->xm_depth = 1;	/* This "open" counts as the first one */
}

/*
 * Whiffle thru the states to see if we have any open paths.  We
 * do this first since we'll be pushing new paths.
 */
static void
xo_filter_open_check_matches (xo_handle_t *xop, xo_filter_t *xfp,
			      xo_xparse_data_t *xdp,
			      const char *tag, ssize_t tlen,
			      const char *type UNUSED)
{
    xo_match_t *xmp = xfp->xf_matches;
    uint32_t i UNUSED;
    xo_xparse_node_t *xnp;

    for (i = 0; xmp; i++, xmp = xmp->xm_next) { /* For each active match */
	xo_stack_t *xsp = xmp->xm_stackp;

	/* Are we at the end of this match?  Or dead in the middle */
	if (xsp->xs_state == XSS_DEEP || xsp->xs_state == XSS_DEADEND) {
	    xmp->xm_depth += 1;
	    continue;
	}

	/*
	 * If we're looking to test a predicate and instead we see an
	 * open, then we're dead.
	*/
	if (xsp->xs_state == XSS_PRED) {
	    xo_filter_deadend(xop, xfp, xmp, xsp, FALSE);
	    continue;
	}

	xnp = xo_xparse_node(xdp, xsp->xs_match);

	if (xnp->xn_type != C_ELEMENT)	/* Only other type supported */
	    continue;

	const char *str = xo_xparse_str(xdp, xnp->xn_str);
	if (str == NULL || !xo_streqn(str, tag, tlen)) {
	    xo_filter_deadend(xop, xfp, xmp, xsp, FALSE);
	    continue;
	}

	const char *label UNUSED = "";

	if (xo_filter_has_predicates(xfp, xnp->xn_contents)) {
	    /*
	     * Mark these predicates as our's.  To do this we copy the
	     * old match but add our predicates
	     */
	    xsp->xs_state = XSS_PRED;

	} else if (xnp->xn_next == 0) {
	    /* We don't set xm_depth to 1 here; this "open" doesn't count */
	    label = xo_filter_match_adjust(xop, xfp, xmp, xsp, XSS_DEEP);

	} else {
	    xo_xparse_node_t *nextp = xo_xparse_node(xdp, xnp->xn_next);

	    xsp->xs_state = XSS_FOUND;
	    xsp = xo_filter_stack_push(xfp, xmp, XSS_NEED, xnp->xn_next,
				       nextp ? nextp->xn_contents : 0);
	}

	/* A succesful match */
	XO_DBG(xop, "filter: open %s: progress match [%u] '%.*s' "
	       "[match %u, next %u] [allow %u/deny %u]%s",
	       type, i, tlen, tag, xmp->xm_base, xsp->xs_match,
	       xfp->xf_allow, xfp->xf_deny, label);
    }
}

/*
 * Whiffle thru the patterns to see if we match any.  When we find one,
 * open a new "match" for it.
 */
static void
xo_filter_open_check_patterns (xo_handle_t *xop, xo_filter_t *xfp,
			       xo_xparse_data_t *xdp,
			       const char *tag, ssize_t tlen,
			       const char *type UNUSED)
{
    xo_match_t *xmp;
    uint32_t i;
    xo_xparse_node_t *xnp;

    xo_xparse_node_id_t *paths = xfp->xf_xd.xd_paths;
    uint32_t cur = xfp->xf_xd.xd_paths_cur;

    for (i = 0; i < cur; i++, paths++) {
	xo_xparse_node_id_t id = *paths;
	xnp = xo_xparse_node(xdp, id);
	if (xnp == NULL)
	    continue;

	int not = FALSE;

	switch (xnp->xn_type) {
	case C_ELEMENT:
	    /* Normal case */
	    break;

	case C_ABSOLUTE:
	    /* Absolute means that we only match at the top of the tree */
	    if (xfp->xf_total_depth != 1)
		continue;

	    /* Now move to the next node */
	    id = xnp->xn_next;
	    xnp = xo_xparse_node(xdp, id);
	    if (xnp->xn_type != C_ELEMENT)
		continue;
	    break;

	case C_NOT:
	    /* A "not" is a path that it negated */
	    not = TRUE;
	    /* fallthru */

	case C_PATH:
	    /* A path contains a set of elements to match */
	    id = xnp->xn_contents;
	    xnp = xo_xparse_node(xdp, id);
	    if (xnp->xn_type != C_ELEMENT && xnp->xn_type != C_ABSOLUTE)
		continue;
	    break;

	default:
	    continue;
	}

	const char *str = xo_xparse_str(xdp, xnp->xn_str);

	/* Look for the matching tag */
	if (str == NULL || !xo_streqn(str, tag, tlen))
	    continue;

	/* A succesful match! Grab a new match struct and fill it in  */
	xmp = xo_filter_match_new(xop, xfp, *paths);
	if (xmp == NULL)
	    continue;

	xmp->xm_base = *paths;

	/* Fill in the initial state frame */
	xo_stack_t *xsp = xmp->xm_stackp;
	xsp->xs_state = xnp->xn_contents ? XSS_PRED : XSS_FIRST;
	xsp->xs_match = id;
	xsp->xs_predicates = xnp->xn_contents;

	if (not)
	    xmp->xm_flags |= XMF_NOT;

	const char *label UNUSED = "";

	if (xo_filter_has_predicates(xfp, xnp->xn_contents)) {
	    /* The predicates are already marked as our's */

	} else if (xnp->xn_next == 0) { /* Only element */
	    label = xo_filter_match_adjust(xop, xfp, xmp, xsp, XSS_DEEP);

	} else {
	    xo_xparse_node_t *nextp = xo_xparse_node(xdp, xnp->xn_next);

	    xsp = xo_filter_stack_push(xfp, xmp, XSS_NEED, xnp->xn_next,
				       nextp ? nextp->xn_contents : 0);
	}

#if 0
	xo_encoder_wb_marker(xop, XO_WB_INIT,
			     &xmp->xm_whiteboard, &xsp->xs_offset);
#endif

	/*
	 * Initialize our whiteboard, where the encoder will write
	 * its data.
	 */
	/* Nothing to do for now.... */

	XO_DBG(xop, "filter: open %s: new match '%.*s' [%u/%u] "
	       "[state %u/%s; match %u, pred %u] "
	       "[%u/%u] %s",
	       type, tlen, tag, *paths, xnp->xn_next,
	       xsp->xs_state, xo_filter_state_name(xsp->xs_state),
	       xsp->xs_match, xsp->xs_predicates,
	       xfp->xf_allow, xfp->xf_deny, label);
    }
}

/*
 * Open a container/list/instance.  Inspects all open matches and
 * patterns to see if the new element matters and adjusts the status.
 */
static int
xo_filter_open (xo_handle_t *xop, xo_filter_t *xfp,
		const char *tag, ssize_t tlen, const char *type)
{
    if (xfp == NULL)
	return 0;

    XO_DBG(xop, "filter: open %s: '%.*s'", type, tlen, tag);

    xfp->xf_total_depth += 1;

    xo_xparse_data_t *xdp = &xfp->xf_xd;

    xo_filter_open_check_matches(xop, xfp, xdp, tag, tlen, type);
    xo_filter_open_check_patterns(xop, xfp, xdp, tag, tlen, type);

    xo_filter_change_status(xop, xfp, "open");

    xo_filter_dump_matches(xop, xfp);

    return xfp->xf_status;
}

int
xo_filter_open_container (xo_handle_t *xop, xo_filter_t *xfp,
			  const char *tag)
{
    return xo_filter_open(xop, xfp, tag, strlen(tag), "container");
}

int
xo_filter_open_instance (xo_handle_t *xop, xo_filter_t *xfp, const char *tag)
{
    return xo_filter_open(xop, xfp, tag, strlen(tag), "list");
}

int
xo_filter_open_field (xo_handle_t *xop, xo_filter_t *xfp,
		      const char *tag, ssize_t  tlen)
{
    return xo_filter_open(xop, xfp, tag, tlen, "field");
}

/*
 * Whiffle thru the states to see if we have any open paths.
 */
static void
xo_filter_close_check_matches (xo_handle_t *xop UNUSED, xo_filter_t *xfp,
			       xo_xparse_data_t *xdp,
			       const char *tag, ssize_t tlen,
			       const char *type UNUSED)
{

    /*
     * Whiffle thru the states to see if we have any open paths.
     */
    uint32_t i UNUSED;
    xo_match_t *xmp = xfp->xf_matches;
    xo_match_t *next_xmp = NULL;
    xo_xparse_node_t *xnp;

    for (i = 0; xmp; i++, xmp = next_xmp) { /* For each active match */
	next_xmp = xmp->xm_next;	    /* Pre-fetch in case we delete */

	xo_stack_t *xsp = xmp->xm_stackp;

	if (xmp->xm_depth != 0) {	/* Already seen nested tags */
	    xmp->xm_depth -= 1;

	    if (xmp->xm_depth == 0) {
		/* If we're closing the last DEADEND tag, go back to NEED */
		if (xsp->xs_state == XSS_DEADEND) {
		    xsp->xs_state = XSS_NEED;
		    xo_filter_stack_free_keys(xfp, xsp);
		}
	    }

	    continue;
	}

	/*
	 * If we're going deep, then we need a close for the top
	 * frame, otherwise we are needing that, so we look at the
	 * penultimate frame
	 */
	if (xsp->xs_state != XSS_DEEP) {
	    if (xsp == xmp->xm_stack) /* Top of stack; nothing to close */
		continue;
	    xsp -= 1;		/* Look at penultimate stack frame */
	}

	xnp = xo_xparse_node(xdp, xsp->xs_match);

	if (xnp->xn_type != C_ELEMENT)	/* Only other type supported */
	    continue;

	const char *str = xo_xparse_str(xdp, xnp->xn_str);
	if (str == NULL || !xo_streqn(str, tag, tlen))
	    continue;

	const char *label UNUSED = "";

	/*
	 * The top stack frame has the deltas to adjust the
	 * global allow/deny by.
	 */
	xfp->xf_allow -= xsp->xs_allow;
	xfp->xf_deny -= xsp->xs_deny;

	if (xsp == xmp->xm_stack) {
	    /* We're at the top of the stack, so this match can die */
	    xo_filter_match_free(xfp, xmp);
	    xmp = NULL;

	} else {
	    /*
	     * Pop a frame from the stack.  If the top is in XSS_DEEP,
	     * then we just need to set it to XSS_NEED.
	     */
	    if (xsp->xs_state == XSS_DEEP) {
		/*
		 * DEEP means we've already matched, so instead of
		 * popping the frame, we just reset it so XSS_NEED state.
		 */
		xsp->xs_state = XSS_NEED;
		xo_filter_stack_free_keys(xfp, xsp);

		/*
		 * We didn't really "top" the top frame, so we need to
		 * reset the allow/deny values
		 */
		xsp->xs_allow = xsp->xs_deny =  0;

	    } else {
		/*
		 * Pop a stack frame.  Then reset the parent to
		 * XSS_NEED state, since it might have a failed
		 * predicate (XSS_DEADEND).  Need to use xsp, since xsp
		 * might not be the top frame.
		 */
		xsp->xs_state = XSS_NEED;
		xo_filter_stack_free_keys(xfp, xsp);
		
		xo_filter_stack_pop(xfp, xmp);
	    }
	}

	/* A succesful un-match */
	XO_DBG(xop, "filter: close %s match [%u]: progress match '%.*s' "
	       "[base %u] [%u/%u]%s",
	       type, i, tlen, tag, xmp ? xmp->xm_base : 0,
	       xfp->xf_allow, xfp->xf_deny, label);
    }
}

/*
 * Open a container/list/instance.  Inspects all open matches to see
 * if the new element matters and adjusts the status.
 */
static int
xo_filter_close (xo_handle_t *xop, xo_filter_t *xfp,
		 const char *tag, ssize_t tlen, const char *type UNUSED)
{
    if (xfp == NULL)
	return 0;

    if (xfp->xf_depth > 0)
	xfp->xf_depth -= 1;		/* Track our depth */

    if (xfp->xf_total_depth > 0)
	xfp->xf_total_depth -= 1;

    XO_DBG(xop, "filter: close %s: '%.*s'", type, tlen, tag);

    xo_xparse_data_t *xdp = &xfp->xf_xd;
    xo_filter_close_check_matches(xop, xfp, xdp, tag, tlen, type);

    xo_filter_change_status(xop, xfp, "close");

    xo_filter_dump_matches(xop, xfp);

    return xfp->xf_status;
}

int
xo_filter_close_field (xo_handle_t *xop, xo_filter_t *xfp,
		      const char *tag, ssize_t  tlen)
{
    return xo_filter_close(xop, xfp, tag, tlen, "field");
}

int
xo_filter_close_instance (xo_handle_t *xop UNUSED, xo_filter_t *xfp,
			  const char *tag)
{
    return xo_filter_close(xop, xfp, tag, strlen(tag), "instance");
}

int
xo_filter_close_container (xo_handle_t *xop UNUSED, xo_filter_t *xfp,
			   const char *tag)
{
    return xo_filter_close(xop, xfp, tag, strlen(tag), "container");
}

/*
 * Add a keys for xs_keys for the match.
 *
 * We store the keys in a simple-but-slow style that might need
 * updated/optimized later.  For now, it "key\0val\0k2\0v2\0\0" So
 * names and values are NUL terminated with another NUL to end the
 * list.  The number of keys should be low (typically one) so the
 * efficiency shouldn't matter.
 */
static void
xo_filter_key_add (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		   xo_match_t *xmp,
		   const char *tag, xo_ssize_t tlen,
		   const char *value, xo_ssize_t vlen)
{
    xo_stack_t *xsp = xmp->xm_stackp;
    xo_ssize_t new_len = tlen + vlen + 3; /* Three NULs */
    char *newp = xo_realloc(xsp->xs_keys, xsp->xs_keys_len + new_len);

    if (newp == NULL)
	return;

    char *addp = newp + xsp->xs_keys_len;
    char *t UNUSED = addp;

    memcpy(addp, tag, tlen);
    addp += tlen;
    *addp++ = '\0';

    char *v UNUSED = addp;
    memcpy(addp, value, vlen);
    addp += vlen;
    *addp++ = '\0';
    *addp++ = '\0';

    xsp->xs_keys_len += new_len - 1;
    xsp->xs_keys = newp;

    XO_DBG(xop, "xo_filter_key: adding '%s' = '%s'", t, v);
}

/*
 * Find the current value of a given key and return it.  Since keys
 * can be added multiple times, we can't short circuit and return the
 * first value, we have to continue and return the _last_ value.
 */
static const char *
xo_filter_key_find (xo_filter_t *xfp UNUSED,
		    xo_match_t *xmp, const char *tag)
{
    xo_ssize_t off = 0;
    xo_stack_t *xsp = xmp->xm_stackp; /* Only look at the top of stack */
    xo_ssize_t len = xsp->xs_keys_len;
    char *cp = xsp->xs_keys;
    const char *match = NULL;

    while (off < len) {
	if (*cp == '\0')	/* SNO: sanity check */
	    break;

	xo_ssize_t klen = strlen(cp);
	if (xo_streq(tag, cp))	/* Match! */
	    match = cp + klen + 1; /* An answer, but keep looking */

	xo_ssize_t vlen = strlen(cp + klen + 1);
	xo_ssize_t tlen = klen + 1 + vlen + 1;

	off += tlen;		/* Skip over this entry */
	cp += tlen;
    }

    return match;
}

/* ------------------------------------------------------------- */

/*
 * This is the 'key' and 'predicate' processing code.
 */

/*
 * Our filter data structure.  We keep the size under 128 bits so we
 * can return it in registers and avoid messing with the stack.  XPath
 * uses JSON-like floating point:
 *
 *    A number represents a floating-point number. A number can have
 *    any double-precision 64-bit format IEEE 754 value
 *
 * but this stinks since floats lose precision, especially with 64-bit
 * numbers like counters, so we use xfdd_number for simple numbers.
 */
typedef struct xo_eval_value_s {
    unsigned xev_type:16;	/* Type (token type) */
    unsigned xev_flags:8;	/* Flags (XFDF_*) */
    unsigned xev_pad:8;		/* Padding */
    xo_xparse_node_id_t xev_node;   /* 32 bits of node */
    union {			    /* Data value (based on xev_type) */
	int64_t xevd_int64;	    /* If C_INT64 */
	uint64_t xevd_uint64;	    /* If C_UINT64 or C_INDEX or C_BOOLEAN */
	xo_float_t xevd_float;	    /* If C_FLOAT */
	const char *xevd_str;	    /* If C_STRING */
    } xev_data;
} xo_eval_value_t;

#define xev_int64 xev_data.xevd_int64
#define xev_uint64 xev_data.xevd_uint64
#define xev_float xev_data.xevd_float
#define xev_str xev_data.xevd_str

/* Flags for xev_flags: */
#define XEVF_TRUE	(1<<0) /* This part is true */
#define XEVF_INVALID	(1<<1) /* Expression hierarchy is invalid/broken */
#define XEVF_MISSING	(1<<2) /* A referenced element is missing  */
#define XEVF_UNSUPPORTED (1<<3) /* Token type is not supported */
#define XEVF_FINAL	(1<<4)  /* This is the final answer */

#define XO_EVAL_OP_ARGS \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, xo_match_t *xmp UNUSED, \
	xo_xparse_node_t *xnp UNUSED, \
	xo_eval_value_t left UNUSED, xo_eval_value_t right UNUSED

#define XO_EVAL_OP_PASS \
    xop, xfp, xmp, xnp, left, right

typedef xo_eval_value_t (xo_eval_op_fn_t)(XO_EVAL_OP_ARGS);

#define XO_EVAL_NODE_ARGS \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, xo_match_t *xmp UNUSED, \
	xo_xparse_node_t *xnp

typedef xo_eval_value_t (xo_eval_node_fn_t)(XO_EVAL_NODE_ARGS);

#define XO_EVAL_CALC_ARGS \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	xo_eval_value_t left UNUSED, xo_eval_value_t right UNUSED

typedef xo_eval_value_t (xo_eval_calc_fn_t)(XO_EVAL_CALC_ARGS);

static inline xo_eval_value_t
xo_eval_value_make (unsigned type, unsigned flags, xo_xparse_node_id_t id)
{
    xo_eval_value_t value = { 0 };

    value.xev_type = type;
    value.xev_flags = flags;
    value.xev_pad = 0;
    value.xev_node = id;
    value.xev_uint64 = 0;

    return value;
}

static inline xo_eval_value_t
xo_eval_value_float (unsigned flags, xo_float_t val)
{
    xo_eval_value_t value = { 0 };

    value.xev_type = C_FLOAT;
    value.xev_flags = flags;
    value.xev_float = val;

    return value;
}

static inline xo_eval_value_t
xo_eval_value_invalid (void)
{
    xo_eval_value_t value = { 0 };

    value.xev_type = M_ERROR;
    value.xev_flags = XEVF_INVALID;
    value.xev_pad = 0;
    value.xev_node = 0;
    value.xev_uint64 = 0;

    return value;
}

/* Forward decl */
static xo_eval_value_t
xo_eval (xo_handle_t *xop, xo_filter_t *xfp, xo_match_t *xmp,
			 xo_xparse_node_id_t id, xo_eval_op_fn_t op_fn);

static xo_eval_value_t
xo_eval_number (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = { .xev_flags = 0 };
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    char *ep;
    int64_t ival = strtoll(str, &ep, 0);
    xo_float_t fval;

    if (ep && *ep == '\0') {
	value = xo_eval_value_make(C_INT64, 0, 0);
	value.xev_int64 = ival;
    } else {
	fval = strtod(str, &ep);
	if (ep && *ep == '\0') {
	    value = xo_eval_value_make(C_FLOAT, 0, 0);
	    value.xev_float = fval;

	} else {
	    /* We can't give an error, so we just return 0 */
	    xo_failure(xop, "invalid number value: '%s'", xnp->xn_str);
	    value = xo_eval_value_make(C_INT64, 0, 0);
	    value.xev_int64 = 0;
	}
    }

    return value;
}

static xo_eval_value_t
xo_eval_quoted (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = { .xev_flags = 0 };
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);

    if (str) {
	value = xo_eval_value_make(C_STRING, 0, 0);
	value.xev_str = str;
    } else {
	value.xev_flags |= XEVF_MISSING;
    }

    return value;
}

static xo_eval_value_t
xo_eval_path (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = { .xev_flags = 0 };
    xo_xparse_node_t *elt = NULL;
    xo_xparse_node_id_t id;

    /* We only support a single element in the path, which must be a key */
    for (id = xnp->xn_contents; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	if (xnp->xn_type != C_ELEMENT) {
	    xo_failure(xop, "filter: non-element path member (%s)",
		       xo_xparse_fancy_token_name(xnp->xn_type));
	    continue;
	}

	if (elt == NULL)
	    elt = xnp;
	else return xo_eval_value_invalid();
    }

    if (elt == NULL)
	return value;

    const char *str = xo_xparse_str(&xfp->xf_xd, elt->xn_str);
    const char *sval = xo_filter_key_find(xfp, xmp, str);
    if (sval) {
	value = xo_eval_value_make(C_STRING, 0, 0);
	value.xev_str = sval;
    } else {
	value.xev_flags |= XEVF_MISSING;
    }

    return value;
}

static int64_t
xo_eval_cast_int64 (xo_filter_t *xfp UNUSED, xo_eval_value_t value)
{
    switch (value.xev_type) {
    case C_STRING:;
	const char *str = value.xev_str;
	char *ep;
	int64_t ival = strtoll(str, &ep, 0);
	return (ep && *ep == '\0') ? ival: 0;

    case C_FLOAT:
	return (int64_t) value.xev_float;

    default:
	return value.xev_int64;
    }
}

static int
xo_eval_cast_boolean (xo_filter_t *xfp UNUSED, xo_eval_value_t value)
{
    switch (value.xev_type) {
    case C_STRING:;
	const char *str = value.xev_str;
	char *ep;
	int64_t ival = strtoll(str, &ep, 0);
	return (ep == NULL || *ep != '\0') ? 0 : ival ? 1 : 0;

    case C_FLOAT:
	return (int64_t) value.xev_float != 0;

    default:
	return value.xev_int64 != 0;
    }
}

static xo_float_t
xo_eval_cast_float (xo_filter_t *xfp UNUSED, xo_eval_value_t value)
{
    xo_float_t fval = 0;

    switch (value.xev_type) {
    case C_STRING:;
	const char *str = value.xev_str;
	char *ep;
	fval = strtod(str, &ep);
	return (ep && *ep == '\0') ? fval: 0;

    case C_FLOAT:
	return value.xev_float;

    case C_BOOLEAN:
	return value.xev_int64 ? 1 : 0;

    case C_UINT64:
	return (xo_float_t) value.xev_uint64;

    case C_INT64:
    default:
	return (xo_float_t) value.xev_int64;
    }
}

static inline xo_eval_value_t
xo_eval_cast_float_value (xo_filter_t *xfp UNUSED, xo_eval_value_t old)
{
    xo_float_t new = xo_eval_cast_float(xfp, old);
    return xo_eval_value_float(0, new);
}

static void
xo_eval_dump_value (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		     xo_eval_value_t value,
		     int indent UNUSED, const char *title UNUSED)
{
    char buf[16];
    const char *bp UNUSED = buf;

    switch (value.xev_type) {

    case C_STRING:
	bp = value.xev_str;
	break;

    case C_BOOLEAN:
    case C_INT64:
	snprintf(buf, sizeof(buf), "%" PRId64, value.xev_int64);
	break;

    case C_UINT64:
	snprintf(buf, sizeof(buf), "%" PRIu64, value.xev_uint64);
	break;

    case C_FLOAT:
	snprintf(buf, sizeof(buf), "%lf", value.xev_float);
	break;

    default:
	bp = "(unknown)";
    }

    const char *type UNUSED = xo_xparse_fancy_token_name(value.xev_type);

    XO_DBG(xop, "%*s%s: type '%s' (%u), flags %#x, node %lu, val '%s'",
	   indent, "", title ?: "",
	   type, value.xev_type, value.xev_flags, value.xev_node, bp);
}

#define TYPE_CMP(_a, _b) (((_a) << 16) | (_b))

static xo_eval_value_t
xo_eval_compare (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = { 0 };
    int rc = 0;
    xo_float_t fval;

    xo_eval_dump_value(xop, xfp, left, 0, "compare: left");
    xo_eval_dump_value(xop, xfp, right, 0, "compare: right");

    switch (TYPE_CMP(left.xev_type, right.xev_type)) {
    case TYPE_CMP(C_STRING, C_STRING):
	rc = strcmp(left.xev_str, right.xev_str);
	break;

    case TYPE_CMP(C_INT64, C_INT64):
	rc = (left.xev_int64 > right.xev_int64) ? 1
	    : (left.xev_int64 < right.xev_int64) ? -1 : 0;
	break;

    case TYPE_CMP(C_UINT64, C_UINT64):
	rc = (left.xev_uint64 > right.xev_uint64) ? 1
	    : (left.xev_uint64 < right.xev_uint64) ? -1 : 0;
	break;

    case TYPE_CMP(C_FLOAT, C_FLOAT):
	rc = (left.xev_float > right.xev_float) ? 1
	    : (left.xev_float < right.xev_float) ? -1 : 0;
	break;

    case TYPE_CMP(C_STRING, C_INT64):
	fval = xo_eval_cast_float(xfp, left);
	rc = (fval > right.xev_int64) ? 1 : (fval < right.xev_int64) ? -1 : 0;
	break;

    case TYPE_CMP(C_INT64, C_STRING):
	fval = xo_eval_cast_float(xfp, right);
	rc = (left.xev_int64 > fval) ? 1 : (left.xev_int64 < fval) ? -1 : 0;
	break;

    case TYPE_CMP(C_STRING, C_FLOAT):
	fval = xo_eval_cast_float(xfp, left);
	rc = (fval > right.xev_float) ? 1 : (fval < right.xev_float) ? -1 : 0;
	break;

    case TYPE_CMP(C_FLOAT, C_STRING):
	fval = xo_eval_cast_float(xfp, right);
	rc = (left.xev_float > fval) ? 1 : (left.xev_float < fval) ? -1 : 0;
	break;

    case TYPE_CMP(C_BOOLEAN, C_BOOLEAN):
    case TYPE_CMP(C_INT64, C_BOOLEAN):
    case TYPE_CMP(C_BOOLEAN, C_INT64):
    case TYPE_CMP(C_UINT64, C_BOOLEAN): /* Cheating a bit, but we only ... */
    case TYPE_CMP(C_BOOLEAN, C_UINT64): /* ... care about non-zero and zero */
	if (left.xev_int64 == 0) {
	    rc = (right.xev_int64 == 0) ? 0 : 1;
	} else {
	    rc = (right.xev_int64 == 0) ? -1 : 0;
	}
	break;

    default:
	return xo_eval_value_invalid();
    }

    value.xev_type = C_INT64;
    value.xev_int64 = rc;

    xo_eval_dump_value(xop, xfp, value, 0, "compare");
    return value;
}

static xo_eval_value_t
xo_eval_op_and (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_value_make(C_BOOLEAN, 0, 0);

    int bool = xo_eval_cast_boolean(xfp, left);
    if (!bool) {
	value.xev_int64 = 0;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    bool = xo_eval_cast_boolean(xfp, right);
    if (!bool) {
	value.xev_int64 = 0;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    value.xev_int64 = 1;
    return value;
}

static xo_eval_value_t
xo_eval_op_or (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_value_make(C_BOOLEAN, 0, 0);

    int bool = xo_eval_cast_boolean(xfp, left);
    if (bool) {
	value.xev_int64 = 1;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    bool = xo_eval_cast_boolean(xfp, right);
    if (bool) {
	value.xev_int64 = 1;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    value.xev_int64 = 0;
    return value;
}

static xo_eval_value_t
xo_eval_op_equals (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_compare(XO_EVAL_OP_PASS);

    value.xev_type = C_BOOLEAN;
    value.xev_int64 = (value.xev_int64 == 0) ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_op_notequals (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_compare(XO_EVAL_OP_PASS);

    value.xev_type = C_BOOLEAN;
    value.xev_int64 = (value.xev_int64 == 0) ? 0 : 1;
    return value;
}

static xo_eval_value_t
xo_eval_op_lt (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_compare(XO_EVAL_OP_PASS);

    value.xev_type = C_BOOLEAN;
    value.xev_int64 = (value.xev_int64 < 0) ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_op_le (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_compare(XO_EVAL_OP_PASS);

    value.xev_type = C_BOOLEAN;
    value.xev_int64 = (value.xev_int64 <= 0) ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_op_gt (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_compare(XO_EVAL_OP_PASS);

    value.xev_type = C_BOOLEAN;
    value.xev_int64 = (value.xev_int64 > 0) ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_op_ge (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_compare(XO_EVAL_OP_PASS);

    value.xev_type = C_BOOLEAN;
    value.xev_int64 = (value.xev_int64 >= 0) ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_calc (XO_EVAL_OP_ARGS, xo_eval_calc_fn_t calc_fn)
{
    xo_eval_value_t lfloat = xo_eval_cast_float_value(xfp, left);
    xo_eval_value_t rfloat = xo_eval_cast_float_value(xfp, right);

    return calc_fn(xop, xfp, lfloat, rfloat);
}

static xo_eval_value_t
xo_eval_calc_plus (XO_EVAL_CALC_ARGS)
{
    left.xev_float += right.xev_float;
    return left;
}

static xo_eval_value_t
xo_eval_op_plus (XO_EVAL_OP_ARGS)
{
    return xo_eval_calc(XO_EVAL_OP_PASS,
			       xo_eval_calc_plus);
}

static xo_eval_value_t
xo_eval_calc_minus (XO_EVAL_CALC_ARGS)
{
    left.xev_float -= right.xev_float;
    return left;
}

static xo_eval_value_t
xo_eval_op_minus (XO_EVAL_OP_ARGS)
{
    return xo_eval_calc(XO_EVAL_OP_PASS,
			       xo_eval_calc_minus);
}

static xo_eval_value_t
xo_eval_calc_div (XO_EVAL_CALC_ARGS)
{
    left.xev_float /= right.xev_float;
    return left;
}

static xo_eval_value_t
xo_eval_op_div (XO_EVAL_OP_ARGS)
{
    return xo_eval_calc(XO_EVAL_OP_PASS,
			       xo_eval_calc_div);
}

static xo_float_t
xo_fmod (xo_float_t x, xo_float_t y)
{
    if (y == 0)
	return 0;

    int64_t i = (int64_t)(x / y);
    xo_float_t n = y * (xo_float_t) i;

    return x - (xo_float_t) n;
}

static xo_eval_value_t
xo_eval_calc_mod (XO_EVAL_CALC_ARGS)
{
    left.xev_float = xo_fmod(left.xev_float, right.xev_float);
    return left;
}

static xo_eval_value_t
xo_eval_op_mod (XO_EVAL_OP_ARGS)
{
    return xo_eval_calc(XO_EVAL_OP_PASS,
			       xo_eval_calc_mod);
}

static xo_eval_value_t
xo_eval (xo_handle_t *xop, xo_filter_t *xfp, xo_match_t *xmp,
			 xo_xparse_node_id_t id, xo_eval_op_fn_t op_fn)
{
    xo_eval_value_t value = xo_eval_value_invalid();
    int first = 1;
    xo_eval_value_t last = { 0 };

    xo_xparse_dump_one_node(&xfp->xf_xd, id, 0, "eval one: ");

    xo_xparse_node_t *xnp;
    xo_eval_node_fn_t *node_fn = NULL;
    xo_eval_op_fn_t *nested_op_fn = NULL;

    for (; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	node_fn = NULL;
	nested_op_fn = NULL;

	switch (xnp->xn_type) {

	case C_PATH:
	    node_fn = xo_eval_path;
	    break;

	case K_AND:
	    nested_op_fn = xo_eval_op_and;
	    break;

	case K_DIV:
	    nested_op_fn = xo_eval_op_div;
	    break;

	case K_MOD:
	    nested_op_fn = xo_eval_op_mod;
	    break;

	case K_OR:
	    nested_op_fn = xo_eval_op_or;
	    break;

	case L_EQUALS:
	    nested_op_fn = xo_eval_op_equals;
	    break;

	case L_GRTR:
	    nested_op_fn = xo_eval_op_gt;
	    break;

	case L_GRTREQ:
	    nested_op_fn = xo_eval_op_ge;
	    break;

	case L_LESS:
	    nested_op_fn = xo_eval_op_lt;
	    break;

	case L_LESSEQ:
	    nested_op_fn = xo_eval_op_le;
	    break;

	case L_PLUS:
	    nested_op_fn = xo_eval_op_plus;
	    break;

	case L_MINUS:
	    nested_op_fn = xo_eval_op_minus;
	    break;

#if 0
	case L_NOT:
	    node_fn = xo_eval_not;
	    break;
#endif

	case L_NOTEQUALS:
	    nested_op_fn = xo_eval_op_notequals;
	    break;

	case T_FUNCTION_NAME:
	    value = xo_eval_value_invalid();
	    break;

	case T_NUMBER:
	    node_fn = xo_eval_number;
	    break;

	case T_QUOTED:
	    node_fn = xo_eval_quoted;
	    break;

	default:		/* For now; should be XEVF_UNSUPPORTED */
	    xo_failure(xop, "filter: unhandle type: '%s'",
		       xo_xparse_fancy_token_name(xnp->xn_type));

	    if (xnp->xn_contents)
		value = xo_eval(xop, xfp, xmp, xnp->xn_contents, op_fn);
	}

	if (node_fn)
	    value = node_fn(xop, xfp, xmp, xnp);
	else if (nested_op_fn)
	    value = xo_eval(xop, xfp, xmp, xnp->xn_contents,
				  nested_op_fn);

	if (first) {
	    first = 0;
	    last = value;

	} else if (op_fn) {
	    value = op_fn(xop, xfp, xmp, xnp, last, value);
	}

	xo_eval_dump_value(xop, xfp, value, 4, "eval");

	/*
	 * We want to allow short-circuiting so if the 'final' flag is
	 * on, we stop further processing.  We want to turn the
	 * 'final' bit off before we return the value, since it's not
	 * final for our caller.
	 */
	if (value.xev_flags & XEVF_FINAL) {
	    value.xev_flags &= ~XEVF_FINAL; /* Turn it off */
	    break;
	}

	last = value;
    }

    return value;
}

/*
 * This is the big deal: evaluate a predicate and see if
 *
 * (a) any referenced variables are missing; if so we need to delay
 * (b) if the expression is true or false
 *
 * We use our explicit knowledge of the data to "cheat": since we know
 * that keys must appear first and we know that we are only
 * (currently) supporting predicates that reference keys, then we
 * don't have to concern ourselves with N*M problems like: foo[x == y]
 * If "x" is a key, then it can only appear once; same for "y".  This
 * means we don't have to think about the case where multiple "x"s and
 * "y"s can appear and the predicate is true if any "x" matches any "y".
 */
static xo_eval_value_t
xo_filter_pred_eval (xo_handle_t *xop, xo_filter_t *xfp, xo_match_t *xmp)
{
    xo_eval_value_t value = { 0 };

    xo_xparse_dump_one_node(&xfp->xf_xd, xmp->xm_stackp->xs_predicates,
			    0, "eval: ");

    xo_xparse_node_id_t id;
    xo_xparse_node_t *xnp;

    for (id = xmp->xm_stackp->xs_predicates; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);

	if (xnp->xn_type != C_PREDICATE) /* Can't eval anything else */
	    continue;

	value = xo_eval(xop, xfp, xmp, xnp->xn_contents, NULL);
	xo_eval_dump_value(xop, xfp, value, 4, "xo_filter_pred_eval: working");
    }

    xo_eval_dump_value(xop, xfp, value, 2, "xo_filter_pred_eval: final");
    return value;
}

/*
 * Recurse down the complete predicate, seeing if it even wants the
 * tag.
 */
static int
xo_filter_pred_needs (xo_xparse_data_t *xdp, xo_filter_t *xfp,
		      xo_xparse_node_id_t id,
		      const char *tag, xo_ssize_t tlen)
{
    xo_xparse_node_t *xnp;

    for (; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(xdp, id);
	if (xnp->xn_type == C_ELEMENT) {
	    const char *str = xo_xparse_str(xdp, xnp->xn_str);
	    xo_ssize_t slen = strlen(str);
	    if (slen == tlen && memcmp(str, tag, slen) == 0)
		return TRUE;
	} else if (xnp->xn_type == C_ABSOLUTE) {
	    /* Nothing to do, just handle the next node (xn_next) */
	}

	if (xnp->xn_contents)
	    if (xo_filter_pred_needs(xdp, xfp, xnp->xn_contents,
				     tag, tlen))
		return TRUE;
    }

    return FALSE;
}

int
xo_filter_key (xo_handle_t *xop, xo_filter_t *xfp,
	       const char *tag, xo_ssize_t tlen,
	       const char *value, xo_ssize_t vlen)
{
    xo_xparse_data_t *xdp = &xfp->xf_xd;
    xo_match_t *xmp = xfp->xf_matches;
    uint32_t i UNUSED;
    xo_xparse_node_t *xnp;
    xo_xparse_node_id_t id;
    int rc = 0;

    XO_DBG(xop, "xo_filter_key: '%.*s' = '%.*s'", tlen, tag, vlen, value);
    xo_filter_dump_matches(xop, xfp);

    for (i = 0; xmp; i++, xmp = xmp->xm_next) { /* For each active match */
	xo_stack_t *xsp = xmp->xm_stackp;
	if (xsp->xs_state != XSS_PRED) /* Not looking for keys */
	    continue;

	rc = XO_FILTER_MISS; /* Start with needing more data */

	for (id = xsp->xs_predicates; id; id = xnp->xn_next) {
	    xnp = xo_xparse_node(xdp, id);

	    if (xnp->xn_type != C_PREDICATE) /* Only type supported */
		continue;

	    if (!xo_filter_pred_needs(xdp, xfp, xsp->xs_predicates,
				      tag, tlen)) {
		XO_DBG(xop, "xo_filter_key: predicate doesn't need '%.*s'",
		       tlen, tag);
		continue;
	    }

	    xo_filter_key_add(xop, xfp, xmp, tag, tlen, value, vlen);

	    const char *test UNUSED = xo_filter_key_find(xfp, xmp, tag);
	    XO_DBG(xop, "filter: new key: [%s] '%s'",
		   tag, test ?: "");

	    xo_eval_value_t result = xo_filter_pred_eval(xop, xfp, xmp);
	    int pred = xo_eval_cast_int64(xfp, result);

	    XO_DBG(xop, "filter: key: pred eval [%u] '%s' "
		   "[base %u [%u/%u] -> %d",
		   i, tag, xmp->xm_base,
		   xfp->xf_allow, xfp->xf_deny, pred);

	    xo_eval_dump_value(xop, xfp, result, 4, "xo_filter_key: working");

	    if (result.xev_flags & XEVF_MISSING) {
		rc = XO_FILTER_MISS; /* Need more data */
		break;
	    }

	    if (!pred) {
		rc = XO_FILTER_FAIL;	/* Never going to succeed */
		break;
	    }

	    rc = 0;		/* Otherwise we might be done */
	}

	if (rc == XO_FILTER_FAIL) {	/* Never going to succeed */
	    xo_filter_deadend(xop, xfp, xmp, xsp, TRUE);
	    continue;
	}

	/* If the predicate isn't complete, we skip to the next match */
	if (rc != 0)
	    continue;

	xsp->xs_state = XSS_FOUND; /* Mark our success */

	/*
	 * Lots going on here.  We have a successful match on a
	 * set of predicates, but what's next?  Go back to the stack
	 * and see.
	 */
	xnp = xo_xparse_node(xdp, xsp->xs_match);

	const char *label UNUSED = "";
	if (xnp->xn_next == 0) {
	    /* We don't set xm_depth to 1 here; this "open" doesn't count */
	    label = xo_filter_match_adjust(xop, xfp, xmp, xsp, XSS_DEEP);

	} else {
	    xo_xparse_node_t *nextp = xo_xparse_node(xdp, xnp->xn_next);

	    /* nextp should never by NULL, but we test anyway */
	    xsp = xo_filter_stack_push(xfp, xmp, XSS_NEED, xnp->xn_next,
					   nextp ? nextp->xn_contents : 0);
	}

	/* A succesful match */
	XO_DBG(xop, "filter: key success [%u] '%.*s' "
	       "[match %u, next %u] [allow %u/deny %u]%s",
	       i, tlen, tag, xmp->xm_base, xsp->xs_match,
	       xfp->xf_allow, xfp->xf_deny, label);
    }

    XO_DBG(xop, "xo_filter_key: '%.*s' = '%.*s' --> %d",
	   tlen, tag, vlen, value, rc);

    xo_filter_change_status(xop, xfp, "key");

    xo_filter_dump_matches(xop, xfp);

    return xfp->xf_status;
}

/* ------------------------------------------------------------- */

/*
 * Dump all the current matches, each with their current state and
 * stack information, allowing us to see what's going on.
 *
 * We don't want to use XO_DBG for this since we want the output to be
 * available for the "--libxo debug" flag.
 */
static void
xo_filter_dump_matches (xo_handle_t *xop, xo_filter_t *xfp)
{
    if (xfp == NULL)
	return;

    xo_dbg(xop, "xo_filter_dump_matches: [depth %d] status: %s/%d",
	   xfp->xf_depth, xo_filter_status_name(xfp->xf_status),
	   xfp->xf_status);

    /*
     * Whiffle thru the states to see if we have any open paths.  We
     * do this first since we'll be pushing new paths.
     */
    xo_xparse_data_t *xdp = &xfp->xf_xd;
    xo_match_t *xmp = xfp->xf_matches;
    uint32_t i;
    xo_xparse_node_t *xnp;
    const char *str;

    for (i = 0; xmp; xmp = xmp->xm_next, i++) {
	xo_dbg(xop, "  match %d: base %u, depth %u, flags %#x "
	       "[allow %u/deny %u]",
	       i, xmp->xm_base, xmp->xm_depth, xmp->xm_flags,
	       xfp->xf_allow, xfp->xf_deny);

	xo_stack_t *xsp;
	for (xsp = xmp->xm_stack; xsp <= xmp->xm_stackp; xsp++) {
	    xnp = xo_xparse_node(xdp, xsp->xs_match);
	    str = xnp ? xo_xparse_str(xdp, xnp->xn_str) : "";

	    xo_dbg(xop, "    stack: state %u/%s, node %u, pred %u, [str '%s'] "
		   "keys_len %d, allow %u, deny %u",
		   xsp->xs_state, xo_filter_state_name(xsp->xs_state),
		   xsp->xs_match, xsp->xs_predicates, str,
		   xsp->xs_keys_len, xsp->xs_allow, xsp->xs_deny);
	}
    }
}

/*
 * We use the whiteboard to stash content that can be reused.
 */
int
xo_filter_whiteboard (XO_ENCODER_HANDLER_ARGS,
		      xo_encoder_func_t func XO_UNUSED,
 		      struct xo_filter_s *xfp)
{
    int rc = 0;
    xo_buffer_t *xbp = bufp;

    XO_DBG(xop, "filter: entering whiteboard: %s: '%s'%s status: %s/%d",
	   xo_encoder_op_name(op), name ?: "",
	   (flags & XFF_KEY) ? " is-a-key" : "",
	   xo_filter_status_name(xfp->xf_status), xfp->xf_status);

    switch (op) {
    case XO_OP_OPEN_CONTAINER:
    case XO_OP_OPEN_LIST:
    case XO_OP_OPEN_INSTANCE:
    case XO_OP_OPEN_LEAF_LIST:
    case XO_OP_CLOSE_CONTAINER:
    case XO_OP_CLOSE_LIST:
    case XO_OP_CLOSE_INSTANCE:
    case XO_OP_CLOSE_LEAF_LIST:
	/*
	 * We need to pass open and close events to the encoder can
	 * track them, regardless of state.
	 */
	break;

    case XO_OP_STRING:		   /* Quoted UTF-8 string */
    case XO_OP_CONTENT:		   /* Other content */
    case XO_OP_ATTRIBUTE:;	   /* Attribute name/value */
	if (xfp->xf_status == XO_STATUS_DEAD) /* The dead have no cares */
	    return 0;

	/*
	 * If the filters aren't all dead, we always want to pass keys
	 * along.  For non-keys, we look at 'allow' to decide: if
	 * allow is false, we don't want it.
	 */
	if (flags & XFF_KEY) { /* Always need keys */
	    /*
	     * Let the predicate logic know we've got a key.
	     */
	    xo_filter_key(xop, xfp, name, strlen(name), value, strlen(value));

	} else {
	    if (xfp->xf_status == XO_STATUS_TRACK)
		return 0;  	  /* Tracking doesn't need non-keys */
	}

	break;
    }

    rc = func(xop, op, xbp, name, value, private, flags);


    XO_DBG(xop, "filter: leaving whiteboard: %s: '%s'%s status: %s/%d",
	   xo_encoder_op_name(op), name ?: "",
	   (flags & XFF_KEY) ? " is-a-key" : "",
	   xo_filter_status_name(xfp->xf_status), xfp->xf_status);

    return rc;
}
