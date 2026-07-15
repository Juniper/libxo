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
#include <math.h>

#include "xo_config.h"

/*
 * Even if xo_config.h didn't ask for filters, we'll need it to build here
 */
#ifndef LIBXO_NEED_FILTERS
#define LIBXO_NEED_FILTERS
#endif /* LIBXO_NEED_FILTERS */

#include "xo.h"
#include "xo_private.h"
#include "xo_buf.h"
#include "xo_encoder.h"
#include "xo_xpath.tab.h"

#include "xo_xparse.h"
#include "xo_filter.h"

#define XO_INDENT	4

typedef double xo_float_t;	/* Our floating point type */

/*
 * We extensively return aggregates (structures) in the file, under the
 * contraint that they are less than 128 bits and can be returned via
 * registers without impacting performance.  So we turn off warnings
 * for aggregate returns.
 */
#pragma GCC   diagnostic ignored "-Waggregate-return"

/*
 * Compiled trie for simultaneous multi-expression XPath matching.
 *
 * At init time all parsed XPath expressions are compiled into a shared
 * prefix-trie so expressions with common prefixes share nodes.
 *
 * At runtime a stack of xo_tframe_t frames (one per nesting depth).
 * Each frame records which trie nodes are currently active and in
 * what sub-state.
 */
typedef uint32_t xo_trie_id_t;	/* Index trie node array (1-based; 0=none) */

/*
 * One node in the compiled trie.  Stored in a flat 1-based array;
 * index 0 means "none".
 */
typedef struct xo_tnode_s {
    xo_off_t xtn_name;          /* Element name offset in xparse string table */
    xo_xparse_node_id_t xtn_pred; /* Predicate subtree root (0=none) */
    xo_trie_id_t xtn_child;	/* First child trie node (0 = leaf) */
    xo_trie_id_t xtn_sibling;	/* Next sibling at this level */
    uint16_t xtn_flags;		/* XTNF_* flags */
} xo_tnode_t;

#define XTNF_TERMINAL	(1<<0)	/* A complete expression ends here */
#define XTNF_NOT	(1<<1)	/* "not" expression: deny on match */
#define XTNF_ABSOLUTE	(1<<2)	/* Anchored at tree root (leading '/') */
#define XTNF_WILDCARD	(1<<3)	/* Wildcard step ('*'): matches any tag */

/*
 * The compiled trie: a flat node array plus the root sibling chain.
 */
typedef struct xo_trie_s {
    xo_tnode_t *xt_nodes;	/* [1..xt_count]; slot 0 unused */
    xo_trie_id_t xt_count;	/* Nodes in use */
    xo_trie_id_t xt_cap;	/* Allocated capacity */
    xo_trie_id_t xt_root;	/* First root-level sibling */
    xo_xparse_data_t *xt_xd;	/* Parse data (for string lookup) */
} xo_trie_t;

/*
 * Per-depth runtime frame.  We cap simultaneous active nodes at
 * XO_TFRAME_MAX; this covers the common case of a handful of filters
 * without heap allocation.
 */
#define XO_TFRAME_MAX	8

typedef struct xo_tframe_s {
    uint8_t xtf_count;		/* # of active slots */
    uint8_t xtf_state[XO_TFRAME_MAX]; /* XTFS_* per slot */
    uint8_t xtf_flags[XO_TFRAME_MAX]; /* XTFF_* per slot */
    xo_trie_id_t xtf_node[XO_TFRAME_MAX]; /* trie node id per slot */
    uint32_t xtf_position[XO_TFRAME_MAX]; /* 1-based open position per slot */
    uint32_t xtf_qual_position[XO_TFRAME_MAX]; /* qualified position (leading-pred-gated) */
    int16_t xtf_allow_delta;	/* allow contribution to undo on pop */
    int16_t xtf_deny_delta;	/* deny contribution to undo on pop */
    char *xtf_keys;		/* buffered "k\0v\0k2\0v2\0\0" pairs */
    ssize_t xtf_keys_len;
    char *xtf_attrs;		/* buffered "@k\0v\0..." pairs (attributes) */
    ssize_t xtf_attrs_len;
    uint32_t xtf_position_cur;  /* scratch: position for current C_INDEX eval */
    /* Child sibling counters (tracked in the PARENT frame, survive close) */
    uint8_t xtf_child_ncount;
    xo_trie_id_t xtf_child_node[XO_TFRAME_MAX];
    uint32_t xtf_child_count_val[XO_TFRAME_MAX];
    /* Qualified child counters: only count when leading predicates pass */
    uint8_t xtf_child_qual_ncount;
    xo_trie_id_t xtf_child_qual_node[XO_TFRAME_MAX];
    uint32_t xtf_child_qual_val[XO_TFRAME_MAX];
} xo_tframe_t;

/* Per-slot flags (xtf_flags[]) */
#define XTFF_QUAL_COUNTED  (1 << 0)  /* leading-pred qualified position counted */

/* Per-slot states */
#define XTFS_SEEK	0	/* Waiting for this node's element name */
#define XTFS_PRED	1	/* Name matched; evaluating predicates */
#define XTFS_LIVE	2	/* Fully matched (name + predicates) */
#define XTFS_DEAD	3	/* Predicate failed; ignore sub-tree */

/*
 * Runtime matching state: a stack of frames driven by open/close events.
 */
typedef struct xo_tmatch_s {
    xo_trie_t *xtm_trie;	        /* The compiled trie */
    uint32_t xtm_depth;	        /* Current stack depth */
    uint32_t xtm_cap;	        /* Allocated frame count */
    xo_tframe_t *xtm_stack;	/* Frame stack [0..xtm_depth] */
    uint32_t xtm_allow;          /* Active allow-match count */
    uint32_t xtm_deny;           /* Active deny-match count */
} xo_tmatch_t;

/*
 * Allocate a trie node
 */
static xo_trie_id_t
xo_trie_alloc_node (xo_trie_t *xtp)
{
    if (xtp->xt_count + 1 >= xtp->xt_cap) {
	xo_trie_id_t cap = xtp->xt_cap ? xtp->xt_cap * 2 : 16;
	xo_tnode_t *p = xo_realloc(xtp->xt_nodes, cap * sizeof(*p));
	if (p == NULL)
	    return 0;

	xtp->xt_nodes = p;
	xtp->xt_cap = cap;
    }

    xo_trie_id_t id = ++xtp->xt_count;
    bzero(&xtp->xt_nodes[id], sizeof(xo_tnode_t));

    return id;
}

/*
 * Return (or create) the wildcard child of `parent`.
 * parent==0 means the root sibling list.
 */
static xo_trie_id_t
xo_trie_get_wildcard_child (xo_trie_t *xtp, xo_trie_id_t parent)
{
    xo_trie_id_t *listp = parent
	? &xtp->xt_nodes[parent].xtn_child
	: &xtp->xt_root;

    for (xo_trie_id_t s = *listp; s; s = xtp->xt_nodes[s].xtn_sibling)
	if (xtp->xt_nodes[s].xtn_flags & XTNF_WILDCARD)
	    return s;

    xo_trie_id_t id = xo_trie_alloc_node(xtp);
    if (id == 0)
	return 0;

    /* xo_trie_alloc_node may have realloced xt_nodes; recalculate listp */
    listp = parent ? &xtp->xt_nodes[parent].xtn_child : &xtp->xt_root;

    xtp->xt_nodes[id].xtn_flags |= XTNF_WILDCARD;
    xtp->xt_nodes[id].xtn_sibling = *listp;
    *listp = id;

    return id;
}

/*
 * Return (or create) the child of `parent` with name `name_id`.
 * parent==0 means the root sibling list.
 */
static xo_trie_id_t
xo_trie_get_child (xo_trie_t *xtp, xo_trie_id_t parent, xo_off_t name_id)
{
    xo_trie_id_t *listp = parent
	? &xtp->xt_nodes[parent].xtn_child
	: &xtp->xt_root;

    for (xo_trie_id_t s = *listp; s; s = xtp->xt_nodes[s].xtn_sibling)
	if (xtp->xt_nodes[s].xtn_name == name_id)
	    return s;

    xo_trie_id_t id = xo_trie_alloc_node(xtp);
    if (id == 0)
	return 0;

    /* xo_trie_alloc_node may have realloced xt_nodes; recalculate listp */
    listp = parent ? &xtp->xt_nodes[parent].xtn_child : &xtp->xt_root;

    xtp->xt_nodes[id].xtn_name = name_id;
    xtp->xt_nodes[id].xtn_sibling = *listp;
    *listp = id;

    return id;
}

static void
xo_trie_insert (xo_trie_t *xtp, xo_xparse_data_t *xdp,
		xo_xparse_node_id_t first_elem, uint16_t flags)
{
    xo_trie_id_t parent = 0;
    xo_xparse_node_t *xnp;

    for (xo_xparse_node_id_t id = first_elem; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(xdp, id);
	if (xnp->xn_type == C_ABSOLUTE) {
	    flags |= XTNF_ABSOLUTE;
	    continue;
	}

	xo_trie_id_t tid;
	if (xnp->xn_type == L_ASTERISK) {
	    tid = xo_trie_get_wildcard_child(xtp, parent);
	} else if (xnp->xn_type == C_ELEMENT) {
	    tid = xo_trie_get_child(xtp, parent, xnp->xn_str);
	} else {
	    continue;
	}
	if (tid == 0)
	    return;

	/*
	 * XTNF_ABSOLUTE means the path starts at root.  The check in
	 * xo_tmatch_open runs on root-level trie nodes, so the flag
	 * must live on the first element of the path (parent == 0),
	 * not on the terminal node.
	 */
	if (parent == 0 && (flags & XTNF_ABSOLUTE))
	    xtp->xt_nodes[tid].xtn_flags |= XTNF_ABSOLUTE;

	/* Attach predicate if present among this element's children */
	for (xo_xparse_node_id_t cid = xnp->xn_contents; cid; ) {
	    xo_xparse_node_t *cp = xo_xparse_node(xdp, cid);
	    if (cp->xn_type == C_PREDICATE) {
		xtp->xt_nodes[tid].xtn_pred = cid;
		break;
	    }
	    cid = cp->xn_next;
	}
	parent = tid;
    }

    if (parent)
	xtp->xt_nodes[parent].xtn_flags |= XTNF_TERMINAL | (flags & ~XTNF_ABSOLUTE);
}

static xo_trie_t *
xo_trie_compile (xo_handle_t *xop UNUSED, xo_xparse_data_t *xdp)
{
    xo_trie_t *xtp = xo_realloc(NULL, sizeof(*xtp));
    if (xtp == NULL)
	return NULL;

    bzero(xtp, sizeof(*xtp));
    xtp->xt_xd = xdp;

    xo_xparse_node_id_t *paths = xdp->xd_paths;
    for (uint32_t i = 0; i < xdp->xd_paths_cur; i++, paths++) {
	xo_xparse_node_t *xnp = xo_xparse_node(xdp, *paths);
	uint16_t flags = 0;
	xo_xparse_node_id_t elem = *paths;

	switch (xnp->xn_type) {
	case C_ELEMENT:
	    break;

	case C_ABSOLUTE:
	    flags |= XTNF_ABSOLUTE;
	    elem = xnp->xn_next;
	    break;

	case C_NOT:
	    flags |= XTNF_NOT;
	    elem = xnp->xn_contents;
	    break;

	case C_PATH:
	    elem = xnp->xn_contents;
	    break;

	case L_ASTERISK:
	    break;		/* wildcard root: elem already = *paths */

	default:
	    continue;
	}
	xo_trie_insert(xtp, xdp, elem, flags);
    }

    return xtp;
}

static void
xo_trie_free (xo_trie_t *xtp)
{
    if (xtp) {
	xo_free(xtp->xt_nodes);
	xo_free(xtp);
    }
}

/* xo_tframe_* functions are for runtime processing */

static void
xo_tframe_key_add (xo_tframe_t *frame,
		   const char *tag, xo_ssize_t tlen,
		   const char *value, xo_ssize_t vlen)
{
    xo_ssize_t new_len = tlen + vlen + 3; /* two NULs plus final NUL */
    char *newp = xo_realloc(frame->xtf_keys, frame->xtf_keys_len + new_len);
    if (newp == NULL)
	return;

    char *addp = newp + frame->xtf_keys_len;
    memcpy(addp, tag, tlen);
    addp += tlen;
    *addp++ = '\0';
    memcpy(addp, value, vlen);
    addp += vlen;
    *addp++ = '\0';
    *addp++ = '\0';

    frame->xtf_keys_len += new_len - 1; /* exclude the final extra NUL */
    frame->xtf_keys = newp;
}

static void
xo_tframe_free_keys (xo_tframe_t *frame)
{
    if (frame->xtf_keys) {
	xo_free(frame->xtf_keys);
	frame->xtf_keys = NULL;
	frame->xtf_keys_len = 0;
    }
}

static void
xo_tframe_attr_add (xo_tframe_t *frame,
		    const char *tag, xo_ssize_t tlen,
		    const char *value, xo_ssize_t vlen)
{
    xo_ssize_t new_len = tlen + vlen + 3;
    char *newp = xo_realloc(frame->xtf_attrs, frame->xtf_attrs_len + new_len);
    if (newp == NULL)
	return;

    char *addp = newp + frame->xtf_attrs_len;
    memcpy(addp, tag, tlen);
    addp += tlen;
    *addp++ = '\0';
    memcpy(addp, value, vlen);
    addp += vlen;
    *addp++ = '\0';
    *addp++ = '\0';

    frame->xtf_attrs_len += new_len - 1;
    frame->xtf_attrs = newp;
}

static void
xo_tframe_free_attrs (xo_tframe_t *frame)
{
    if (frame->xtf_attrs) {
	xo_free(frame->xtf_attrs);
	frame->xtf_attrs = NULL;
	frame->xtf_attrs_len = 0;
    }
}

static int
xo_tmatch_init (xo_handle_t *xop UNUSED, xo_tmatch_t *xtmp, xo_trie_t *trie)
{
    bzero(xtmp, sizeof(*xtmp));
    xtmp->xtm_trie = trie;

    uint32_t cap = 16;
    xtmp->xtm_stack = xo_realloc(NULL, cap * sizeof(*xtmp->xtm_stack));
    if (xtmp->xtm_stack == NULL)
	return -1;

    bzero(xtmp->xtm_stack, cap * sizeof(*xtmp->xtm_stack));
    xtmp->xtm_cap = cap;

    /*
     * Depth-0 is an empty virtual frame.  The root re-probe loop in
     * xo_tmatch_open matches root trie nodes at every real depth, so
     * pre-seeding depth-0 with LIVE root nodes is wrong: it would let
     * the parent-descent loop skip the first step of a multi-step path
     * (e.g. "d/one" would match when "one" is opened directly).
     */
#if defined(XO_DEBUG)
    if (XOIF_ISSET(xop, XOF_DEBUG)) {
	xo_trie_t *xtp = trie;
	uint32_t root_count = 0;
	for (xo_trie_id_t r = xtp->xt_root; r; r = xtp->xt_nodes[r].xtn_sibling)
	    root_count += 1;

	xo_dbg(xop, "xo_tmatch_init: trie root nodes: %u", root_count);
    }
#endif /* XO_DEBUG */

    return 0;
}

static void
xo_tmatch_cleanup (xo_tmatch_t *xtmp)
{
    if (xtmp->xtm_stack) {
	for (uint32_t d = 0; d <= xtmp->xtm_depth; d++) {
	    xo_tframe_free_keys(&xtmp->xtm_stack[d]);
	    xo_tframe_free_attrs(&xtmp->xtm_stack[d]);
	}
	xo_free(xtmp->xtm_stack);
	xtmp->xtm_stack = NULL;
    }
}

/*
 * xo_tmatch_record_live/open/close/key and xo_filter_pred_eval all
 * reference types (xo_eval_value_t, xo_tmatch_t, xo_filter_s fields)
 * defined later in this file.  They are placed after those definitions;
 * forward declarations appear here.
 */
static void xo_tmatch_record_live(xo_tmatch_t *, xo_tframe_t *, xo_tnode_t *);
static void xo_tmatch_open(xo_handle_t *, xo_filter_t *, xo_tmatch_t *,
			   const char *, ssize_t);
static void xo_tmatch_close(xo_handle_t *, xo_filter_t *, xo_tmatch_t *,
			    const char *, ssize_t);
static int xo_tmatch_try_eager(xo_handle_t *, xo_filter_t *, xo_tframe_t *,
			       xo_xparse_node_id_t, xo_tnode_t *, xo_tmatch_t *);
static void xo_filter_force_resolve_pred(xo_handle_t *, xo_filter_t *,
					 const char *);

typedef unsigned xo_xsf_flags_t;   /* Type for XFSF_* flag fields */

struct xo_filter_s {		 /* Forward/typdef decl in xo_private.h */
    struct xo_xparse_data_s xf_xd; /* Main parsing structure */
    xo_filter_status_t xf_status; /* Current status: (see XO_STATUS_*) */
    uint32_t xf_depth;		 /* Depth of hierarchy seen (zero == top) */
    uint32_t xf_total_depth;	 /* Total depth ('opens' minus 'closes') */
    xo_xsf_flags_t xf_flags;	 /* Flags (XFSF_*) */
    xo_trie_t *xf_trie;	 /* Compiled trie (NULL until first filter added) */
    xo_tmatch_t xf_tmatch;	 /* Runtime trie-matching state */
};

/* Flags for xf_flags */
#define XFSF_BLOCK		(1<<0)	/* Block emitting data */
#define XFSF_FORCE_RESOLVE	(1<<1)	/* Treat missing fields as "" at close */

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
static xo_filter_t *
xo_filter_op_create (xo_handle_t *xop)
{
    xo_filter_t *xfp = xo_realloc(NULL, sizeof(*xfp));
    if (xfp == NULL)
	return NULL;

    bzero(xfp, sizeof(*xfp));

    xo_xparse_init(&xfp->xf_xd);

    xo_set_filter_data(xop, xfp);

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
static void
xo_filter_op_destroy (xo_handle_t *xop, xo_filter_t *xfp)
{
    xo_xparse_clean(&xfp->xf_xd);
    xo_tmatch_cleanup(&xfp->xf_tmatch);
    xo_trie_free(xfp->xf_trie);
    xfp->xf_trie = NULL;

    xo_set_filter_data(xop, NULL);
    xo_free(xfp);
}

static void
xo_tmatch_record_live (xo_tmatch_t *xtmp, xo_tframe_t *frame, xo_tnode_t *tn)
{
    if (!(tn->xtn_flags & XTNF_TERMINAL))
	return;

    if (tn->xtn_flags & XTNF_NOT) {
	xtmp->xtm_deny += 1;
	frame->xtf_deny_delta += 1;
    } else {
	xtmp->xtm_allow += 1;
	frame->xtf_allow_delta += 1;
    }
}

/*
 * Look up child trie node `c` in the parent's sibling-counter table,
 * increment its count, and return the new (1-based) open position.
 */
static uint32_t
xo_tframe_child_position (xo_tframe_t *parent, xo_trie_id_t c)
{
    for (uint32_t j = 0; j < parent->xtf_child_ncount; j++) {
	if (parent->xtf_child_node[j] == c)
	    return ++parent->xtf_child_count_val[j];
    }
    if (parent->xtf_child_ncount < XO_TFRAME_MAX) {
	uint32_t j = parent->xtf_child_ncount++;
	parent->xtf_child_node[j] = c;
	parent->xtf_child_count_val[j] = 1;
	return 1;
    }
    return 0; /* table full; can't track */
}

/*
 * Like xo_tframe_child_position, but only counts opens where the leading
 * (non-positional) predicates passed.  Stored in separate parallel counters
 * in the parent frame so the two counts never interfere.
 */
static uint32_t
xo_tframe_child_qualified_position (xo_tframe_t *parent, xo_trie_id_t c)
{
    for (uint32_t j = 0; j < parent->xtf_child_qual_ncount; j++) {
	if (parent->xtf_child_qual_node[j] == c)
	    return ++parent->xtf_child_qual_val[j];
    }
    if (parent->xtf_child_qual_ncount < XO_TFRAME_MAX) {
	uint32_t j = parent->xtf_child_qual_ncount++;
	parent->xtf_child_qual_node[j] = c;
	parent->xtf_child_qual_val[j] = 1;
	return 1;
    }
    return 0; /* table full; can't track */
}

/*
 * Return TRUE if the predicate list has a C_INDEX predicate that is NOT
 * the first predicate (meaning there are leading key/test predicates before it).
 * foo[2]           → FALSE (C_INDEX is first, use open-time position)
 * foo[x=1][2]      → TRUE  (C_INDEX is trailing, must use qualified position)
 * foo[2][x=1]      → FALSE (C_INDEX is first)
 */
static int
xo_pred_has_trailing_cindex (xo_filter_t *xfp, xo_xparse_node_id_t pred_id)
{
    int has_leading = FALSE;
    xo_xparse_node_t *xnp;
    for (xo_xparse_node_id_t id = pred_id; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	if (xnp->xn_type != C_PREDICATE)
	    continue;
	xo_xparse_node_id_t cid = xnp->xn_contents;
	int is_cindex = cid
	    && xo_xparse_node(&xfp->xf_xd, cid)->xn_type == C_INDEX;
	if (is_cindex && has_leading)
	    return TRUE;
	if (!is_cindex)
	    has_leading = TRUE;
    }
    return FALSE;
}

static void
xo_tmatch_open (xo_handle_t *xop, xo_filter_t *xfp UNUSED,
		xo_tmatch_t *xtmp, const char *tag, ssize_t tlen)
{
    xo_trie_t *xtp = xtmp->xtm_trie;
    xo_xparse_data_t *xdp = xtp->xt_xd;

    if (xtmp->xtm_depth + 1 >= xtmp->xtm_cap) {
	uint32_t cap = xtmp->xtm_cap * 2;
	xo_tframe_t *p = xo_realloc(xtmp->xtm_stack, cap * sizeof(*p));
	if (p == NULL)
	    return;
	bzero(p + xtmp->xtm_cap, (cap - xtmp->xtm_cap) * sizeof(*p));
	xtmp->xtm_stack = p;
	xtmp->xtm_cap = cap;
    }

    xo_tframe_t *parent = &xtmp->xtm_stack[xtmp->xtm_depth];
    xtmp->xtm_depth += 1;
    xo_tframe_t *frame = &xtmp->xtm_stack[xtmp->xtm_depth];
    bzero(frame, sizeof(*frame));

    xo_dbg(xop, "xo_tmatch_open: depth %u tag '%.*s'", xtmp->xtm_depth, tlen, tag);

    /* Descend from every LIVE parent slot */
    for (uint32_t i = 0; i < parent->xtf_count; i++) {
	if (parent->xtf_state[i] != XTFS_LIVE)
	    continue;
	xo_tnode_t *ptn = &xtp->xt_nodes[parent->xtf_node[i]];
	for (xo_trie_id_t c = ptn->xtn_child;
		     c && frame->xtf_count < XO_TFRAME_MAX;
		     c = xtp->xt_nodes[c].xtn_sibling) {
	    xo_tnode_t *tn = &xtp->xt_nodes[c];
	    const char *nm = xo_xparse_str(xdp, tn->xtn_name);
	    if (!(tn->xtn_flags & XTNF_WILDCARD) &&
		    (nm == NULL || !xo_streqn(nm, tag, tlen)))
		continue;

	    uint32_t position = xo_tframe_child_position(parent, c);
	    uint32_t s = frame->xtf_count++;
	    frame->xtf_node[s] = c;
	    frame->xtf_position[s] = position;

	    if (tn->xtn_pred) {
		frame->xtf_position_cur = position;
		frame->xtf_state[s] =
		    xo_tmatch_try_eager(xop, xfp, frame, tn->xtn_pred,
					tn, xtmp);
	    } else {
		frame->xtf_state[s] = XTFS_LIVE;
		xo_tmatch_record_live(xtmp, frame, tn);
	    }
	}
    }

    /* Re-probe root nodes for relative paths; absolute only at depth 1 */
    for (xo_trie_id_t r = xtp->xt_root; r && frame->xtf_count < XO_TFRAME_MAX;
	 r = xtp->xt_nodes[r].xtn_sibling) {
	xo_tnode_t *tn = &xtp->xt_nodes[r];
	if ((tn->xtn_flags & XTNF_ABSOLUTE) && xtmp->xtm_depth != 1)
	    continue;

	const char *nm = xo_xparse_str(xdp, tn->xtn_name);
	if (!(tn->xtn_flags & XTNF_WILDCARD) &&
		(nm == NULL || !xo_streqn(nm, tag, tlen)))
	    continue;

	/* Avoid duplicating a node already added via parent descent */
	int dup = FALSE;
	for (uint32_t j = 0; j < frame->xtf_count; j++) {
	    if (frame->xtf_node[j] == r) {
		dup = TRUE;
		break;
	    }
	}

	if (dup)
	    continue;

	uint32_t position = xo_tframe_child_position(parent, r);
	uint32_t s = frame->xtf_count++;
	frame->xtf_node[s] = r;
	frame->xtf_position[s] = position;

	if (tn->xtn_pred) {
	    frame->xtf_position_cur = position;
	    frame->xtf_state[s] =
		xo_tmatch_try_eager(xop, xfp, frame, tn->xtn_pred, tn, xtmp);
	} else {
	    frame->xtf_state[s] = XTFS_LIVE;
	    xo_tmatch_record_live(xtmp, frame, tn);
	}
    }

    xo_dbg(xop, "xo_tmatch_open: frame %u active [allow %u/deny %u]",
	   frame->xtf_count, xtmp->xtm_allow, xtmp->xtm_deny);
}

static void
xo_tmatch_close (xo_handle_t *xop, xo_filter_t *xfp UNUSED,
		 xo_tmatch_t *xtmp, const char *tag UNUSED, ssize_t tlen UNUSED)
{
    if (xtmp->xtm_depth == 0)
	return;

    xo_tframe_t *frame = &xtmp->xtm_stack[xtmp->xtm_depth];
    xtmp->xtm_allow -= frame->xtf_allow_delta;
    xtmp->xtm_deny  -= frame->xtf_deny_delta;
    xo_tframe_free_keys(frame);
    xo_tframe_free_attrs(frame);

    xo_dbg(xop, "xo_tmatch_close: depth %u [allow %u/deny %u]",
	   xtmp->xtm_depth, xtmp->xtm_allow, xtmp->xtm_deny);

    xtmp->xtm_depth -= 1;
}

/*
 * xo_filter_pred_eval and xo_tmatch_key reference xo_eval_value_t and
 * other types defined later; they are placed after xo_filter_pred_eval.
 */
static xo_filter_status_t xo_tmatch_key(xo_handle_t *, xo_filter_t *,
					 xo_tmatch_t *, const char *,
					 xo_ssize_t, const char *, xo_ssize_t);
static xo_filter_status_t xo_tmatch_attr(xo_handle_t *, xo_filter_t *,
					  xo_tmatch_t *, const char *,
					  xo_ssize_t, const char *, xo_ssize_t);

/*
 * Add a filter (xpath) to our filtering mechanism
 */
static int
xo_filter_op_add_one (xo_handle_t *xop, const char *input)
{
    xo_filter_t *xfp = xo_get_filter_data(xop, TRUE);
    if (xfp == NULL)
	return -1;

    xo_xparse_data_t *xdp = xo_filter_xparse_data(xop, xfp);
    int start = xdp->xd_paths_cur;

    int rc = xo_xparse_parse_string(xop, xdp, input);

    if (rc == 0) {
	static int unsupported_tokens[] = {
	    L_DOTDOT, L_DOTDOTDOT, L_DOT,
	    K_COMMENT, K_ID, K_KEY, K_NODE,
	    K_PROCESSING_INSTRUCTION, K_TEXT,
	    T_AXIS_NAME, T_VAR, M_SEQUENCE, C_DESCENDANT,
	    C_TEST, C_UNION, C_NESTED_PREDICATES, C_PREDICATE_PATHS,
	    0
	};

	rc = xo_xpath_feature_warn_since(NULL, xdp, start,
					 unsupported_tokens, "");
    }

    if (rc)
	return -1;

    /* Recompile the trie from all expressions accumulated so far */
    xo_trie_free(xfp->xf_trie);
    xo_tmatch_cleanup(&xfp->xf_tmatch);

    xfp->xf_trie = xo_trie_compile(xop, xdp);
    if (xfp->xf_trie == NULL)
	return -1;

    if (xo_tmatch_init(xop, &xfp->xf_tmatch, xfp->xf_trie) < 0) {
	xo_trie_free(xfp->xf_trie);
	xfp->xf_trie = NULL;
	return -1;
    }

    return 0;
}

static xo_filter_status_t
xo_filter_op_get_status (xo_handle_t *xop UNUSED, xo_filter_t *xfp)
{
    return xfp->xf_status;
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
xo_filter_change_status (xo_handle_t *xop UNUSED, xo_filter_t *xfp,
			 const char *op UNUSED,
			 const char *tag UNUSED, ssize_t tlen UNUSED)
{
    const char *why UNUSED;
    int rc;

    /* No filters means always allow */
    if (xfp == NULL || xfp->xf_xd.xd_paths_cur == 0) {
	why = "no-filters";
	rc = XO_STATUS_FULL;

    } else if (xfp->xf_tmatch.xtm_deny) {
	why = "deny-is-set";
	rc = XO_STATUS_TRACK;		/* No means no */

    } else if (xfp->xf_tmatch.xtm_allow) {
	why = "allow-is-set";
	rc = XO_STATUS_FULL;

    } else if (xfp->xf_xd.xd_flags & XDF_ALL_NOTS) {
	why = "all-nots";
	rc = XO_STATUS_FULL;

    } else {
	/*
	 * No active matches mean we're tracking; never DEAD in trie
	 * mode since the trie always re-probes root nodes on each
	 * open.  If any slot in the current frame is waiting for a
	 * non-key predicate field, use PRED so all sibling content is
	 * kept tentatively.
	 */
	xo_tmatch_t *xtmp = &xfp->xf_tmatch;
	if (xtmp->xtm_depth > 0) {
	    xo_tframe_t *frame = &xtmp->xtm_stack[xtmp->xtm_depth];
	    for (uint32_t i = 0; i < frame->xtf_count; i++) {
		if (frame->xtf_state[i] == XTFS_PRED) {
		    why = "pred-pending";
		    rc = XO_STATUS_PRED;
		    goto done_status;
		}
	    }
	}

	why = "default-to-no";
	rc = XO_STATUS_TRACK;
    done_status:;
    }

    if (tlen == 0)		/* Avoid NULL deref */
	tag = "";

    XO_DBG(xop, "xo_filter_update_status (%s%s%.*s) returns %s/%d "
	   "why: %s (was %s/%d)",
	   op, tlen ? " " : "", tlen, tag,
	   xo_filt_status_name(rc), rc, why,
	   xo_filt_status_name(xfp->xf_status), xfp->xf_status);

    xfp->xf_status = rc;	/* Record new value */

    return rc;
}

/*
 * Open a container/list/instance/field: advance the trie FSM.
 */
static int
xo_filter_open (xo_handle_t *xop, xo_filter_t *xfp,
		const char *tag, ssize_t tlen, const char *type UNUSED)
{
    if (xfp == NULL || xfp->xf_trie == NULL)
	return 0;

    XO_DBG(xop, "filter: open %s: '%.*s'", type, tlen, tag);

    xfp->xf_total_depth += 1;

    xo_tmatch_open(xop, xfp, &xfp->xf_tmatch, tag, tlen);

    xo_filter_change_status(xop, xfp, "open", tag, tlen);

    return xfp->xf_status;
}

static int
xo_filter_op_open_container (xo_handle_t *xop, xo_filter_t *xfp,
			  const char *tag)
{
    return xo_filter_open(xop, xfp, tag, strlen(tag), "container");
}

static int
xo_filter_op_open_instance (xo_handle_t *xop, xo_filter_t *xfp, const char *tag)
{
    return xo_filter_open(xop, xfp, tag, strlen(tag), "list");
}

static int
xo_filter_op_open_field (xo_handle_t *xop, xo_filter_t *xfp,
		      const char *tag, ssize_t  tlen)
{
    return xo_filter_open(xop, xfp, tag, tlen, "field");
}

/*
 * Close a container/list/instance/field: pop the trie FSM frame.
 */
static int
xo_filter_close (xo_handle_t *xop, xo_filter_t *xfp,
		 const char *tag, ssize_t tlen, const char *type UNUSED)
{
    if (xfp == NULL || xfp->xf_trie == NULL)
	return 0;

    if (xfp->xf_depth > 0)
	xfp->xf_depth -= 1;
    if (xfp->xf_total_depth > 0)
	xfp->xf_total_depth -= 1;

    XO_DBG(xop, "filter: close %s: '%.*s'", type, tlen, tag);

    xo_tmatch_close(xop, xfp, &xfp->xf_tmatch, tag, tlen);

    xo_filter_change_status(xop, xfp, "close", tag, tlen);

    return xfp->xf_status;
}

static int
xo_filter_op_close_field (xo_handle_t *xop, xo_filter_t *xfp,
		      const char *tag, ssize_t  tlen)
{
    return xo_filter_close(xop, xfp, tag, tlen, "field");
}

static int
xo_filter_op_close_instance (xo_handle_t *xop, xo_filter_t *xfp,
			  const char *tag)
{
    /*
     * If the instance is still in PRED state at close time, the predicate
     * field was never seen.  Force-resolve treating absent fields as empty
     * string (XPath: absent node → empty nodeset → string("")).
     */
    if (xfp != NULL && xfp->xf_trie != NULL
	    && xfp->xf_status == XO_STATUS_PRED)
	xo_filter_force_resolve_pred(xop, xfp, tag);

    xo_filter_status_t pre_close = xfp ? xfp->xf_status : XO_STATUS_ZERO;
    xo_filter_close(xop, xfp, tag, strlen(tag), "instance");

    /*
     * If force-resolve promoted us to FULL, return FULL even though the
     * frame pop has now decremented xtm_allow back.
     */
    return (pre_close == XO_STATUS_FULL) ? XO_STATUS_FULL
	: (xfp ? xfp->xf_status : XO_STATUS_ZERO);
}

static int
xo_filter_op_close_container (xo_handle_t *xop UNUSED, xo_filter_t *xfp,
			   const char *tag)
{
    return xo_filter_close(xop, xfp, tag, strlen(tag), "container");
}

/*
 * Find the current value of a given key and return it.  Since keys
 * can be added multiple times, we can't short circuit and return the
 * first value, we have to continue and return the _last_ value.
 */
static const char *
xo_filter_key_find (xo_filter_t *xfp UNUSED,
		    xo_tframe_t *framep, const char *tag)
{
    xo_ssize_t off = 0;
    xo_ssize_t len = framep->xtf_keys_len;
    char *cp = framep->xtf_keys;
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

static const char *
xo_filter_attr_find (xo_filter_t *xfp UNUSED,
		     xo_tframe_t *framep, const char *tag)
{
    xo_ssize_t off = 0;
    xo_ssize_t len = framep->xtf_attrs_len;
    char *cp = framep->xtf_attrs;
    const char *match = NULL;

    while (off < len) {
	if (*cp == '\0')
	    break;

	xo_ssize_t klen = strlen(cp);
	if (xo_streq(tag, cp))
	    match = cp + klen + 1;

	xo_ssize_t vlen = strlen(cp + klen + 1);
	xo_ssize_t tlen = klen + 1 + vlen + 1;

	off += tlen;
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
	const char *xevd_str;	    /* If C_STRING or C_DSTRING */
    } xev_data;
} xo_eval_value_t;

#define xev_int64 xev_data.xevd_int64
#define xev_uint64 xev_data.xevd_uint64
#define xev_float xev_data.xevd_float
#define xev_str xev_data.xevd_str

/* Flags for xev_flags: */
#define XEVF_INVALID	(1<<0) /* Expression hierarchy is invalid/broken */
#define XEVF_MISSING	(1<<1) /* A referenced element is missing  */
#define XEVF_UNSUPPORTED (1<<2) /* Token type is not supported */
#define XEVF_FINAL	(1<<3)  /* This is the final answer */

/* C_DSTRING: a C_STRING whose xev_str is malloc'd and owned by this value */
#define C_DSTRING	83

#define XO_EVAL_VALUE_ZERO { .xev_type = C_INT64, .xev_flags = 0 }
#define XO_EVAL_VALUE_FLOAT { .xev_type = C_FLOAT, .xev_flags = 0 }
#define XO_EVAL_VALUE_BOOLEAN_FALSE { .xev_type = C_BOOLEAN }
#define XO_EVAL_VALUE_BOOLEAN_TRUE { .xev_type = C_BOOLEAN, .xev_int64 = 1 }
#define XO_EVAL_VALUE_INVALID { .xev_type = M_ERROR, .xev_flags = XEVF_INVALID }
#define XO_EVAL_VALUE_MISSING {  .xev_flags = XEVF_MISSING }
#define XO_EVAL_VALUE_UNSUPPORTED {  .xev_flags = XEVF_UNSUPPORTED }

static xo_eval_value_t xo_filter_pred_eval(xo_handle_t *, xo_filter_t *,
					   xo_tframe_t *, xo_xparse_node_id_t);
static int xo_eval_cast_boolean(xo_handle_t *, xo_eval_value_t);

#define XO_EVAL_OP_ARGS \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	xo_tframe_t *framep UNUSED, \
	xo_xparse_node_t *xnp UNUSED, const char *name UNUSED, \
        int indent UNUSED, \
	xo_eval_value_t left UNUSED, xo_eval_value_t right UNUSED

#define XO_EVAL_OP_PASS \
    xop, xfp, framep, xnp, name, indent, left, right

typedef xo_eval_value_t (*xo_eval_op_fn_t)(XO_EVAL_OP_ARGS);

#define XO_EVAL_NODE_ARGS \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	xo_tframe_t *framep UNUSED, \
	xo_xparse_node_t *xnp UNUSED, int indent UNUSED, \
	int argc UNUSED, xo_eval_value_t *argv UNUSED
#define XO_EVAL_NODE_PASS xop, xfp, framep, xnp, indent, argc, argv

typedef xo_eval_value_t (*xo_eval_node_fn_t)(XO_EVAL_NODE_ARGS);

#define XO_EVAL_CALC_ARGS \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	xo_eval_value_t left UNUSED, xo_eval_value_t right UNUSED

typedef xo_eval_value_t (*xo_eval_calc_fn_t)(XO_EVAL_CALC_ARGS);

static inline xo_eval_value_t
xo_eval_value_make (unsigned type, unsigned flags, xo_xparse_node_id_t id)
{
    xo_eval_value_t value = XO_EVAL_VALUE_ZERO;

    value.xev_type = type;
    value.xev_flags = flags;
    value.xev_node = id;

    return value;
}

static inline void
xo_eval_value_free (xo_eval_value_t val)
{
    if (val.xev_type == C_DSTRING && val.xev_str != NULL) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	xo_free((char *) val.xev_str);
#pragma GCC diagnostic pop
    }
}

static inline xo_eval_value_t
xo_eval_value_float (unsigned flags, xo_float_t val)
{
    xo_eval_value_t value = XO_EVAL_VALUE_ZERO;

    value.xev_type = C_FLOAT;
    value.xev_flags = flags;
    value.xev_float = val;

    return value;
}

static inline xo_eval_value_t
xo_eval_value_string (int stype, unsigned flags, const char *str)
{
    xo_eval_value_t value = XO_EVAL_VALUE_ZERO;

    value.xev_type = stype;
    value.xev_flags = flags;
    value.xev_str = str;

    return value;
}

/*
 * C allows structure creation via fields, but doesn't support directly
 * assigning them, like either of these two lines:
 *      value = { .xev_flags = XEVF_MISSING };
 *      return { .xev_type = M_ERROR };
 * So we make these annoying inline functions where needed.
 */
static inline xo_eval_value_t
xo_eval_value_invalid (void)
{
    xo_eval_value_t value = XO_EVAL_VALUE_INVALID;
    return value;
}

static inline xo_eval_value_t
xo_eval_value_missing (void)
{
    xo_eval_value_t value = XO_EVAL_VALUE_MISSING;
    return value;
}

static inline xo_eval_value_t
xo_eval_value_boolean_false (void)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;
    return value;
}

static inline xo_eval_value_t
xo_eval_value_boolean_true (void)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_TRUE;
    return value;
}

#if 0
static inline xo_eval_value_t
xo_eval_value_unsupported (void)
{
    xo_eval_value_t value = XO_EVAL_VALUE_UNSUPPORTED;
    return value;
}
#endif

/* Forward decl */
static xo_eval_value_t
xo_eval (xo_handle_t *xop, xo_filter_t *xfp, xo_tframe_t *framep,
	 const char *pname, int indent,
	 xo_xparse_node_id_t id, xo_eval_op_fn_t op_fn);

static xo_eval_value_t
xo_eval_make_number (xo_handle_t *xop, const char *str)
{
    xo_eval_value_t value = XO_EVAL_VALUE_ZERO;
    char *ep;

    if (str && str[0] == '0' && str[1] == 'x') {
	uint64_t uval = strtoull(str, &ep, 16);
	if (ep && *ep == '\0') {
	    value = xo_eval_value_make(C_UINT64, 0, 0);
	    value.xev_uint64 = uval;
	    return value;
	}
    }

    int64_t ival = strtoll(str, &ep, 0);
    if (ep && *ep == '\0') {
	value = xo_eval_value_make(C_INT64, 0, 0);
	value.xev_int64 = ival;
	return value;
    }

    xo_float_t fval = strtod(str, &ep);
    if (ep && *ep == '\0') {
	value = xo_eval_value_make(C_FLOAT, 0, 0);
	value.xev_float = fval;
	return value;
    }

    /* We can't give an error, so we just return 0 */
    xo_failure_filter(xop, "invalid number value: '%s'", str);
    value = xo_eval_value_make(C_INT64, 0, 0);
    return value;
}

static xo_eval_value_t
xo_eval_make_number_from_value (xo_handle_t *xop, xo_eval_value_t value)
{
    switch (value.xev_type) {
    case C_DSTRING:
    case C_STRING:;
	return xo_eval_make_number(xop, value.xev_str);

    case C_FLOAT:
    case C_BOOLEAN:
    case C_UINT64:
    case C_INT64:
    default:
	return value;
    }
}

static xo_eval_value_t
xo_eval_position (XO_EVAL_NODE_ARGS)
{
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    uint64_t idx = str ? (uint64_t) strtoul(str, NULL, 10) : 0;
    uint32_t pos = framep->xtf_position_cur;
    xo_eval_value_t value = xo_eval_value_make(C_BOOLEAN, 0, 0);
    value.xev_uint64 = (pos != 0 && pos == idx) ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_number (XO_EVAL_NODE_ARGS)
{
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    return xo_eval_make_number(xop, str);
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
xo_eval_attribute (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = { .xev_flags = 0 };
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    const char *aval = xo_filter_attr_find(xfp, framep, str);
    if (aval) {
	value = xo_eval_value_make(C_STRING, 0, 0);
	value.xev_str = aval;
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
    int is_attr = FALSE;
    xo_xparse_node_id_t id;

    /* We only support a single element or attribute in the path */
    for (id = xnp->xn_contents; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	if (xnp->xn_type == C_ELEMENT) {
	    if (elt == NULL) {
		elt = xnp;
		is_attr = FALSE;
	    } else return xo_eval_value_invalid();
	} else if (xnp->xn_type == C_ATTRIBUTE) {
	    if (elt == NULL) {
		elt = xnp;
		is_attr = TRUE;
	    } else return xo_eval_value_invalid();
	} else if (xnp->xn_type == C_ABSOLUTE) {
	    /* skip */
	} else {
	    xo_failure_filter(xop, "filter: non-element path member (%s)",
			      xo_xparse_fancy_token_name(xnp->xn_type));
	    continue;
	}
    }

    if (elt == NULL)
	return value;

    const char *str = xo_xparse_str(&xfp->xf_xd, elt->xn_str);
    const char *sval = is_attr
	? xo_filter_attr_find(xfp, framep, str)
	: xo_filter_key_find(xfp, framep, str);
    if (sval) {
	value = xo_eval_value_make(C_STRING, 0, 0);
	value.xev_str = sval;
    } else if (xfp->xf_flags & XFSF_FORCE_RESOLVE) {
	/* Absent field = empty string per XPath semantics */
	value = xo_eval_value_make(C_STRING, 0, 0);
	value.xev_str = "";
    } else {
	value.xev_flags |= XEVF_MISSING;
    }

    return value;
}

static int
xo_eval_cast_boolean (xo_handle_t *xop, xo_eval_value_t value)
{
    if (value.xev_type == C_STRING || value.xev_type == C_DSTRING)
	value = xo_eval_make_number_from_value(xop, value);

    switch (value.xev_type) {
    case C_DSTRING:
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
xo_eval_cast_float (xo_handle_t *xop, xo_eval_value_t value)
{
    xo_float_t fval = 0;

    if (value.xev_type == C_STRING || value.xev_type == C_DSTRING)
	value = xo_eval_make_number_from_value(xop, value);

    switch (value.xev_type) {
    case C_DSTRING:
    case C_STRING:;
	const char *str = value.xev_str; /* Should not occur */
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

static char *
xo_eval_cast_string (xo_handle_t *xop UNUSED, xo_eval_value_t value)
{
    char buf[16];
    const char *bp UNUSED = buf;

    switch (value.xev_type) {

    case C_DSTRING:
    case C_STRING:
	bp = value.xev_str;
	break;

    case C_BOOLEAN:
	snprintf(buf, sizeof(buf), "%s", value.xev_int64 ? "true" : "false");
	break;

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

    return strdup(bp);
}

static inline xo_eval_value_t
xo_eval_cast_float_value (xo_handle_t *xop, xo_eval_value_t old)
{
    xo_float_t new = xo_eval_cast_float(xop, old);
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

    case C_DSTRING:
    case C_STRING:
	bp = value.xev_str;
	break;

    case C_BOOLEAN:
	snprintf(buf, sizeof(buf), "%s (%" PRId64 ")",
		 value.xev_int64 ? "true" : "false", value.xev_int64);
	break;

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

    const char *type UNUSED = xo_xparse_fancy_token_name(value.xev_type) ?: "";

    XO_DBG(xop, "%*s%s: type '%s' (%u), flags %#x(%s%s%s%s), "
	   "node %lu, val '%s'",
	   indent, "", title ?: "",
	   type, value.xev_type, value.xev_flags,
	   (value.xev_flags & XEVF_INVALID) ? "+invalid" : "",
	   (value.xev_flags & XEVF_MISSING) ? "+missing" : "",
	   (value.xev_flags & XEVF_UNSUPPORTED) ? "+unsupported" : "",
	   (value.xev_flags & XEVF_FINAL) ? "+final" : "",
	   value.xev_node, bp);
}

#define TYPE_CMP(_a, _b) (((_a) << 16) | (_b))

static xo_eval_value_t
xo_eval_compare (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_ZERO;
    int rc = 0;
    xo_float_t fval;

    xo_eval_dump_value(xop, xfp, left, indent, "compare: left");
    xo_eval_dump_value(xop, xfp, right, indent, "compare: right");

    unsigned left_type = (left.xev_type == C_DSTRING) ? C_STRING : left.xev_type;
    unsigned right_type = (right.xev_type == C_DSTRING) ? C_STRING : right.xev_type;

    switch (TYPE_CMP(left_type, right_type)) {
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

    case TYPE_CMP(C_INT64, C_UINT64):
       if (left.xev_int64 < 0)
           rc = -1;
       else {
           uint64_t lval = (uint64_t) left.xev_int64;
           rc = (lval > right.xev_uint64) ? 1
               : (lval < right.xev_uint64) ? -1 : 0;
       }
       break;

    case TYPE_CMP(C_UINT64, C_INT64):
       if (right.xev_int64 < 0)
           rc = 1;
       else {
           uint64_t rval = (uint64_t) right.xev_int64;
           rc = (left.xev_uint64 > rval) ? 1
               : (left.xev_uint64 < rval) ? -1 : 0;
       }
       break;

    case TYPE_CMP(C_FLOAT, C_FLOAT):
	rc = (left.xev_float > right.xev_float) ? 1
	    : (left.xev_float < right.xev_float) ? -1 : 0;
	break;

    case TYPE_CMP(C_STRING, C_INT64):
	fval = xo_eval_cast_float(xop, left);
	rc = (fval > right.xev_int64) ? 1 : (fval < right.xev_int64) ? -1 : 0;
	break;

    case TYPE_CMP(C_INT64, C_STRING):
	fval = xo_eval_cast_float(xop, right);
	rc = (left.xev_int64 > fval) ? 1 : (left.xev_int64 < fval) ? -1 : 0;
	break;

    case TYPE_CMP(C_STRING, C_UINT64):
	fval = xo_eval_cast_float(xop, left);
	rc = (fval > right.xev_uint64) ? 1 : (fval < right.xev_uint64) ? -1 : 0;
	break;

    case TYPE_CMP(C_UINT64, C_STRING):
	fval = xo_eval_cast_float(xop, right);
	rc = (left.xev_uint64 > fval) ? 1 : (left.xev_uint64 < fval) ? -1 : 0;
	break;

    case TYPE_CMP(C_STRING, C_FLOAT):
	fval = xo_eval_cast_float(xop, left);
	rc = (fval > right.xev_float) ? 1 : (fval < right.xev_float) ? -1 : 0;
	break;

    case TYPE_CMP(C_FLOAT, C_STRING):
	fval = xo_eval_cast_float(xop, right);
	rc = (left.xev_float > fval) ? 1 : (left.xev_float < fval) ? -1 : 0;
	break;

    case TYPE_CMP(C_INT64, C_FLOAT):
    case TYPE_CMP(C_UINT64, C_FLOAT):
	fval = xo_eval_cast_float(xop, left);
	rc = (fval > right.xev_float) ? 1 : (fval < right.xev_float) ? -1 : 0;
	break;

    case TYPE_CMP(C_FLOAT, C_INT64):
    case TYPE_CMP(C_FLOAT, C_UINT64):
	fval = xo_eval_cast_float(xop, right);
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
	if (xop != NULL && (xo_get_flags(xop) & XOF_WARN)) {
	    static const char unk[] = "(unknown)";
	    const char *lname = xo_xparse_token_name(left.xev_type) ?: unk;
	    const char *rname = xo_xparse_token_name(right.xev_type) ?: unk;
	    xo_failure_filter(xop,
		       "filter: eval: unsupported type comparison (%s/%s)",
		       lname, rname);
	}

	return xo_eval_value_invalid();
    }

    value.xev_type = C_INT64;
    value.xev_int64 = rc;

    xo_eval_dump_value(xop, xfp, value, indent, "compare: results");
    return value;
}

static xo_eval_value_t
xo_eval_op_and (XO_EVAL_OP_ARGS)
{
    xo_eval_value_t value = xo_eval_value_make(C_BOOLEAN, 0, 0);

    int bool = xo_eval_cast_boolean(xop, left);
    if (!bool && !(left.xev_flags & XEVF_MISSING)) {
	value.xev_int64 = 0;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    bool = xo_eval_cast_boolean(xop, right);
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

    int bool = xo_eval_cast_boolean(xop, left);
    if (bool) {
	value.xev_int64 = 1;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    bool = xo_eval_cast_boolean(xop, right);
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
    xo_eval_value_t lfloat = xo_eval_cast_float_value(xop, left);
    xo_eval_value_t rfloat = xo_eval_cast_float_value(xop, right);

    xo_eval_value_t result = calc_fn(xop, xfp, lfloat, rfloat);

    if (XO_HAS_DEBUG(xop)) {
	xo_eval_dump_value(xop, xfp, lfloat, indent, "eval_calc: left");
	xo_eval_dump_value(xop, xfp, rfloat, indent, "eval_calc: right");
	char nbuf[128];
	snprintf(nbuf, sizeof(nbuf), "eval_calc: %s", name);

	xo_eval_dump_value(xop, xfp, result, indent, nbuf);
    }

    return result;
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
    if (right.xev_float == 0)
	return xo_eval_value_invalid();

    left.xev_float /= right.xev_float;
    return left;
}

static xo_eval_value_t
xo_eval_op_div (XO_EVAL_OP_ARGS)
{
    return xo_eval_calc(XO_EVAL_OP_PASS,
			       xo_eval_calc_div);
}

static xo_eval_value_t
xo_eval_calc_mul (XO_EVAL_CALC_ARGS)
{
    left.xev_float *= right.xev_float;
    return left;
}

static xo_eval_value_t
xo_eval_op_mul (XO_EVAL_OP_ARGS)
{
    return xo_eval_calc(XO_EVAL_OP_PASS,
			       xo_eval_calc_mul);
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
xo_eval_not (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value;

    /* We only support a single element in the path, which must be a key */
    value = xo_eval(xop, xfp, framep, "arguments", indent,
                       xnp->xn_contents, NULL);
    xo_eval_dump_value(xop, xfp, value, indent, "xo_eval_not");

    if (value.xev_flags & XEVF_MISSING)
	return value;

    int bool = xo_eval_cast_boolean(xop, value);
    xo_eval_value_free(value);
    value.xev_type = C_BOOLEAN;
    value.xev_int64 = bool ? 0 : 1; /* Perform the 'not' */
    return value;
}

/*
 * Look at a arguments to a function and return the count
 */
static int UNUSED
xo_eval_argument_count (XO_EVAL_NODE_ARGS)
{
    xo_xparse_node_id_t id;
    int count = 0;

    for (id = xnp->xn_contents; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	count += 1;
    }

    return count;
}

/*
 * Evaluate the arguments to a function, filling in an array with
 * their values.
 */
static int
xo_eval_arguments (XO_EVAL_NODE_ARGS,
		   int nargs, xo_eval_value_t *argp)
{
    xo_xparse_node_id_t id;
    xo_eval_value_t value;

    for (id = xnp->xn_contents; id && nargs > 0; id = xnp->xn_next, nargs--) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);

	if (XO_HAS_DEBUG(xop))
	    xo_xparse_dump_node(&xfp->xf_xd, id, indent);

	value = xo_eval(xop, xfp, framep, "arguments", indent + XO_INDENT,
			id, NULL);
	xo_eval_dump_value(xop, xfp, value, XO_INDENT,
			   "xo_eval_argument: working");
	*argp++ = value;
    }

    for (; nargs > 0; nargs--)
	*argp++ = xo_eval_value_invalid();

    return (xnp && xnp->xn_next);
}

static void
xo_eval_arguments_free (XO_EVAL_NODE_ARGS)
{
    for (int i = 0; i < argc; i++)
	xo_eval_value_free(argv[i]);
}

static xo_eval_value_t
xo_eval_func_starts_with (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;

    char *base = xo_eval_cast_string(xop, argv[0]);
    char *start = xo_eval_cast_string(xop, argv[1]);
    XO_DBG(xop, "starts_with: '%s' '%s'", base ?: "", start ?: "");

    if (base && start && strncmp(base, start, strlen(start)) == 0)
	value.xev_int64 = TRUE;

    if (base)
	xo_free(base);
    if (start)
	xo_free(start);

    return value;
}

static xo_eval_value_t
xo_eval_func_ends_with (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;

    char *base = xo_eval_cast_string(xop, argv[0]);
    char *start = xo_eval_cast_string(xop, argv[1]);
    XO_DBG(xop, "ends_with: '%s' '%s'", base ?: "", start ?: "");

    if (base && start) {
	int blen = strlen(base);
	int slen = strlen(start);

	if (strncmp(base + blen - slen, start, strlen(start)) == 0)
	    value.xev_int64 = TRUE;
    }

    if (base)
	xo_free(base);
    if (start)
	xo_free(start);

    return value;
}

static xo_eval_value_t
xo_eval_func_true (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_TRUE;
    return value;
}

static xo_eval_value_t
xo_eval_func_false (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;
    return value;
}

static xo_eval_value_t
xo_eval_func_boolean (XO_EVAL_NODE_ARGS)
{
    int bool = xo_eval_cast_boolean(xop, argv[0]);

    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;
    value.xev_int64 = bool ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_func_string (XO_EVAL_NODE_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    return xo_eval_value_string(C_DSTRING, 0, str);
}

static xo_eval_value_t
xo_eval_func_normalize_space (XO_EVAL_NODE_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    const char *p = str ?: "";

    char *out = xo_realloc(NULL, strlen(p) + 1);
    if (out == NULL) {
	xo_free(str);
	xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
	result.xev_str = strdup("");
	return result;
    }

    char *q = out;

    while (isspace((unsigned char) *p))	/* strip leading whitespace */
	p += 1;

    while (*p) {
	if (isspace((unsigned char) *p)) {
	    *q++ = ' ';
	    while (isspace((unsigned char) *p))
		p += 1;
	} else {
	    *q++ = *p++;
	}
    }

    if (q > out && q[-1] == ' ')	/* strip trailing whitespace */
	q -= 1;
    *q = '\0';

    xo_free(str);

    xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
    result.xev_str = out;
    return result;
}

static xo_eval_value_t
xo_eval_func_not (XO_EVAL_NODE_ARGS)
{
    int bool = xo_eval_cast_boolean(xop, argv[0]);
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;
    value.xev_int64 = bool ? 0 : 1;
    return value;
}

static xo_eval_value_t
xo_eval_func_ceiling (XO_EVAL_NODE_ARGS)
{
    xo_float_t fval = xo_eval_cast_float(xop, argv[0]);
    fval = ceil(fval);
    return xo_eval_value_float(0, fval);
}

static xo_eval_value_t
xo_eval_func_floor (XO_EVAL_NODE_ARGS)
{
    xo_float_t fval = xo_eval_cast_float(xop, argv[0]);
    fval = floor(fval);
    return xo_eval_value_float(0, fval);
}

static xo_eval_value_t
xo_eval_func_substring_before (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = xo_eval_value_make(C_STRING, 0, 0);

    char *haystack = xo_eval_cast_string(xop, argv[0]);
    char *needle = xo_eval_cast_string(xop, argv[1]);
    char *found = (haystack && needle) ? strstr(haystack, needle) : NULL;

    if (found) {
	value.xev_type = C_DSTRING;
	value.xev_str = strndup(haystack, found - haystack);
    } else {
	value.xev_str = "";
    }

    xo_free(haystack);
    xo_free(needle);
    return value;
}

static xo_eval_value_t
xo_eval_func_substring_after (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = xo_eval_value_make(C_STRING, 0, 0);

    char *haystack = xo_eval_cast_string(xop, argv[0]);
    char *needle = xo_eval_cast_string(xop, argv[1]);
    char *found = (haystack && needle) ? strstr(haystack, needle) : NULL;

    if (found) {
	const char *after = found + strlen(needle);
	value.xev_type = C_DSTRING;
	value.xev_str = strdup(after);
    } else {
	value.xev_str = "";
    }

    xo_free(haystack);
    xo_free(needle);
    return value;
}

static xo_eval_value_t
xo_eval_func_choose (XO_EVAL_NODE_ARGS)
{
    /* Evaluate the condition (first arg) */
    xo_xparse_node_id_t cond_id = xnp->xn_contents;
    xnp = xo_xparse_node(&xfp->xf_xd, cond_id);

    xo_eval_value_t cond = xo_eval(xop, xfp, framep, "choose-cond",
				   indent + XO_INDENT, cond_id, NULL);
    if (cond.xev_flags & XEVF_MISSING) {
	xo_eval_value_free(cond);
	return xo_eval_value_missing();
    }

    int bool = xo_eval_cast_boolean(xop, cond);
    xo_eval_value_free(cond);

    /* Locate the then and else node ids */
    xo_xparse_node_id_t then_id = xnp->xn_next;
    xnp = xo_xparse_node(&xfp->xf_xd, then_id);
    xo_xparse_node_id_t else_id = xnp->xn_next;

    xo_xparse_node_id_t branch_id = bool ? then_id : else_id;
    return xo_eval(xop, xfp, framep, bool ? "choose-then" : "choose-else",
		   indent + XO_INDENT, branch_id, NULL);
}

static xo_eval_value_t
xo_eval_func_choose2 (XO_EVAL_NODE_ARGS)
{
    xo_xparse_node_id_t first_id = xnp->xn_contents;
    xnp = xo_xparse_node(&xfp->xf_xd, first_id);
    xo_xparse_node_id_t second_id = xnp->xn_next;

    xo_eval_value_t value = xo_eval(xop, xfp, framep, "choose2-first",
				    indent + XO_INDENT, first_id, NULL);
    if (!(value.xev_flags & XEVF_MISSING) && xo_eval_cast_boolean(xop, value))
	return value;

    xo_eval_value_free(value);
    return xo_eval(xop, xfp, framep, "choose2-second",
		   indent + XO_INDENT, second_id, NULL);
}

static xo_eval_value_t
xo_eval_func_concat (XO_EVAL_NODE_ARGS)
{
    xo_buffer_t buf;
    xo_buf_init(&buf);

    xo_xparse_node_id_t id;
    int count = 0;

    for (id = xnp->xn_contents; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	count += 1;

	xo_eval_value_t value = xo_eval(xop, xfp, framep, "concat-arg",
					indent + XO_INDENT, id, NULL);
	if (value.xev_flags & XEVF_MISSING) {
	    xo_eval_value_free(value);
	    xo_buf_cleanup(&buf);
	    return xo_eval_value_missing();
	}
	char *str = xo_eval_cast_string(xop, value);
	if (str) {
	    xo_buf_append(&buf, str, strlen(str));
	    xo_free(str);
	}
	xo_eval_value_free(value);
    }

    if (count < 2) {
	xo_failure_filter(xop, "function 'concat' requires "
			  "at least 2 arguments, got %d",
			  count);
	xo_buf_cleanup(&buf);
	xo_eval_value_t false_val = XO_EVAL_VALUE_BOOLEAN_FALSE;
	return false_val;
    }

    xo_buf_append(&buf, "", 1);	/* null terminator */
    xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
    result.xev_str = strdup(xo_buf_data(&buf, 0));
    xo_buf_cleanup(&buf);
    return result;
}

static xo_eval_value_t
xo_eval_func_contains (XO_EVAL_NODE_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;

    char *haystack = xo_eval_cast_string(xop, argv[0]);
    char *needle = xo_eval_cast_string(xop, argv[1]);
    XO_DBG(xop, "contains: '%s' '%s'", haystack ?: "", needle ?: "");

    if (haystack && needle && strstr(haystack, needle) != NULL)
	value.xev_int64 = TRUE;

    if (haystack)
	xo_free(haystack);
    if (needle)
	xo_free(needle);

    return value;
}

static xo_eval_value_t
xo_eval_func_number (XO_EVAL_NODE_ARGS)
{
    return xo_eval_make_number_from_value(xop, argv[0]);
}

static xo_eval_value_t
xo_eval_func_round (XO_EVAL_NODE_ARGS)
{
    xo_float_t fval = xo_eval_cast_float(xop, argv[0]);
    fval = round(fval);
    return xo_eval_value_float(0, fval);
}

static xo_eval_value_t
xo_eval_func_string_length (XO_EVAL_NODE_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    xo_float_t len = str ? strlen(str) : 0;
    xo_free(str);
    return xo_eval_value_float(0, len);
}

/*
 * sum(a, b, ...) — sum all arguments converted to numbers.
 */
static xo_eval_value_t
xo_eval_func_sum (XO_EVAL_NODE_ARGS)
{
    xo_float_t total = 0;

    for (xo_xparse_node_id_t id = xnp->xn_contents; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);

	xo_eval_value_t value = xo_eval(xop, xfp, framep, "sum-arg",
					indent + XO_INDENT, id, NULL);
	if (value.xev_flags & XEVF_MISSING) {
	    xo_eval_value_free(value);
	    return xo_eval_value_missing();
	}
	total += xo_eval_cast_float(xop, value);
	xo_eval_value_free(value);
    }

    return xo_eval_value_float(0, total);
}

/*
 * translate(string, from, to) — replace each char in string that appears
 * in from with the corresponding char in to; delete chars with no mapping.
 */
static xo_eval_value_t
xo_eval_func_translate (XO_EVAL_NODE_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    char *from = xo_eval_cast_string(xop, argv[1]);
    char *to = xo_eval_cast_string(xop, argv[2]);

    if (str == NULL || from == NULL) {
	xo_free(str);
	xo_free(from);
	xo_free(to);
	xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
	result.xev_str = strdup("");
	return result;
    }

    size_t from_len = strlen(from);
    size_t to_len = to ? strlen(to) : 0;
    char *out = xo_realloc(NULL, strlen(str) + 1);

    if (out == NULL) {
	xo_free(str);
	xo_free(from);
	xo_free(to);
	xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
	result.xev_str = strdup("");
	return result;
    }

    char *q = out;
    for (const char *p = str; *p; p++) {
	const char *found = memchr(from, (unsigned char) *p, from_len);
	if (found == NULL) {
	    *q++ = *p;		/* not in from: copy as-is */
	} else {
	    size_t idx = found - from;
	    if (idx < to_len)
		*q++ = to[idx];	/* replacement char */
	    /* else: no mapping — delete the character */
	}
    }
    *q = '\0';

    xo_free(str);
    xo_free(from);
    xo_free(to);

    xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
    result.xev_str = out;
    return result;
}

/*
 * Map between names and numbers and functions, searchable by string
 * name.
 */
typedef uint32_t xo_eval_func_flags_t;

#define XEFF_NO_EVAL	(1<<0)	/* Function evaluates its own args (no infra) */

typedef struct xo_eval_func_map_s {
    xo_eval_node_fn_t xfm_func;	/* The function that implements the logic */
    const char *xfm_name;	/* Name (e.g. "plus") */
    xo_eval_func_flags_t xfm_flags; /* Flags (XEFF_*) */
    int xfm_nargs;		/* Required arg count; -1 means don't checks */
} xo_eval_func_map_t;

xo_eval_func_map_t xo_eval_functions[] = {
    { xo_eval_func_boolean, "boolean", 0, 1 },
    { xo_eval_func_ceiling, "ceiling", 0, 1 },
    { xo_eval_func_choose, "choose", XEFF_NO_EVAL, 3 },
    { xo_eval_func_choose2, "choose2", XEFF_NO_EVAL, 2 },
    { xo_eval_func_concat, "concat", XEFF_NO_EVAL, -1 },
    { xo_eval_func_contains, "contains", 0, 2 },
    { xo_eval_func_ends_with, "ends-with", 0, 2 },
    { xo_eval_func_false, "false", 0, 0 },
    { xo_eval_func_floor, "floor", 0, 1 },
    { xo_eval_func_normalize_space, "normalize-space", 0, 1 },
    { xo_eval_func_not, "not", 0, 1 },
    { xo_eval_func_number, "number", 0, 1 },
    { xo_eval_func_round, "round", 0, 1 },
    { xo_eval_func_starts_with, "starts-with", 0, 2 },
    { xo_eval_func_string, "string", 0, 1 },
    { xo_eval_func_string_length, "string-length", 0, 1 },
    { xo_eval_func_substring_after, "substring-after", 0, 2 },
    { xo_eval_func_substring_before, "substring-before", 0, 2 },
    { xo_eval_func_sum, "sum", XEFF_NO_EVAL, -1 },
    { xo_eval_func_translate, "translate", 0, 3 },
    { xo_eval_func_true, "true", 0, 0 },

    { NULL, NULL, 0, 0 }
};

static xo_eval_func_map_t *
xo_eval_find_func (xo_eval_func_map_t *map, const char *name)
{
    for ( ; map && map->xfm_func; map++)
	if (map->xfm_name && xo_streq(map->xfm_name, name))
	    return map;

    return NULL;
}

static xo_eval_value_t
xo_eval_function (XO_EVAL_NODE_ARGS)
{
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    if (str == NULL) {
	xo_failure_filter(xop, "unknown function in filter expression");
	return xo_eval_value_invalid();
    }

    xo_eval_func_map_t *entry = xo_eval_find_func(xo_eval_functions, str);
    if (entry == NULL) {
	xo_failure_filter(xop, "unknown function in filter expression: '%s'",
			  str);
	return xo_eval_value_invalid();
    }

    int fn_argc = xo_eval_argument_count(XO_EVAL_NODE_PASS);

    if (entry->xfm_nargs >= 0 && fn_argc != entry->xfm_nargs) {
	xo_failure_filter(xop, "function '%s' requires %d argument(s), got %d",
			  str, entry->xfm_nargs, fn_argc);
	xo_eval_value_t false_val = XO_EVAL_VALUE_BOOLEAN_FALSE;
	return false_val;
    }

    xo_eval_value_t value;

    if (entry->xfm_flags & XEFF_NO_EVAL) {
	/* Function manages its own argument evaluation */
	value = entry->xfm_func(xop, xfp, framep, xnp, indent, 0, NULL);
    } else {
	/* Infra: allocate, evaluate all args, call, then free */
	xo_eval_value_t *fn_argv = fn_argc
	    ? xo_realloc(NULL, fn_argc * sizeof(*fn_argv)) : NULL;

	xo_eval_arguments(XO_EVAL_NODE_PASS, fn_argc, fn_argv);

	if (entry->xfm_nargs > 0) {
	    for (int i = 0; i < fn_argc; i++) {
		if (fn_argv[i].xev_flags & XEVF_MISSING) {
		    xo_eval_arguments_free(xop, xfp, framep, xnp, indent,
					   fn_argc, fn_argv);
		    xo_free(fn_argv);
		    return xo_eval_value_missing();
		}
	    }
	}

	value = entry->xfm_func(xop, xfp, framep, xnp, indent,
				fn_argc, fn_argv);

	xo_eval_arguments_free(xop, xfp, framep, xnp, indent, fn_argc, fn_argv);
	xo_free(fn_argv);
    }

    xo_eval_dump_value(xop, xfp, value, indent, str);

    return value;
}

static xo_eval_value_t
xo_eval (xo_handle_t *xop, xo_filter_t *xfp, xo_tframe_t *framep,
	 const char *pname, int indent,
	 xo_xparse_node_id_t id, xo_eval_op_fn_t op_fn)
{
    xo_eval_value_t value = xo_eval_value_invalid();
    xo_eval_value_t last = XO_EVAL_VALUE_ZERO;
    int first = 1;

    xo_xparse_node_t *xnp;
    xo_eval_node_fn_t node_fn = NULL;
    xo_eval_op_fn_t nested_op_fn = NULL;

    for (; id; id = xnp->xn_next) {
	xo_xparse_dump_one_node(&xfp->xf_xd, id, indent, "eval (loop): ");

	xnp = xo_xparse_node(&xfp->xf_xd, id);
	const char *cname = xo_xparse_token_name(xnp->xn_type);

	node_fn = NULL;
	nested_op_fn = NULL;

	switch (xnp->xn_type) {

	case C_ATTRIBUTE:
	    node_fn = xo_eval_attribute;
	    break;

	case C_PATH:
	    node_fn = xo_eval_path;
	    break;

	case K_AND:
	    nested_op_fn = xo_eval_op_and;
	    break;

	case K_DIV:
	    nested_op_fn = xo_eval_op_div;
	    break;

	case L_STAR:
	    nested_op_fn = xo_eval_op_mul;
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

	case C_NOT:
	    node_fn = xo_eval_not;
	    break;

	case L_NOTEQUALS:
	    nested_op_fn = xo_eval_op_notequals;
	    break;

	case T_FUNCTION_NAME:
	    node_fn = xo_eval_function;
	    break;

	case C_INDEX:
	    node_fn = xo_eval_position;
	    break;

	case T_NUMBER:
	    node_fn = xo_eval_number;
	    break;

	case T_QUOTED:
	    node_fn = xo_eval_quoted;
	    break;

	case C_EXPR:
	    if (xnp->xn_contents)
		value = xo_eval(xop, xfp, framep, pname, indent + XO_INDENT,
				xnp->xn_contents, NULL);
	    break;

	default:		/* For now; should be XEVF_UNSUPPORTED */
	    xo_failure_filter(xop, "filter: unhandle type: '%s'",
			      xo_xparse_fancy_token_name(xnp->xn_type));

#if 1
	    /*
	     * Probably the 'right' thing to, but really should be handled
	     * individually
	     */
	    if (xnp->xn_contents)
		value = xo_eval(xop, xfp, framep, pname, indent + XO_INDENT,
				xnp->xn_contents, NULL);
#endif
	}

	if (node_fn)
	    value = node_fn(xop, xfp, framep, xnp, indent + XO_INDENT, 0, NULL);
	else if (nested_op_fn)
	    value = xo_eval(xop, xfp, framep, cname, indent + XO_INDENT,
			    xnp->xn_contents, nested_op_fn);

	if (first) {
	    first = 0;
	    last = value;

	} else if (op_fn) {
	    /*
	     * If either operand is 'missing', we just delay the operation
	     * til the missing piece arrives
	     */
	    if ((last.xev_flags & XEVF_MISSING)
		|| (value.xev_flags & XEVF_MISSING)) {
		xo_eval_value_free(last);
		xo_eval_value_free(value);
		value = xo_eval_value_missing();

	    } else {
		xo_eval_value_t result = op_fn(xop, xfp, framep, xnp, pname,
					       indent + XO_INDENT, last, value);
		xo_eval_value_free(last);
		xo_eval_value_free(value);
		value = result;
	    }
	}

	xo_eval_dump_value(xop, xfp, value, indent, "eval (bottom)");

	/* If we're not combining output values, then we're done */
	if (op_fn == NULL)
	    break;

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
xo_filter_pred_eval (xo_handle_t *xop, xo_filter_t *xfp,
		     xo_tframe_t *framep, xo_xparse_node_id_t pred_id)
{
    int have_missing = FALSE;

    xo_xparse_dump_one_node(&xfp->xf_xd, pred_id, 0, "eval: ");

    xo_xparse_node_id_t id;
    xo_xparse_node_t *xnp;

    for (id = pred_id; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);

	if (XO_HAS_DEBUG(xop))
	    xo_xparse_dump_node(&xfp->xf_xd, id, XO_INDENT);

	if (xnp->xn_type != C_PREDICATE) /* Can't eval anything else */
	    continue;

	xo_eval_value_t pv = xo_eval(xop, xfp, framep, "top", XO_INDENT,
				     xnp->xn_contents, NULL);
	xo_eval_dump_value(xop, xfp, pv, XO_INDENT,
			   "xo_filter_pred_eval: working");

	if (pv.xev_flags & XEVF_MISSING) {
	    /* Key not yet seen — can't resolve this predicate yet */
	    have_missing = TRUE;
	    xo_eval_value_free(pv);
	    continue;
	}

	int passes = xo_eval_cast_boolean(xop, pv);
	xo_eval_value_free(pv);

	if (!passes) {
	    /* Definitive FALSE: AND short-circuits regardless of other preds */
	    xo_eval_dump_value(xop, xfp, xo_eval_value_boolean_false(),
			       0, "xo_filter_pred_eval: final");
	    return xo_eval_value_boolean_false();
	}
    }

    if (have_missing) {
	xo_eval_dump_value(xop, xfp, xo_eval_value_missing(),
			   0, "xo_filter_pred_eval: final");
	return xo_eval_value_missing();
    }

    xo_eval_dump_value(xop, xfp, xo_eval_value_boolean_true(),
		       0, "xo_filter_pred_eval: final");
    return xo_eval_value_boolean_true();
}

/*
 * Recurse down the complete predicate, seeing if it even wants the
 * tag.
 */
static int
xo_filter_pred_needs (xo_xparse_data_t *xdp, xo_filter_t *xfp,
		      xo_xparse_node_id_t id,
		      const char *tag, xo_ssize_t tlen, int is_attr)
{
    xo_xparse_node_t *xnp;
    xo_xparse_token_t want_type = is_attr ? C_ATTRIBUTE : C_ELEMENT;

    for (; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(xdp, id);
	if (xnp->xn_type == want_type) {
	    const char *str = xo_xparse_str(xdp, xnp->xn_str);
	    xo_ssize_t slen = strlen(str);
	    if (slen == tlen && memcmp(str, tag, slen) == 0)
		return TRUE;
	} else if (xnp->xn_type == C_ABSOLUTE) {
	    /* Nothing to do, just handle the next node (xn_next) */
	}

	if (xnp->xn_contents)
	    if (xo_filter_pred_needs(xdp, xfp, xnp->xn_contents,
				     tag, tlen, is_attr))
		return TRUE;
    }

    return FALSE;
}

/*
 * Eagerly evaluate a predicate at instance-open time with no keys yet.
 * Returns XTFS_LIVE, XTFS_DEAD, or XTFS_PRED (predicate needs key data).
 */
static int
xo_tmatch_try_eager (xo_handle_t *xop, xo_filter_t *xfp,
		     xo_tframe_t *framep, xo_xparse_node_id_t pred,
		     xo_tnode_t *tn, xo_tmatch_t *xtmp)
{
    /*
     * For foo[A][N] (trailing C_INDEX after leading predicates) we cannot
     * evaluate the positional predicate eagerly because the qualified
     * position (counting only instances where A passes) is not yet known.
     * Defer to key-arrival or force-resolve time.
     */
    if (xo_pred_has_trailing_cindex(xfp, pred))
	return XTFS_PRED;

    xo_eval_value_t result = xo_filter_pred_eval(xop, xfp, framep, pred);
    if (result.xev_flags & XEVF_MISSING)
	return XTFS_PRED;
    int live = xo_eval_cast_boolean(xop, result);
    xo_eval_value_free(result);
    if (live) {
	xo_tmatch_record_live(xtmp, framep, tn);
	return XTFS_LIVE;
    }
    return XTFS_DEAD;
}

/*
 * Evaluate only the leading (non-C_INDEX) predicates in the list.
 * Returns MISSING if any leading predicate still needs unseen keys,
 * TRUE/FALSE otherwise.  C_INDEX predicates are skipped entirely.
 */
static xo_eval_value_t
xo_filter_leading_preds_eval (xo_handle_t *xop, xo_filter_t *xfp,
			      xo_tframe_t *framep, xo_xparse_node_id_t pred_id)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_TRUE;
    xo_xparse_node_t *xnp;
    for (xo_xparse_node_id_t id = pred_id; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	if (xnp->xn_type != C_PREDICATE)
	    continue;
	xo_xparse_node_id_t cid = xnp->xn_contents;
	if (cid && xo_xparse_node(&xfp->xf_xd, cid)->xn_type == C_INDEX)
	    continue;  /* skip positional predicates */
	value = xo_eval(xop, xfp, framep, "lead-pred", XO_INDENT, cid, NULL);
	if (value.xev_flags & XEVF_MISSING)
	    return value;
    }
    return value;
}

/*
 * Determine the position to use for C_INDEX evaluation for slot `slot` in
 * `framep`.  If the predicate list has a trailing C_INDEX (i.e. leading
 * key/test predicates precede it), we maintain a separate "qualified"
 * counter in the parent frame that only advances when those leading
 * predicates pass.  Otherwise we fall back to the at-open-time position.
 */
static uint32_t
xo_tmatch_slot_position (xo_handle_t *xop, xo_filter_t *xfp,
			  xo_tmatch_t *xtmp, xo_tframe_t *framep,
			  uint32_t slot, xo_xparse_node_id_t pred_id)
{
    /* Already counted for this slot: return saved qualified position */
    if (framep->xtf_flags[slot] & XTFF_QUAL_COUNTED)
	return framep->xtf_qual_position[slot];

    /* No trailing C_INDEX → use normal open-time position */
    if (!xo_pred_has_trailing_cindex(xfp, pred_id))
	return framep->xtf_position[slot];

    /* Evaluate leading predicates to see if they currently pass */
    xo_eval_value_t lv = xo_filter_leading_preds_eval(xop, xfp, framep, pred_id);
    if (lv.xev_flags & XEVF_MISSING) {
	xo_eval_value_free(lv);
	return framep->xtf_position[slot]; /* not yet decidable */
    }
    int passes = xo_eval_cast_boolean(xop, lv);
    xo_eval_value_free(lv);

    if (!passes)
	return framep->xtf_position[slot]; /* C_INDEX outcome won't matter */

    /* Leading predicates passed: advance the qualified counter in parent frame */
    xo_tframe_t *parentp = xtmp->xtm_depth > 0
	? &xtmp->xtm_stack[xtmp->xtm_depth - 1] : NULL;
    if (parentp == NULL)
	return framep->xtf_position[slot];

    uint32_t qpos = xo_tframe_child_qualified_position(parentp, framep->xtf_node[slot]);
    framep->xtf_qual_position[slot] = qpos;
    framep->xtf_flags[slot] |= XTFF_QUAL_COUNTED;
    return qpos;
}

static xo_filter_status_t
xo_tmatch_key (xo_handle_t *xop, xo_filter_t *xfp, xo_tmatch_t *xtmp,
	       const char *tag, xo_ssize_t tlen,
	       const char *value, xo_ssize_t vlen)
{
    if (xtmp->xtm_depth == 0)
	return xfp->xf_status;

    xo_trie_t *xtp = xtmp->xtm_trie;
    xo_tframe_t *framep = &xtmp->xtm_stack[xtmp->xtm_depth];

    for (uint32_t i = 0; i < framep->xtf_count; i++) {
	if (framep->xtf_state[i] != XTFS_PRED)
	    continue;

	xo_tnode_t *tn = &xtp->xt_nodes[framep->xtf_node[i]];

	if (!xo_filter_pred_needs(&xfp->xf_xd, xfp, tn->xtn_pred, tag, tlen, FALSE))
	    continue;

	xo_tframe_key_add(framep, tag, tlen, value, vlen);

	framep->xtf_position_cur = xo_tmatch_slot_position(xop, xfp, xtmp,
							    framep, i,
							    tn->xtn_pred);
	xo_eval_value_t result =
	    xo_filter_pred_eval(xop, xfp, framep, tn->xtn_pred);

	if (result.xev_flags & XEVF_MISSING)
	    continue;

	int live = xo_eval_cast_boolean(xop, result);
	xo_eval_value_free(result);
	if (live) {
	    framep->xtf_state[i] = XTFS_LIVE;
	    xo_tmatch_record_live(xtmp, framep, tn);
	} else {
	    framep->xtf_state[i] = XTFS_DEAD;
	}
    }

    return xfp->xf_status;
}

static int
xo_filter_op_key (XO_FILTER_KEY_SIGNATURE)
{
    if (xfp == NULL || xfp->xf_trie == NULL)
	return 0;

    XO_DBG(xop, "xo_filter_key: '%.*s' = '%.*s'", tlen, tag, vlen, value);

    xo_filter_status_t rc = xo_tmatch_key(xop, xfp, &xfp->xf_tmatch,
					   tag, tlen, value, vlen);

    xo_filter_change_status(xop, xfp, "key", tag, tlen);

    XO_DBG(xop, "xo_filter_key: '%.*s' = '%.*s' --> status %s",
	   tlen, tag, vlen, value,
	   xo_filt_status_name(xfp->xf_status));

    return rc;
}

static xo_filter_status_t
xo_tmatch_attr (xo_handle_t *xop, xo_filter_t *xfp, xo_tmatch_t *xtmp,
		const char *tag, xo_ssize_t tlen,
		const char *value, xo_ssize_t vlen)
{
    if (xtmp->xtm_depth == 0)
	return xfp->xf_status;

    xo_trie_t *xtp = xtmp->xtm_trie;
    xo_tframe_t *framep = &xtmp->xtm_stack[xtmp->xtm_depth];

    for (uint32_t i = 0; i < framep->xtf_count; i++) {
	if (framep->xtf_state[i] != XTFS_PRED)
	    continue;

	xo_tnode_t *tn = &xtp->xt_nodes[framep->xtf_node[i]];

	if (!xo_filter_pred_needs(&xfp->xf_xd, xfp, tn->xtn_pred,
				  tag, tlen, TRUE))
	    continue;

	xo_tframe_attr_add(framep, tag, tlen, value, vlen);

	framep->xtf_position_cur = xo_tmatch_slot_position(xop, xfp, xtmp,
							    framep, i,
							    tn->xtn_pred);
	xo_eval_value_t result =
	    xo_filter_pred_eval(xop, xfp, framep, tn->xtn_pred);

	if (result.xev_flags & XEVF_MISSING)
	    continue;

	int live = xo_eval_cast_boolean(xop, result);
	xo_eval_value_free(result);
	if (live) {
	    framep->xtf_state[i] = XTFS_LIVE;
	    xo_tmatch_record_live(xtmp, framep, tn);
	} else {
	    framep->xtf_state[i] = XTFS_DEAD;
	}
    }

    return xfp->xf_status;
}

static int
xo_filter_op_attribute (xo_handle_t *xop, xo_filter_t *xfp,
			const char *tag, xo_ssize_t tlen,
			const char *value, xo_ssize_t vlen)
{
    if (xfp == NULL || xfp->xf_trie == NULL)
	return 0;

    XO_DBG(xop, "xo_filter_attribute: '@%.*s' = '%.*s'", tlen, tag, vlen, value);

    xo_filter_status_t rc = xo_tmatch_attr(xop, xfp, &xfp->xf_tmatch,
					    tag, tlen, value, vlen);

    xo_filter_change_status(xop, xfp, "attr", tag, tlen);

    XO_DBG(xop, "xo_filter_attribute: '@%.*s' = '%.*s' --> status %s",
	   tlen, tag, vlen, value,
	   xo_filt_status_name(xfp->xf_status));

    return rc;
}

/* ------------------------------------------------------------- */

/*
 * We use the passthru to pass content through to the encoder
 */
static int
xo_filter_op_passthru (XO_ENCODER_HANDLER_ARGS,
		      xo_encoder_func_t func UNUSED,
 		      struct xo_filter_s *xfp UNUSED)
{
    int rc = 0;
    xo_buffer_t *xbp = bufp;

    XO_DBG(xop, "filter: entering passthru: %s: '%s'%s status: %s/%d",
	   xo_encoder_op_name(op), name ?: "",
	   (flags & XFF_KEY) ? " is-a-key" : "",
	   xo_filt_status_name(xfp->xf_status), xfp->xf_status);

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

    case XO_OP_ATTRIBUTE:	   /* Attribute name/value */
	if (name && value)
	    xo_filter_op_attribute(xop, xfp, name, strlen(name),
				   value, strlen(value));
	break;

    case XO_OP_STRING:		   /* Quoted UTF-8 string */
    case XO_OP_CONTENT:		   /* Other content */
	break;
    }

    rc = func(xop, op, xbp, name, value, private, flags);

    XO_DBG(xop, "filter: leaving passthru: %s: '%s'%s status: %s/%d",
	   xo_encoder_op_name(op), name ?: "",
	   (flags & XFF_KEY) ? " is-a-key" : "",
	   xo_filt_status_name(xfp->xf_status), xfp->xf_status);

    return rc;
}

/*
 * Return TRUE if any XTFS_PRED slot in the current frame references `tag`
 * as a non-attribute element in its predicate.  Used by libxo to decide
 * whether to keep a non-key field's output tentatively in the whiteboard.
 */
static int
xo_filter_op_needs_nonkey_field (XO_FILTER_NEEDS_NONKEY_FIELD_SIGNATURE)
{
    if (xfp == NULL || xfp->xf_trie == NULL)
	return FALSE;

    if (xfp->xf_status != XO_STATUS_TRACK)
	return FALSE;

    xo_tmatch_t *xtmp = &xfp->xf_tmatch;
    if (xtmp->xtm_depth == 0)
	return FALSE;

    xo_trie_t *xtp = xtmp->xtm_trie;
    xo_tframe_t *framep = &xtmp->xtm_stack[xtmp->xtm_depth];

    for (uint32_t i = 0; i < framep->xtf_count; i++) {
	if (framep->xtf_state[i] != XTFS_PRED)
	    continue;
	xo_tnode_t *tn = &xtp->xt_nodes[framep->xtf_node[i]];
	if (xo_filter_pred_needs(&xfp->xf_xd, xfp, tn->xtn_pred, tag, tlen, FALSE))
	    return TRUE;
    }

    return FALSE;
}

/*
 * Force-evaluate all XTFS_PRED slots in the current trie frame, treating
 * any missing (never-seen) field as empty string per XPath semantics.
 * Called from xo_filter_op_close_instance when the predicate field was
 * never emitted inside this instance.
 */
static void
xo_filter_force_resolve_pred (xo_handle_t *xop, xo_filter_t *xfp,
			      const char *tag)
{
    xo_tmatch_t *xtmp = &xfp->xf_tmatch;
    if (xtmp->xtm_depth == 0)
	return;

    xo_tframe_t *framep = &xtmp->xtm_stack[xtmp->xtm_depth];
    xfp->xf_flags |= XFSF_FORCE_RESOLVE;

    for (uint32_t i = 0; i < framep->xtf_count; i++) {
	if (framep->xtf_state[i] != XTFS_PRED)
	    continue;
	xo_tnode_t *tn = &xtmp->xtm_trie->xt_nodes[framep->xtf_node[i]];
	if (tn->xtn_pred == 0) {
	    framep->xtf_state[i] = XTFS_LIVE;
	    xo_tmatch_record_live(xtmp, framep, tn);
	    continue;
	}
	framep->xtf_position_cur = (framep->xtf_flags[i] & XTFF_QUAL_COUNTED)
	    ? framep->xtf_qual_position[i] : framep->xtf_position[i];
	xo_eval_value_t result =
	    xo_filter_pred_eval(xop, xfp, framep, tn->xtn_pred);
	if (!(result.xev_flags & XEVF_MISSING)) {
	    int live = xo_eval_cast_boolean(xop, result);
	    xo_eval_value_free(result);
	    if (live) {
		framep->xtf_state[i] = XTFS_LIVE;
		xo_tmatch_record_live(xtmp, framep, tn);
	    } else {
		framep->xtf_state[i] = XTFS_DEAD;
	    }
	}
    }

    xfp->xf_flags &= ~XFSF_FORCE_RESOLVE;
    xo_filter_change_status(xop, xfp, "close-instance-pred", tag, strlen(tag));
}

/*
 * Buffer a non-key field's value for predicate evaluation, just like
 * xo_filter_op_key does for key fields.  xo_tmatch_key internally gates on
 * xo_filter_pred_needs, so fields not referenced by any predicate are no-ops.
 */
static int
xo_filter_op_pred_field (XO_FILTER_PRED_FIELD_SIGNATURE)
{
    if (xfp == NULL || xfp->xf_trie == NULL)
	return 0;

    if (xfp->xf_status != XO_STATUS_TRACK && xfp->xf_status != XO_STATUS_PRED)
	return xfp->xf_status;

    xo_tmatch_key(xop, xfp, &xfp->xf_tmatch, tag, tlen, value, vlen);

    xo_filter_change_status(xop, xfp, "pred-field", tag, tlen);

    return xfp->xf_status;
}

static xo_filter_ops_t xo_filter_ops_local = {
    XO_FILTER_OPS_VERSION,
    XO_FILTER_OPS_FUNCS
};

int				/* Found via dlsym() */
xo_filter_init (int version, xo_filter_ops_t *ops);

int
xo_filter_init (int version, xo_filter_ops_t *ops)
{
    if (version && version < XO_FILTER_OPS_VERSION)
	return -1;

    memcpy(ops, &xo_filter_ops_local, sizeof(*ops));

    return XO_FILTER_OPS_VERSION;
}

void
xo_filter_setup_test (void)
{
    xo_setup_filter_lib_test(XO_FILTER_OPS_VERSION, &xo_filter_ops_local);

}
