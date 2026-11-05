/*
 * License-Identifier: BSD-2-Clause
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
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <regex.h>
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
#include "xo_utf8.h"
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
    xo_name_id_t xtn_name;      /* Interned element name (backend-opaque id) */
    xo_xparse_node_id_t xtn_pred; /* Predicate subtree root (0=none) */
    xo_trie_id_t xtn_child;	/* First child trie node (0 = leaf) */
    xo_trie_id_t xtn_sibling;	/* Next sibling at this level */
    uint16_t xtn_flags;		/* XTNF_* flags */
    int16_t xtn_import_prec;	/* import precedence of the pattern that set
				   xtn_action; higher wins regardless of
				   priority (XSLT import precedence outranks
				   priority in conflict resolution) */
    uint32_t xtn_action;	/* backend-opaque action id (XTNF_TERMINAL only) */
    double xtn_priority;	/* priority of the pattern that set xtn_action;
				   used to arbitrate when a later insert wants
				   this same node as its own terminal, after
				   import precedence has been compared */
} xo_tnode_t;

#define XTNF_TERMINAL	(1<<0)	/* A complete expression ends here */
#define XTNF_NOT	(1<<1)	/* "not" expression: deny on match */
#define XTNF_ABSOLUTE	(1<<2)	/* Anchored at tree root (leading '/') */
#define XTNF_WILDCARD	(1<<3)	/* Wildcard step ('*'): matches any tag */

/*
 * The compiled trie: a flat node array plus the root sibling chain.
 */
typedef struct xo_trie_s {
    xo_tnode_t *xt_nodes;	/* [1..xt_nodes_count]; slot 0 unused */
    xo_trie_id_t xt_nodes_count;	/* Nodes in use */
    xo_trie_id_t xt_nodes_max;	/* Allocated capacity */
    xo_trie_id_t xt_root;	/* First root-level sibling */
    xo_xparse_data_t *xt_xd;	/* Parse data (for string lookup) */
    xo_filter_data_t *xt_data;	/* Data context (opaque to core) */
    xo_filter_data_ops_t *xt_ops; /* Data vtable for alloc and name ops */
} xo_trie_t;

/*
 * One active trie node in a frame: a partial match that the node
 * opened at this depth has made.
 */
typedef struct xo_tslot_s {
    xo_trie_id_t xts_node;	/* trie node id */
    uint32_t xts_position;	/* 1-based open position */
    uint32_t xts_qual_position;	/* qualified position (leading-pred-gated) */
    uint8_t xts_state;		/* XTFS_* */
    uint8_t xts_flags;		/* XTFF_* */
} xo_tslot_t;

/*
 * A sibling counter: how many times a child matching a trie node has
 * been opened beneath a frame.
 */
typedef struct xo_tcount_s {
    xo_trie_id_t xtc_node;	/* child trie node id */
    uint32_t xtc_count;		/* opens seen so far */
} xo_tcount_t;

/*
 * Per-depth runtime frame.  The slots and the two child counter
 * tables each have room for one entry per trie node (xtm_slots_max),
 * which is the most any of them can hold: a trie node appears at
 * most once among a frame's slots, since the slots of the parent
 * frame hold distinct nodes and each node has one parent, and the
 * counter tables are keyed by trie node.  All three are carved from
 * one block, which starts at xtf_slots; it is allocated the first
 * time a depth is reached and kept for the life of the stack.
 */
typedef struct xo_tframe_s {
    uint32_t xtf_slots_count;	/* # of active slots */
    xo_tslot_t *xtf_slots;	/* Active slots (and start of the block) */
    int32_t xtf_allow_delta;	/* allow contribution to undo on pop */
    int32_t xtf_deny_delta;	/* deny contribution to undo on pop */
    int32_t xtf_cond_delta;	/* cond contribution to undo on pop */
    char *xtf_tag;		/* element name that opened this frame */
    ssize_t xtf_tag_len;
    char *xtf_keys;		/* buffered "k\0v\0k2\0v2\0\0" pairs */
    ssize_t xtf_keys_len;
    char *xtf_attrs;		/* buffered "@k\0v\0..." pairs (attributes) */
    ssize_t xtf_attrs_len;
    char *xtf_self;		/* this node's own value (for '.' in predicates) */
    ssize_t xtf_self_len;
    uint32_t xtf_dot_count;	/* # matched slots whose predicate uses '.' */
    uint32_t xtf_position_cur;  /* scratch: position for current C_INDEX eval */
    /* Child sibling counters (tracked in the PARENT frame, survive close) */
    uint32_t xtf_child_count;
    xo_tcount_t *xtf_child;
    /* Qualified child counters: only count when leading predicates pass */
    uint32_t xtf_child_qual_count;
    xo_tcount_t *xtf_child_qual;
} xo_tframe_t;

/* Bytes in a frame's block for each entry: a slot and two counters */
#define XO_TFRAME_SLOT_SIZE	(sizeof(xo_tslot_t) + 2 * sizeof(xo_tcount_t))

/* Per-slot flags (xts_flags) */
#define XTFF_QUAL_COUNTED  (1 << 0)  /* leading-pred qualified position counted */
#define XTFF_COND_COUNTED  (1 << 1)  /* slot is counted in xtm_cond */

/* Per-slot states */
#define XTFS_SEEK	0	/* Waiting for this node's element name */
#define XTFS_PRED	1	/* Name matched; evaluating predicates */
#define XTFS_LIVE	2	/* Fully matched (name + predicates) */
#define XTFS_DEAD	3	/* Predicate failed; ignore sub-tree */
#define XTFS_COND	4	/* Matched, if an enclosing predicate passes */
#define XTFS_CPRED	5	/* Like PRED, but beneath a pending predicate */

/*
 * A path step beneath a slot whose predicate is still pending (PRED,
 * CPRED, or COND) can only match conditionally: for "item[size =
 * 10]/sub", a "sub" that precedes "size" in the output matches only
 * if "size" later turns out to be 10.  Such a slot is COND (or CPRED,
 * while its own predicate is pending), and a terminal one counts in
 * xtm_cond, which makes the filter report XO_STATUS_COND so libxo
 * keeps that content tentatively until the predicate resolves.  A
 * terminal PRED slot counts as well, since its content is likewise
 * kept or tossed with the instance.
 *
 * LIVE/PRED and COND/CPREF are pairs that both differ in the same
 * way. Each slot depends on two things: its own predicate (decided or
 * not) and the predicates of its ancestors (all decided, or at least one
 * still pending).
 *
 *                         own predicate      own predicate
 *                        decided/none       pending
 *   ancestors decided     LIVE               PRED
 *   ancestor pending      COND               CPRED
 *
 * So PRED and CPRED are both waiting on their own predicate. With
 * PRED, everything above the slot is already settled, so a pass makes
 * the slot a real match. With CPRED, something above it is still
 * pending, so even a pass leaves the slot conditional.
 */

/*
 * Runtime matching state: a stack of frames driven by open/close events.
 */
typedef struct xo_tmatch_s {
    xo_trie_t *xtm_trie;	        /* The compiled trie */
    uint32_t xtm_depth;	        /* Current stack depth */
    uint32_t xtm_skipped;	/* Opens with no frame (see xo_tmatch_open) */
    uint32_t xtm_stack_max;	        /* Allocated frame count */
    uint32_t xtm_slots_max;	/* Max entries in xtf_slots et al */
    xo_tframe_t *xtm_stack;	/* Frame stack [0..xtm_depth] */
    uint32_t xtm_allow;          /* Active allow-match count */
    uint32_t xtm_deny;           /* Active deny-match count */
    uint32_t xtm_cond;           /* Active conditional-match count */
    uint32_t xtm_cur_action;     /* xtn_action from last matched TERMINAL node */
    uint32_t xtm_event;		/* Bumped per open/key/attr/resolve */
    uint32_t xtm_cur_event;	/* xtm_event when xtm_cur_action was set */
    int16_t xtm_cur_import_prec; /* import precedence of xtm_cur_action */
    double xtm_cur_priority;	/* priority of xtm_cur_action */
} xo_tmatch_t;

/*
 * Allocate a trie node
 */
static xo_trie_id_t
xo_trie_alloc_node (xo_trie_t *xtp)
{
    if (xtp->xt_nodes_count + 1 >= xtp->xt_nodes_max) {
	xo_trie_id_t cap = xtp->xt_nodes_max ? xtp->xt_nodes_max * 2 : 16;
	xo_tnode_t *p = xtp->xt_ops->xfdo_realloc(xtp->xt_data,
						    xtp->xt_nodes,
						    cap * sizeof(*p));
	if (p == NULL)
	    return 0;

	xtp->xt_nodes = p;
	xtp->xt_nodes_max = cap;
    }

    xo_trie_id_t id = ++xtp->xt_nodes_count;
    bzero(&xtp->xt_nodes[id], sizeof(xo_tnode_t));

    return id;
}

/*
 * Return (or create) the child of `parent` for one step of a pattern.
 * parent==0 means the root sibling list.  `flags` holds XTNF_WILDCARD
 * for a '*' step (name_id is then ignored) and XTNF_ABSOLUTE for the
 * first step of an absolute path; `pred` is the step's predicate (0
 * for none).
 *
 * Two steps share a node only when they are the same step: same name
 * (or both wildcards), and, at the root, both absolute or both
 * relative.  A relative "one" must not land on an absolute "/one",
 * or it inherits the anchor (and "a/one" stops matching).
 *
 * Predicates: two different predicates never share a node, since a
 * node holds only one; sharing would silently drop the earlier one.
 * A plain step and a predicated step of the same name do share, with
 * the node keeping the predicate.  libpin relies on this: it waits
 * for the predicate before dispatching, and its Patricia fallback
 * handles the plain pattern when the predicate fails (see the
 * terminal-conflict comment in xo_trie_insert).  libxo's default
 * backend names nodes by string-table offset, which is never shared,
 * so none of this merging happens there.
 */
static xo_trie_id_t
xo_trie_get_child (xo_trie_t *xtp, xo_trie_id_t parent, xo_name_id_t name_id,
		   uint16_t flags, xo_xparse_node_id_t pred)
{
    const uint16_t match_flags = XTNF_WILDCARD | XTNF_ABSOLUTE;
    xo_trie_id_t *listp = parent
	? &xtp->xt_nodes[parent].xtn_child
	: &xtp->xt_root;

    if (parent)
	flags &= ~XTNF_ABSOLUTE;
    flags &= match_flags;

    for (xo_trie_id_t s = *listp; s; s = xtp->xt_nodes[s].xtn_sibling) {
	xo_tnode_t *tn = &xtp->xt_nodes[s];

	if ((tn->xtn_flags & match_flags) != flags)
	    continue;
	if (!(flags & XTNF_WILDCARD) && tn->xtn_name.ni_id != name_id.ni_id)
	    continue;
	if (pred && tn->xtn_pred && pred != tn->xtn_pred)
	    continue;

	/* Only ever SET xtn_pred, never clear it (see above) */
	if (pred)
	    tn->xtn_pred = pred;
	return s;
    }

    xo_trie_id_t id = xo_trie_alloc_node(xtp);
    if (id == 0)
	return 0;

    /* xo_trie_alloc_node may have realloced xt_nodes; recalculate listp */
    listp = parent ? &xtp->xt_nodes[parent].xtn_child : &xtp->xt_root;

    xo_tnode_t *tn = &xtp->xt_nodes[id];
    if (!(flags & XTNF_WILDCARD))
	tn->xtn_name = name_id;
    tn->xtn_flags |= flags;
    tn->xtn_pred = pred;
    tn->xtn_sibling = *listp;
    *listp = id;

    return id;
}

static void
xo_trie_insert (xo_trie_t *xtp, xo_xparse_data_t *xdp,
		xo_xparse_node_id_t first_elem, uint16_t flags, uint32_t action,
		double priority, int16_t import_prec)
{
    xo_trie_id_t parent = 0;
    xo_xparse_node_t *xnp;

    for (xo_xparse_node_id_t id = first_elem; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(xdp, id);
	if (xnp->xn_type == C_ABSOLUTE) {
	    flags |= XTNF_ABSOLUTE;
	    continue;
	}

	xo_name_id_t nid = { 0 };
	uint16_t step_flags;

	if (xnp->xn_type == L_ASTERISK) {
	    step_flags = XTNF_WILDCARD;
	} else if (xnp->xn_type == C_ELEMENT) {
	    step_flags = 0;
	    if (xtp->xt_ops->xfdo_name_intern) {
		const char *nm = xo_xparse_str(xdp, xnp->xn_str);
		nid = xtp->xt_ops->xfdo_name_intern(xtp->xt_data, nm,
						     nm ? (ssize_t) strlen(nm) : 0);
	    } else {
		nid.ni_id = (uint32_t) xnp->xn_str;
	    }
	} else {
	    continue;
	}

	/*
	 * XTNF_ABSOLUTE means the path starts at root.  The check in
	 * xo_tmatch_open runs on root-level trie nodes, so the flag
	 * must live on the first element of the path (parent == 0),
	 * not on the terminal node.  xo_trie_get_child drops it for
	 * non-root steps.
	 */
	step_flags |= flags & XTNF_ABSOLUTE;

	/* Find this element's predicate (if any) among its children */
	xo_xparse_node_id_t pred = 0;
	for (xo_xparse_node_id_t cid = xnp->xn_contents; cid; ) {
	    xo_xparse_node_t *cp = xo_xparse_node(xdp, cid);
	    if (cp->xn_type == C_PREDICATE) {
		pred = cid;
		break;
	    }
	    cid = cp->xn_next;
	}

	xo_trie_id_t tid = xo_trie_get_child(xtp, parent, nid,
					     step_flags, pred);
	if (tid == 0)
	    return;

	parent = tid;
    }

    if (parent) {
	/*
	 * Two match patterns can legitimately terminate on the very same
	 * trie node (same tag name, same position) when pin_compile.c
	 * registers both a bare name and a predicate-qualified pattern for
	 * it (e.g. "author" and "author[life-span/born]"), or when an
	 * importing stylesheet overrides a same-named pattern from an
	 * xsl:import'd one -- the shared filter holds every match pattern,
	 * not just the "hard" ones.  A node has room for only one
	 * (predicate, action) pair, so when two patterns collide here,
	 * XSLT's own conflict-resolution rule (XSLT 1.0 SS5.5) decides which
	 * one the node keeps, in the same order pin_rulebook_apply_add uses
	 * for its Patricia-tree entries: higher import precedence wins
	 * outright; only when import precedence ties does priority decide;
	 * a full tie keeps whichever was inserted last.  The losing pattern
	 * still matches structurally, so libpin's Patricia fallback path is
	 * what ends up dispatching it when this node's winning predicate
	 * resolves false.
	 */
	int new_wins;
	if (!(xtp->xt_nodes[parent].xtn_flags & XTNF_TERMINAL))
	    new_wins = 1;
	else if (import_prec != xtp->xt_nodes[parent].xtn_import_prec)
	    new_wins = (import_prec > xtp->xt_nodes[parent].xtn_import_prec);
	else if (priority != xtp->xt_nodes[parent].xtn_priority)
	    new_wins = (priority > xtp->xt_nodes[parent].xtn_priority);
	else
	    new_wins = 1;

	if (new_wins) {
	    xtp->xt_nodes[parent].xtn_flags |=
		XTNF_TERMINAL | (flags & ~XTNF_ABSOLUTE);
	    xtp->xt_nodes[parent].xtn_action = action;
	    xtp->xt_nodes[parent].xtn_priority = priority;
	    xtp->xt_nodes[parent].xtn_import_prec = import_prec;
	}
    }
}

static xo_trie_t *
xo_trie_compile (xo_handle_t *xop UNUSED, xo_xparse_data_t *xdp,
		 xo_filter_data_t *dp, xo_filter_data_ops_t *ops,
		 uint32_t *actions, double *priorities, int16_t *import_precs)
{
    xo_trie_t *xtp = ops->xfdo_realloc(dp, NULL, sizeof(*xtp));
    if (xtp == NULL)
	return NULL;

    bzero(xtp, sizeof(*xtp));
    xtp->xt_xd = xdp;
    xtp->xt_data = dp;
    xtp->xt_ops = ops;

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
	uint32_t action = (actions != NULL) ? actions[i] : 0;
	double priority = (priorities != NULL) ? priorities[i] : 0.0;
	int16_t import_prec = (import_precs != NULL) ? import_precs[i] : 0;
	xo_trie_insert(xtp, xdp, elem, flags, action, priority, import_prec);
    }

    return xtp;
}

static void
xo_trie_free (xo_trie_t *xtp)
{
    if (xtp) {
	xo_filter_data_t *dp = xtp->xt_data;
	xo_filter_data_ops_t *ops = xtp->xt_ops;
	ops->xfdo_free(dp, xtp->xt_nodes);
	ops->xfdo_free(dp, xtp);
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
xo_tframe_free_tag (xo_tframe_t *frame)
{
    if (frame->xtf_tag) {
	xo_free(frame->xtf_tag);
	frame->xtf_tag = NULL;
	frame->xtf_tag_len = 0;
    }
}

static void
xo_tframe_set_tag (xo_tframe_t *frame, const char *tag, ssize_t tlen)
{
    xo_tframe_free_tag(frame);
    if (tlen < 0)
	tlen = strlen(tag);
    char *p = xo_realloc(NULL, tlen + 1);
    if (p) {
	memcpy(p, tag, tlen);
	p[tlen] = '\0';
	frame->xtf_tag = p;
	frame->xtf_tag_len = tlen;
    }
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

/*
 * Record this node's own value so a '.' (L_DOT) reference in a predicate
 * attached to the node can resolve to it.  Copied since the caller's
 * buffer may not outlive the frame.
 */
static void
xo_tframe_set_self (xo_tframe_t *frame, const char *value, xo_ssize_t vlen)
{
    if (value == NULL || vlen <= 0)
	return;

    char *newp = xo_realloc(NULL, vlen + 1);
    if (newp == NULL)
	return;

    memcpy(newp, value, vlen);
    newp[vlen] = '\0';
    frame->xtf_self = newp;
    frame->xtf_self_len = vlen;
}

static void
xo_tframe_free_self (xo_tframe_t *frame)
{
    if (frame->xtf_self) {
	xo_free(frame->xtf_self);
	frame->xtf_self = NULL;
	frame->xtf_self_len = 0;
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

/*
 * Does the node we are in have a frame of its own?  Not at the top,
 * and not beneath an open that xo_tmatch_open had to skip, where the
 * top of the stack belongs to an ancestor.
 */
static inline int
xo_tmatch_has_frame (xo_tmatch_t *xtmp)
{
    return xtmp->xtm_depth != 0 && xtmp->xtm_skipped == 0;
}

/*
 * Make a frame ready for a new node: everything in it is cleared
 * except the block for its per-slot arrays, which is kept (or
 * allocated, the first time this depth is used) and zeroed.  Returns
 * non-zero if that allocation fails, leaving the frame untouched.
 */
static int
xo_tframe_reset (xo_tmatch_t *xtmp, xo_tframe_t *frame)
{
    uint32_t count = xtmp->xtm_slots_max;
    size_t len = count * XO_TFRAME_SLOT_SIZE;
    xo_tslot_t *slots = frame->xtf_slots;

    if (slots == NULL) {
	slots = xo_realloc(NULL, len);
	if (slots == NULL)
	    return -1;
    }

    bzero(slots, len);
    bzero(frame, sizeof(*frame));

    /* The slots come first; the two counter tables follow them */
    frame->xtf_slots = slots;
    frame->xtf_child = (xo_tcount_t *) (slots + count);
    frame->xtf_child_qual = frame->xtf_child + count;

    return 0;
}

static void xo_tmatch_cleanup (xo_tmatch_t *xtmp);

static int
xo_tmatch_init (xo_handle_t *xop UNUSED, xo_tmatch_t *xtmp, xo_trie_t *trie)
{
    bzero(xtmp, sizeof(*xtmp));
    xtmp->xtm_trie = trie;
    xtmp->xtm_slots_max = trie->xt_nodes_count ? trie->xt_nodes_count : 1;

    uint32_t cap = 16;
    xtmp->xtm_stack = xo_realloc(NULL, cap * sizeof(*xtmp->xtm_stack));
    if (xtmp->xtm_stack == NULL)
	return -1;

    bzero(xtmp->xtm_stack, cap * sizeof(*xtmp->xtm_stack));
    xtmp->xtm_stack_max = cap;

    if (xo_tframe_reset(xtmp, &xtmp->xtm_stack[0]) < 0) {
	xo_tmatch_cleanup(xtmp);
	return -1;
    }

    /*
     * Depth-0 is an empty virtual frame.  The root re-probe loop in
     * xo_tmatch_open matches root trie nodes at every real depth, so
     * pre-seeding depth-0 with LIVE root nodes is wrong: it would let
     * the parent-descent loop skip the first step of a multi-step path
     * (e.g. "d/one" would match when "one" is opened directly).
     */
#if defined(XO_DEBUG)
    /* xo_get_flags handles the NULL xop that walk API callers pass */
    if (xo_get_flags(xop) & XOF_DEBUG) {
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
	    xo_tframe_free_tag(&xtmp->xtm_stack[d]);
	    xo_tframe_free_keys(&xtmp->xtm_stack[d]);
	    xo_tframe_free_attrs(&xtmp->xtm_stack[d]);
	    xo_tframe_free_self(&xtmp->xtm_stack[d]);
	}

	/* Frames keep their slot arrays after they are popped */
	for (uint32_t d = 0; d < xtmp->xtm_stack_max; d++) {
	    if (xtmp->xtm_stack[d].xtf_slots)
		xo_free(xtmp->xtm_stack[d].xtf_slots);
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
			   const char *, ssize_t, const char *, ssize_t);
static void xo_tmatch_close(xo_handle_t *, xo_filter_t *, xo_tmatch_t *,
			    const char *, ssize_t);
static int xo_tmatch_try_eager(xo_handle_t *, xo_filter_t *, xo_tframe_t *,
			       xo_xparse_node_id_t);
static void xo_filter_force_resolve_pred(xo_handle_t *, xo_filter_t *,
					 const char *, ssize_t);

/*
 * xo_eval_prog_s is fully defined much further down, with the rest of
 * the eval infrastructure; xo_filter_s only needs the pointer type here,
 * for its compiled-predicate cache.
 */
typedef struct xo_eval_prog_s xo_eval_prog_t;

/*
 * Defined with the rest of the eval infrastructure, but needed here
 * already by xo_filter_destroy_standalone/xo_filter_op_destroy to free
 * the predicate-program cache.
 */
void xo_eval_prog_destroy(xo_eval_prog_t *);

typedef unsigned xo_xsf_flags_t;   /* Type for XFSF_* flag fields */

/*
 * Default data backend: heap allocation + xparse string table.
 * This is the real definition of xo_filter_data_t used within this .so.
 * Alternative backends (e.g. libpin) define their own struct in their
 * own translation unit; the core only ever sees the forward declaration.
 */
struct xo_filter_data_s {
    xo_handle_t *xfd_xop;	 /* libxo handle (for xo_dbg calls) */
    xo_xparse_data_t *xfd_xdp;	 /* xparse data (for string-table lookup) */
};

static void *
xo_filter_data_default_realloc (xo_filter_data_t *dp UNUSED,
				 void *ptr, size_t sz)
{
    return xo_realloc(ptr, sz);
}

static void
xo_filter_data_default_free (xo_filter_data_t *dp UNUSED, void *ptr)
{
    xo_free(ptr);
}

static int
xo_filter_data_default_name_eq (xo_filter_data_t *dp,
				 xo_name_id_t id,
				 const char *tag, ssize_t tlen)
{
    const char *nm = xo_xparse_str(dp->xfd_xdp, (xo_xparse_str_id_t) id.ni_id);
    if (nm == NULL)
	return 0;
    return xo_streqn(nm, tag, tlen);
}

static void
xo_filter_data_default_error (xo_filter_data_t *dp, const char *fmt,
			       va_list vap)
{
    xo_failure_filter_v(dp->xfd_xop, fmt, vap);
}

xo_filter_data_ops_t xo_filter_data_ops_default = {
    .xfdo_version     = XO_FILTER_DATA_OPS_VERSION,
    .xfdo_realloc     = xo_filter_data_default_realloc,
    .xfdo_free        = xo_filter_data_default_free,
    .xfdo_name_intern = NULL,   /* optional; NULL wraps xparse str_id directly */
    .xfdo_name_eq     = xo_filter_data_default_name_eq,
    .xfdo_error       = xo_filter_data_default_error,
};

/*
 * Dispatches a compile-time or evaluation-time error through the data
 * context's xfdo_error, if the backend set one.  dp/ops always travel
 * together since xo_filter_data_t carries no back-pointer to its own
 * ops.  xo_eval_fail() below is the only caller today, reaching both
 * via xfp->xf_data / xfp->xf_ops; xo_eval_compile() will become a
 * second caller once it gains its own (dp, ops) parameters.
 */
static void
xo_filter_data_verror (xo_filter_data_t *dp, xo_filter_data_ops_t *ops,
			const char *fmt, va_list vap)
{
    /*
     * ops (and dp) may be NULL: xo_eval_compile()'s own doc comment
     * documents passing NULL for both when the caller has no data
     * context to report through, in which case the failure is silent.
     */
    if (ops == NULL || ops->xfdo_error == NULL)
	return;

    ops->xfdo_error(dp, fmt, vap);
}

/*
 * One entry in the packed xf_pred_progs[] cache; see its declaration
 * in struct xo_filter_s for details.
 */
typedef struct xo_filter_pred_entry_s {
    xo_xparse_node_id_t xpe_node_id;	/* predicate root node id (key) */
    xo_eval_prog_t *xpe_prog;		/* compiled program for that node */
} xo_filter_pred_entry_t;

struct xo_filter_s {		 /* Forward/typdef decl in xo_private.h */
    struct xo_xparse_data_s xf_xd; /* Main parsing structure */
    xo_filter_status_t xf_status; /* Current status: (see XO_STATUS_*) */
    xo_xsf_flags_t xf_flags;	 /* Flags (XFSF_*) */
    xo_trie_t *xf_trie;	 /* Compiled trie (NULL until first filter added) */
    xo_tmatch_t xf_tmatch;	 /* Runtime trie-matching state */
    xo_filter_data_t *xf_data;	 /* Data context (opaque to core) */
    xo_filter_data_ops_t *xf_ops; /* Data vtable */
    xo_filter_data_t xf_def_data; /* Embedded default data context (avoids extra alloc) */
    uint32_t *xf_path_actions;	 /* per-path action ids (parallel to xf_xd.xd_paths[]) */
    uint32_t xf_path_actions_cap; /* allocated capacity of xf_path_actions[] */
    double *xf_path_priorities;  /* per-path priorities (parallel to xf_path_actions[]) */
    uint32_t xf_path_priorities_cap; /* allocated capacity of xf_path_priorities[] */
    int16_t *xf_path_import_precs; /* per-path import precedence (parallel) */
    uint32_t xf_path_import_precs_cap; /* allocated capacity of xf_path_import_precs[] */
    uint32_t xf_cur_action;	 /* action from the last XO_STATUS_FULL transition */
    xo_filter_pred_entry_t *xf_pred_progs; /* compiled predicate cache: a
					packed array of {node id, compiled
					program} pairs, kept sorted by node
					id (the predicate's root expression,
					a C_PREDICATE node's xn_contents) and
					searched/inserted via binary search
					in xo_filter_pred_prog(); sized by the
					number of distinct predicates, not by
					the (much larger, sparse) AST node id
					space */
    uint32_t xf_pred_progs_count; /* number of entries in use in xf_pred_progs[] */
    uint32_t xf_pred_progs_max;	 /* allocated capacity (entries) of xf_pred_progs[] */
    int *xf_unsupported_tokens;	 /* set by xo_filter_set_unsupported_features;
					    every create installs the library
					    default
					    (xo_filter_default_unsupported_features) */
};

/* Flags for xf_flags */
#define XFSF_BLOCK		(1<<0)	/* Block emitting data */
#define XFSF_FORCE_RESOLVE	(1<<1)	/* Missing fields are "" at close */
#define XFSF_PROMOTED		(1<<2)	/* Pending pred passed, not terminal */

/*
 * The feature set every filter rejects by default, as installed by
 * xo_filter_op_create (the --libxo filter=/select= machinery),
 * xo_filter_create_with_data and xo_filter_create_standalone; this is
 * the Phase 1 grammar restriction (no predicates involving descendant-
 * or-self, union, variables, axis steps, or nested predicates) that used
 * to be hard-coded as a raw token list and applied implicitly to every
 * filter.  It is now just an ordinary XO_XPF_* array passed through
 * xo_filter_set_unsupported_features() like any caller (e.g. libpin)
 * would pass its own.
 */
static const int xo_filter_default_unsupported_features[] = {
    XO_XPF_DOTDOT, XO_XPF_DOTDOTDOT,
    XO_XPF_COMMENT, XO_XPF_ID, XO_XPF_KEY, XO_XPF_NODE,
    XO_XPF_PROCESSING_INSTRUCTION, XO_XPF_TEXT, XO_XPF_DSLASH,
    XO_XPF_AXIS_NAME, XO_XPF_VAR, XO_XPF_SEQUENCE, XO_XPF_DESCENDANT,
    XO_XPF_TEST, XO_XPF_UNION, XO_XPF_NESTED_PREDICATES,
    XO_XPF_PREDICATE_PATHS,
    0
};

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

    /* Use embedded default data context; point xfd_xdp at our own xparse data */
    xfp->xf_def_data.xfd_xop = xop;
    xfp->xf_def_data.xfd_xdp = &xfp->xf_xd;
    xfp->xf_data = &xfp->xf_def_data;
    xfp->xf_ops  = &xo_filter_data_ops_default;

    if (xo_filter_set_unsupported_features(xfp, NULL) < 0) {
	xo_xparse_clean(&xfp->xf_xd);
	xo_free(xfp);
	return NULL;
    }

    xo_set_filter_data(xop, xfp);

    return xfp;
}

xo_filter_t *
xo_filter_create_with_data (xo_handle_t *xop, xo_filter_data_t *dp,
			     xo_filter_data_ops_t *ops)
{
    xo_filter_t *xfp = xo_realloc(NULL, sizeof(*xfp));
    if (xfp == NULL)
	return NULL;

    bzero(xfp, sizeof(*xfp));

    xo_xparse_init(&xfp->xf_xd);

    xfp->xf_data = dp;
    xfp->xf_ops  = ops;

    if (xo_filter_set_unsupported_features(xfp, NULL) < 0) {
	xo_xparse_clean(&xfp->xf_xd);
	xo_free(xfp);
	return NULL;
    }

    xo_set_filter_data(xop, xfp);

    return xfp;
}

/*
 * Create a standalone filter not attached to any xo_handle_t.
 * Uses the default (built-in) data ops.  Intended for per-BIA_IF
 * condition evaluation where no custom name-intern or value-of is needed.
 */
xo_filter_t *
xo_filter_create_standalone (void)
{
    xo_filter_t *xfp = xo_realloc(NULL, sizeof(*xfp));
    if (xfp == NULL)
	return NULL;

    bzero(xfp, sizeof(*xfp));
    xo_xparse_init(&xfp->xf_xd);

    xfp->xf_def_data.xfd_xdp = &xfp->xf_xd;
    xfp->xf_data = &xfp->xf_def_data;
    xfp->xf_ops  = &xo_filter_data_ops_default;

    if (xo_filter_set_unsupported_features(xfp, NULL) < 0) {
	xo_xparse_clean(&xfp->xf_xd);
	xo_free(xfp);
	return NULL;
    }

    return xfp;
}

/*
 * Destroy a standalone filter created by xo_filter_create_standalone.
 */
void
xo_filter_destroy_standalone (xo_filter_t *xfp)
{
    if (xfp == NULL)
	return;
    xo_xparse_clean(&xfp->xf_xd);
    xo_tmatch_cleanup(&xfp->xf_tmatch);
    xo_trie_free(xfp->xf_trie);
    xfp->xf_trie = NULL;
    if (xfp->xf_path_actions) {
	free(xfp->xf_path_actions);
	xfp->xf_path_actions = NULL;
    }
    if (xfp->xf_path_priorities) {
	free(xfp->xf_path_priorities);
	xfp->xf_path_priorities = NULL;
    }
    if (xfp->xf_path_import_precs) {
	free(xfp->xf_path_import_precs);
	xfp->xf_path_import_precs = NULL;
    }
    if (xfp->xf_pred_progs) {
	for (uint32_t i = 0; i < xfp->xf_pred_progs_count; i++)
	    xo_eval_prog_destroy(xfp->xf_pred_progs[i].xpe_prog);
	free(xfp->xf_pred_progs);
	xfp->xf_pred_progs = NULL;
	xfp->xf_pred_progs_count = xfp->xf_pred_progs_max = 0;
    }
    if (xfp->xf_unsupported_tokens) {
	free(xfp->xf_unsupported_tokens);
	xfp->xf_unsupported_tokens = NULL;
    }

    if (xfp->xf_ops != NULL && xfp->xf_ops->xfdo_destroy != NULL)
	xfp->xf_ops->xfdo_destroy(xfp->xf_data);

    xo_free(xfp);
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

    if (xfp->xf_path_actions) {
	free(xfp->xf_path_actions);
	xfp->xf_path_actions = NULL;
    }
    if (xfp->xf_path_priorities) {
	free(xfp->xf_path_priorities);
	xfp->xf_path_priorities = NULL;
    }
    if (xfp->xf_path_import_precs) {
	free(xfp->xf_path_import_precs);
	xfp->xf_path_import_precs = NULL;
    }
    if (xfp->xf_pred_progs) {
	for (uint32_t i = 0; i < xfp->xf_pred_progs_count; i++)
	    xo_eval_prog_destroy(xfp->xf_pred_progs[i].xpe_prog);
	free(xfp->xf_pred_progs);
	xfp->xf_pred_progs = NULL;
	xfp->xf_pred_progs_count = xfp->xf_pred_progs_max = 0;
    }
    if (xfp->xf_unsupported_tokens) {
	free(xfp->xf_unsupported_tokens);
	xfp->xf_unsupported_tokens = NULL;
    }

    if (xfp->xf_ops != NULL && xfp->xf_ops->xfdo_destroy != NULL)
	xfp->xf_ops->xfdo_destroy(xfp->xf_data);

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

	/*
	 * Several terminal nodes can match during one event (e.g.
	 * "item[a]" and "item[b]" resolving on the same key).  Pick
	 * the action the way xo_trie_insert does for a shared node:
	 * higher import precedence, then higher priority; on a full
	 * tie, the first one recorded stays.  A match from a later
	 * event always replaces the current action.
	 */
	if (xtmp->xtm_cur_event == xtmp->xtm_event) {
	    if (tn->xtn_import_prec != xtmp->xtm_cur_import_prec) {
		if (tn->xtn_import_prec < xtmp->xtm_cur_import_prec)
		    return;
	    } else if (tn->xtn_priority <= xtmp->xtm_cur_priority)
		return;
	}

	xtmp->xtm_cur_action = tn->xtn_action;
	xtmp->xtm_cur_event = xtmp->xtm_event;
	xtmp->xtm_cur_import_prec = tn->xtn_import_prec;
	xtmp->xtm_cur_priority = tn->xtn_priority;
    }
}

/*
 * Count a conditional (or pending) terminal slot in xtm_cond, so its
 * content is kept tentatively.  Deny paths are not conditional.
 */
static void
xo_tmatch_record_cond (xo_tmatch_t *xtmp, xo_tframe_t *frame,
		       xo_tslot_t *slotp, xo_tnode_t *tn)
{
    if (!(tn->xtn_flags & XTNF_TERMINAL) || (tn->xtn_flags & XTNF_NOT))
	return;
    if (slotp->xts_flags & XTFF_COND_COUNTED)
	return;

    slotp->xts_flags |= XTFF_COND_COUNTED;
    xtmp->xtm_cond += 1;
    frame->xtf_cond_delta += 1;
}

static void
xo_tmatch_uncount_cond (xo_tmatch_t *xtmp, xo_tframe_t *frame,
			xo_tslot_t *slotp)
{
    if (!(slotp->xts_flags & XTFF_COND_COUNTED))
	return;

    slotp->xts_flags &= ~XTFF_COND_COUNTED;
    xtmp->xtm_cond -= 1;
    frame->xtf_cond_delta -= 1;
}

/*
 * A pending (PRED or CPRED) slot's predicate has been decided.  A
 * PRED slot becomes LIVE or DEAD; a CPRED slot becomes COND or DEAD,
 * since it still hangs on its enclosing predicate.  When a PRED slot
 * passes but is not terminal, nothing becomes FULL, but content kept
 * conditionally beneath it is now wanted, so we flag the promotion
 * for the op to report as XO_STATUS_LIVE.
 */
static void
xo_tmatch_resolve (xo_filter_t *xfp, xo_tmatch_t *xtmp, xo_tframe_t *frame,
		   xo_tslot_t *slotp, xo_tnode_t *tn, int live)
{
    if (!live) {
	slotp->xts_state = XTFS_DEAD;
	xo_tmatch_uncount_cond(xtmp, frame, slotp);

    } else if (slotp->xts_state == XTFS_CPRED) {
	slotp->xts_state = XTFS_COND;

    } else {
	slotp->xts_state = XTFS_LIVE;
	xo_tmatch_uncount_cond(xtmp, frame, slotp);
	xo_tmatch_record_live(xtmp, frame, tn);
	if (!(tn->xtn_flags & XTNF_TERMINAL))
	    xfp->xf_flags |= XFSF_PROMOTED;
    }
}

static inline int
xo_tslot_is_pending (xo_tslot_t *slotp)
{
    return slotp->xts_state == XTFS_PRED || slotp->xts_state == XTFS_CPRED;
}

/*
 * Return TRUE if any slot in this frame is waiting on a predicate
 */
static int
xo_tframe_has_pred (xo_tframe_t *framep)
{
    for (uint32_t i = 0; i < framep->xtf_slots_count; i++)
	if (xo_tslot_is_pending(&framep->xtf_slots[i]))
	    return TRUE;

    return FALSE;
}

/*
 * Look up child trie node `c` in the parent's sibling-counter table,
 * increment its count, and return the new (1-based) open position.
 */
static uint32_t
xo_tframe_child_position (xo_tframe_t *parent, xo_trie_id_t c)
{
    for (uint32_t j = 0; j < parent->xtf_child_count; j++) {
	if (parent->xtf_child[j].xtc_node == c)
	    return ++parent->xtf_child[j].xtc_count;
    }

    uint32_t j = parent->xtf_child_count++;
    parent->xtf_child[j].xtc_node = c;
    parent->xtf_child[j].xtc_count = 1;
    return 1;
}

/*
 * Like xo_tframe_child_position, but only counts opens where the leading
 * (non-positional) predicates passed.  Stored in separate parallel counters
 * in the parent frame so the two counts never interfere.
 */
static uint32_t
xo_tframe_child_qualified_position (xo_tframe_t *parent, xo_trie_id_t c)
{
    for (uint32_t j = 0; j < parent->xtf_child_qual_count; j++) {
	if (parent->xtf_child_qual[j].xtc_node == c)
	    return ++parent->xtf_child_qual[j].xtc_count;
    }

    uint32_t j = parent->xtf_child_qual_count++;
    parent->xtf_child_qual[j].xtc_node = c;
    parent->xtf_child_qual[j].xtc_count = 1;
    return 1;
}

/*
 * Return TRUE if the predicate list has a C_INDEX predicate that is
 * NOT the first predicate (meaning there are leading key/test
 * predicates before it).
 *
 * foo[2]           - FALSE (C_INDEX is first, use open-time position)
 * foo[x=1][2]      - TRUE  (C_INDEX is trailing, must use qualified position)
 * foo[2][x=1]      - FALSE (C_INDEX is first)
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

/*
 * Does this node's predicate chain reference the context node ('.')?  The
 * grammar marks such C_PREDICATE nodes with XXPF_USES_DOT at parse time.
 */
static int
xo_pred_uses_dot (xo_filter_t *xfp, xo_xparse_node_id_t pred)
{
    xo_xparse_node_t *xnp;

    for ( ; pred; pred = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, pred);
	if (xnp->xn_type == C_PREDICATE && (xnp->xn_flags & XXPF_USES_DOT))
	    return TRUE;
    }

    return FALSE;
}

/*
 * Record a matched predicate slot: if it references '.', the frame must hold
 * this node's own value before the eager eval reads it (via xo_eval_dot).
 * We stash the value only once per frame and count such slots.
 */
static void
xo_tmatch_note_pred (xo_filter_t *xfp, xo_tframe_t *frame,
		     xo_xparse_node_id_t pred,
		     const char *value, ssize_t vlen)
{
    if (xo_pred_uses_dot(xfp, pred)) {
	if (frame->xtf_dot_count++ == 0)
	    xo_tframe_set_self(frame, value, vlen);
    }
}

/*
 * Set the initial state of a new slot 's' in 'frame' for trie node
 * 'tn'.  'cond' means the parent slot is itself conditional or
 * pending, making any match here conditional on it.
 */
static void
xo_tmatch_slot_init (xo_handle_t *xop, xo_filter_t *xfp, xo_tmatch_t *xtmp,
		     xo_tframe_t *frame, uint32_t s, xo_tnode_t *tn,
		     uint32_t position, int cond,
		     const char *value, ssize_t vlen)
{
    xo_tslot_t *slotp = &frame->xtf_slots[s];
    int state = XTFS_LIVE;

    if (tn->xtn_pred) {
	xo_tmatch_note_pred(xfp, frame, tn->xtn_pred, value, vlen);
	frame->xtf_position_cur = position;
	state = xo_tmatch_try_eager(xop, xfp, frame, tn->xtn_pred);
    }

    if (cond) {
	if (state == XTFS_LIVE)
	    state = XTFS_COND;
	else if (state == XTFS_PRED)
	    state = XTFS_CPRED;
    }

    slotp->xts_state = state;

    if (state == XTFS_LIVE)
	xo_tmatch_record_live(xtmp, frame, tn);
    else if (state != XTFS_DEAD)
	xo_tmatch_record_cond(xtmp, frame, slotp, tn);
}

static void
xo_tmatch_open (xo_handle_t *xop, xo_filter_t *xfp,
		xo_tmatch_t *xtmp, const char *tag, ssize_t tlen,
		const char *value, ssize_t vlen)
{
    xo_trie_t *xtp = xtmp->xtm_trie;
    xo_filter_data_t *dp = xtp->xt_data;
    xo_filter_data_ops_t *ops = xtp->xt_ops;

    /*
     * If we cannot grow the stack (or get the arrays for a new
     * frame), this open gets no frame, and the close that pairs with
     * it must not pop someone else's.  So we
     * count the opens we skip, and everything beneath one is skipped
     * as well (even if memory returns), since a frame pushed there
     * would sit on the wrong parent.
     */
    if (xtmp->xtm_skipped) {
	xtmp->xtm_skipped += 1;
	return;
    }

    if (xtmp->xtm_depth + 1 >= xtmp->xtm_stack_max) {
	uint32_t cap = xtmp->xtm_stack_max * 2;
	xo_tframe_t *p = xo_realloc(xtmp->xtm_stack, cap * sizeof(*p));
	if (p == NULL) {
	    xtmp->xtm_skipped = 1;
	    return;
	}
	bzero(p + xtmp->xtm_stack_max, (cap - xtmp->xtm_stack_max) * sizeof(*p));
	xtmp->xtm_stack = p;
	xtmp->xtm_stack_max = cap;
    }

    xo_tframe_t *parent = &xtmp->xtm_stack[xtmp->xtm_depth];
    xo_tframe_t *frame = parent + 1;
    if (xo_tframe_reset(xtmp, frame) < 0) {
	xtmp->xtm_skipped = 1;
	return;
    }

    xtmp->xtm_depth += 1;
    xtmp->xtm_event += 1;
    xo_tframe_set_tag(frame, tag, tlen);

    xo_dbg(xop, "xo_tmatch_open: depth %u tag '%.*s'",
	   xtmp->xtm_depth, (int) tlen, tag);

    if (ops->xfdo_event_open)
	ops->xfdo_event_open(dp, tag, tlen);

    /*
     * Descend from every LIVE parent slot, and conditionally from
     * every slot that is waiting on a predicate (see XTFS_COND).
     */
    for (uint32_t i = 0; i < parent->xtf_slots_count; i++) {
	int pstate = parent->xtf_slots[i].xts_state;
	if (pstate == XTFS_SEEK || pstate == XTFS_DEAD)
	    continue;
	int cond = (pstate != XTFS_LIVE);
	xo_tnode_t *ptn = &xtp->xt_nodes[parent->xtf_slots[i].xts_node];
	for (xo_trie_id_t c = ptn->xtn_child; c;
		     c = xtp->xt_nodes[c].xtn_sibling) {
	    xo_tnode_t *tn = &xtp->xt_nodes[c];
	    if (!(tn->xtn_flags & XTNF_WILDCARD) &&
		    !ops->xfdo_name_eq(dp, tn->xtn_name, tag, tlen))
		continue;

	    uint32_t position = xo_tframe_child_position(parent, c);
	    uint32_t s = frame->xtf_slots_count++;
	    frame->xtf_slots[s].xts_node = c;
	    frame->xtf_slots[s].xts_position = position;

	    xo_tmatch_slot_init(xop, xfp, xtmp, frame, s, tn, position,
				cond, value, vlen);
	}
    }

    /* Re-probe root nodes for relative paths; absolute only at depth 1 */
    for (xo_trie_id_t r = xtp->xt_root; r;
	 r = xtp->xt_nodes[r].xtn_sibling) {
	xo_tnode_t *tn = &xtp->xt_nodes[r];
	if ((tn->xtn_flags & XTNF_ABSOLUTE) && xtmp->xtm_depth != 1)
	    continue;

	if (!(tn->xtn_flags & XTNF_WILDCARD) &&
		!ops->xfdo_name_eq(dp, tn->xtn_name, tag, tlen))
	    continue;

	/* Avoid duplicating a node already added via parent descent */
	int dup = FALSE;
	for (uint32_t j = 0; j < frame->xtf_slots_count; j++) {
	    if (frame->xtf_slots[j].xts_node == r) {
		dup = TRUE;
		break;
	    }
	}

	if (dup)
	    continue;

	uint32_t position = xo_tframe_child_position(parent, r);
	uint32_t s = frame->xtf_slots_count++;
	frame->xtf_slots[s].xts_node = r;
	frame->xtf_slots[s].xts_position = position;

	xo_tmatch_slot_init(xop, xfp, xtmp, frame, s, tn, position,
			    FALSE, value, vlen);
    }

    xo_dbg(xop, "xo_tmatch_open: frame %u active [allow %u/deny %u/cond %u]",
	   frame->xtf_slots_count, xtmp->xtm_allow, xtmp->xtm_deny,
	   xtmp->xtm_cond);
}

static void
xo_tmatch_close (xo_handle_t *xop, xo_filter_t *xfp,
		 xo_tmatch_t *xtmp, const char *tag, ssize_t tlen)
{
    /* An open that pushed no frame has nothing to pop */
    if (xtmp->xtm_skipped) {
	xtmp->xtm_skipped -= 1;
	return;
    }

    if (xtmp->xtm_depth == 0)
	return;

    xo_tframe_t *frame = &xtmp->xtm_stack[xtmp->xtm_depth];
    xtmp->xtm_allow -= frame->xtf_allow_delta;
    xtmp->xtm_deny  -= frame->xtf_deny_delta;
    xtmp->xtm_cond  -= frame->xtf_cond_delta;
    xo_tframe_free_tag(frame);
    xo_tframe_free_keys(frame);
    xo_tframe_free_attrs(frame);
    xo_tframe_free_self(frame);

    if (xfp->xf_ops && xfp->xf_ops->xfdo_event_close)
	xfp->xf_ops->xfdo_event_close(xfp->xf_data, tag, tlen);

    xo_dbg(xop, "xo_tmatch_close: depth %u [allow %u/deny %u/cond %u]",
	   xtmp->xtm_depth, xtmp->xtm_allow, xtmp->xtm_deny, xtmp->xtm_cond);

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
 * Maps the public XO_XPF_* feature ids (xo_filter.h) to the private,
 * bison-generated token numbers (xo_xpath.tab.h) used internally by
 * xo_xparse/xo_xpath.  Adding a new gatable grammar feature means adding
 * one line here (and a matching %token in xo_xpath.y, if it's not already
 * there) plus a new XO_XPF_* define in xo_filter.h.
 */
static const struct {
    int xftm_feature;		/* XO_XPF_* id, from xo_filter.h */
    int xftm_token;		/* internal token number, from xo_xpath.tab.h */
} xo_filter_feature_tokens[] = {
    { XO_XPF_DOTDOT,			L_DOTDOT },
    { XO_XPF_DOTDOTDOT,			L_DOTDOTDOT },
    { XO_XPF_COMMENT,			K_COMMENT },
    { XO_XPF_ID,			K_ID },
    { XO_XPF_KEY,			K_KEY },
    { XO_XPF_NODE,			K_NODE },
    { XO_XPF_PROCESSING_INSTRUCTION,	K_PROCESSING_INSTRUCTION },
    { XO_XPF_TEXT,			K_TEXT },
    { XO_XPF_DSLASH,			L_DSLASH },
    { XO_XPF_AXIS_NAME,			T_AXIS_NAME },
    { XO_XPF_VAR,			T_VAR },
    { XO_XPF_SEQUENCE,			M_SEQUENCE },
    { XO_XPF_DESCENDANT,		C_DESCENDANT },
    { XO_XPF_TEST,			C_TEST },
    { XO_XPF_UNION,			C_UNION },
    { XO_XPF_NESTED_PREDICATES,		C_NESTED_PREDICATES },
    { XO_XPF_PREDICATE_PATHS,		C_PREDICATE_PATHS },
    { 0, 0 }
};

/*
 * See xo_filter.h for the full description.  Translates 'features' (a
 * zero-terminated array of XO_XPF_* ids) into the private token numbers
 * xo_xparse/xo_xpath actually use, and stores the result on xfp so that
 * xo_filter_compile_xpath_action_priority() picks it up on the next call.
 * features == NULL restores xo_filter_default_unsupported_features; an
 * empty list ({ 0 }) means no grammar restriction at all.  The new list
 * is built before the old one is freed, so a failure leaves the
 * previous setting in place.
 */
int
xo_filter_set_unsupported_features (xo_filter_t *xfp, const int *features)
{
    if (features == NULL)
	features = xo_filter_default_unsupported_features;

    int count = 0;
    while (features[count])
	count += 1;

    int *tokens = malloc((count + 1) * sizeof(*tokens));
    if (tokens == NULL)
	return -1;

    for (int i = 0; i < count; i++) {
	int j;
	for (j = 0; xo_filter_feature_tokens[j].xftm_feature; j++)
	    if (xo_filter_feature_tokens[j].xftm_feature == features[i])
		break;

	if (xo_filter_feature_tokens[j].xftm_feature == 0) {
	    free(tokens);
	    return -1;		/* unknown feature id */
	}

	tokens[i] = xo_filter_feature_tokens[j].xftm_token;
    }
    tokens[count] = 0;

    if (xfp->xf_unsupported_tokens)
	free(xfp->xf_unsupported_tokens);
    xfp->xf_unsupported_tokens = tokens;
    return 0;
}

/*
 * Core implementation: parse an XPath expression string and recompile the
 * trie for the given filter.  Called by both xo_filter_op_add_one (which
 * looks up xfp from xop) and xo_filter_walk_add (which already has xfp).
 */
static int
xo_filter_compile_xpath_action_priority (xo_handle_t *xop, xo_filter_t *xfp,
					 const char *input, uint32_t action,
					 double priority, int16_t import_prec)
{
    xo_xparse_data_t *xdp = xo_filter_xparse_data(xop, xfp);
    if (xdp == NULL)
	return -1;

    int *unsupported = xfp->xf_unsupported_tokens;

    int start = xdp->xd_paths_cur;
    xo_xparse_set_unsupported_tokens(xdp, unsupported);

    int rc = xo_xparse_parse_string(xop, xdp, input);
    if (rc == 0)
	rc = xo_xpath_feature_warn_since(NULL, xdp, start, unsupported, "");
    if (rc)
	return -1;

    /* Grow xf_path_actions to cover the new capacity if needed */
    if (xdp->xd_paths_max > xfp->xf_path_actions_cap) {
	uint32_t *newp = realloc(xfp->xf_path_actions,
				  xdp->xd_paths_max * sizeof(*newp));
	if (newp == NULL)
	    return -1;
	/* zero-init the newly allocated slots */
	bzero(newp + xfp->xf_path_actions_cap,
	      (xdp->xd_paths_max - xfp->xf_path_actions_cap) * sizeof(*newp));
	xfp->xf_path_actions = newp;
	xfp->xf_path_actions_cap = xdp->xd_paths_max;
    }

    /* Grow xf_path_priorities to cover the new capacity if needed */
    if (xdp->xd_paths_max > xfp->xf_path_priorities_cap) {
	double *newp = realloc(xfp->xf_path_priorities,
				xdp->xd_paths_max * sizeof(*newp));
	if (newp == NULL)
	    return -1;
	/* zero-init the newly allocated slots */
	bzero(newp + xfp->xf_path_priorities_cap,
	      (xdp->xd_paths_max - xfp->xf_path_priorities_cap) * sizeof(*newp));
	xfp->xf_path_priorities = newp;
	xfp->xf_path_priorities_cap = xdp->xd_paths_max;
    }

    /* Grow xf_path_import_precs to cover the new capacity if needed */
    if (xdp->xd_paths_max > xfp->xf_path_import_precs_cap) {
	int16_t *newp = realloc(xfp->xf_path_import_precs,
				 xdp->xd_paths_max * sizeof(*newp));
	if (newp == NULL)
	    return -1;
	bzero(newp + xfp->xf_path_import_precs_cap,
	      (xdp->xd_paths_max - xfp->xf_path_import_precs_cap)
	      * sizeof(*newp));
	xfp->xf_path_import_precs = newp;
	xfp->xf_path_import_precs_cap = xdp->xd_paths_max;
    }

    /* Record the action/priority/import-precedence for each new path */
    for (uint32_t i = (uint32_t) start; i < xdp->xd_paths_cur; i++) {
	xfp->xf_path_actions[i] = action;
	xfp->xf_path_priorities[i] = priority;
	xfp->xf_path_import_precs[i] = import_prec;
    }

    xo_trie_free(xfp->xf_trie);
    xo_tmatch_cleanup(&xfp->xf_tmatch);

    xfp->xf_trie = xo_trie_compile(xop, xdp, xfp->xf_data, xfp->xf_ops,
				    xfp->xf_path_actions, xfp->xf_path_priorities,
				    xfp->xf_path_import_precs);
    if (xfp->xf_trie == NULL)
	return -1;

    if (xo_tmatch_init(xop, &xfp->xf_tmatch, xfp->xf_trie) < 0) {
	xo_trie_free(xfp->xf_trie);
	xfp->xf_trie = NULL;
	return -1;
    }

    return 0;
}

static int
xo_filter_compile_xpath_action (xo_handle_t *xop, xo_filter_t *xfp,
				 const char *input, uint32_t action)
{
    return xo_filter_compile_xpath_action_priority(xop, xfp, input, action,
						   0.0, 0);
}

static int
xo_filter_compile_xpath (xo_handle_t *xop, xo_filter_t *xfp, const char *input)
{
    return xo_filter_compile_xpath_action(xop, xfp, input, 0);
}

/*
 * Add a filter (xpath) to our filtering mechanism
 */
static int
xo_filter_op_add_one (xo_handle_t *xop, const char *input)
{
    xo_filter_t *xfp = xo_get_filter_data(xop, TRUE);
    if (xfp == NULL)
	return -1;

    return xo_filter_compile_xpath(xop, xfp, input);
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
	xfp->xf_cur_action = xfp->xf_tmatch.xtm_cur_action;

    } else if (xfp->xf_xd.xd_flags & XDF_ALL_NOTS) {
	why = "all-nots";
	rc = XO_STATUS_FULL;

    } else {
	/*
	 * No active matches mean we're tracking; never DEAD in trie
	 * mode since the trie always re-probes root nodes on each
	 * open.  If any slot in the current frame is waiting for a
	 * predicate, use PRED so content is kept tentatively until
	 * it resolves.  If we're beneath a pending predicate and
	 * would match should it pass, use COND.  A slot that might
	 * match deeper down (a COND slot that isn't terminal) also
	 * uses PRED: it's tentative, kept only if something beneath
	 * it turns out to be wanted.
	 */
	xo_tmatch_t *xtmp = &xfp->xf_tmatch;
	int cond_slot = FALSE;

	if (xo_tmatch_has_frame(xtmp)) {
	    xo_tframe_t *frame = &xtmp->xtm_stack[xtmp->xtm_depth];
	    for (uint32_t i = 0; i < frame->xtf_slots_count; i++) {
		if (xo_tslot_is_pending(&frame->xtf_slots[i])) {
		    why = "pred-pending";
		    rc = XO_STATUS_PRED;
		    goto done_status;
		}
		if (frame->xtf_slots[i].xts_state == XTFS_COND)
		    cond_slot = TRUE;
	    }
	}

	if (xtmp->xtm_cond) {
	    why = "cond-full";
	    rc = XO_STATUS_COND;
	} else if (cond_slot) {
	    why = "cond-pending";
	    rc = XO_STATUS_PRED;
	} else {
	    why = "default-to-no";
	    rc = XO_STATUS_TRACK;
	}
    done_status:;
    }

    if (tlen == 0)		/* Avoid NULL deref */
	tag = "";

    XO_DBG(xop, "xo_filter_update_status (%s%s%.*s) returns %s/%d "
	   "why: %s (was %s/%d)",
	   op, tlen ? " " : "", (int) tlen, tag,
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
		const char *tag, ssize_t tlen, const char *type UNUSED,
		const char *value, ssize_t vlen)
{
    if (xfp == NULL || xfp->xf_trie == NULL)
	return 0;

    XO_DBG(xop, "filter: open %s: '%.*s'", type, (int) tlen, tag);

    xo_tmatch_open(xop, xfp, &xfp->xf_tmatch, tag, tlen, value, vlen);

    xo_filter_change_status(xop, xfp, "open", tag, tlen);

    return xfp->xf_status;
}

static int
xo_filter_op_open_container (xo_handle_t *xop, xo_filter_t *xfp,
			  const char *tag)
{
    return xo_filter_open(xop, xfp, tag, strlen(tag), "container", NULL, 0);
}

static int
xo_filter_op_open_instance (xo_handle_t *xop, xo_filter_t *xfp, const char *tag)
{
    return xo_filter_open(xop, xfp, tag, strlen(tag), "list", NULL, 0);
}

static int
xo_filter_op_open_field (XO_FILTER_OPEN_FIELD_SIGNATURE)
{
    int rc = xo_filter_open(xop, xfp, tag, tlen, "field", value, vlen);

    /*
     * A field is a leaf: no child can arrive to satisfy a pending
     * predicate (e.g. a leaf-list member "item" against
     * "item[starts-with(sku, 'GRO')]"), so resolve it now, treating
     * missing fields as empty, as close_instance does.  Otherwise
     * PRED status makes the field unskippable and encoders see it.
     */
    if (rc == XO_STATUS_PRED) {
	xo_filter_force_resolve_pred(xop, xfp, tag, tlen);
	rc = xfp->xf_status;
    }

    return rc;
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

    XO_DBG(xop, "filter: close %s: '%.*s'", type, (int) tlen, tag);

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

/*
 * Close an instance or container that may still be waiting on a
 * predicate (PRED): the fields it tests were never seen, so
 * force-resolve, treating them as absent (see xo_eval_node_value).
 */
static int
xo_filter_close_pending (xo_handle_t *xop, xo_filter_t *xfp,
			 const char *tag, const char *type)
{
    int promoted = FALSE;
    if (xfp != NULL && xfp->xf_trie != NULL
	    && xfp->xf_status == XO_STATUS_PRED) {
	xfp->xf_flags &= ~XFSF_PROMOTED;
	xo_filter_force_resolve_pred(xop, xfp, tag, strlen(tag));
	promoted = (xfp->xf_flags & XFSF_PROMOTED) ? TRUE : FALSE;
	xfp->xf_flags &= ~XFSF_PROMOTED;
    }

    xo_filter_status_t pre_close = xfp ? xfp->xf_status : XO_STATUS_ZERO;
    xo_filter_close(xop, xfp, tag, strlen(tag), type);

    /*
     * If force-resolve promoted us to FULL, return FULL even though the
     * frame pop has now decremented xtm_allow back.  If it passed a
     * predicate on a path that continues below us, return LIVE so
     * content kept conditionally can be committed.
     */
    if (pre_close == XO_STATUS_FULL)
	return XO_STATUS_FULL;
    if (promoted && pre_close != XO_STATUS_COND)
	return XO_STATUS_LIVE;
    return xfp ? xfp->xf_status : XO_STATUS_ZERO;
}

static int
xo_filter_op_close_instance (xo_handle_t *xop, xo_filter_t *xfp,
			  const char *tag)
{
    return xo_filter_close_pending(xop, xfp, tag, "instance");
}

/*
 * A container is resolved at close like an instance, but only when
 * it's still pending; one that's simply FULL reports the status after
 * the pop, as it always has.
 */
static int
xo_filter_op_close_container (xo_handle_t *xop, xo_filter_t *xfp,
			   const char *tag)
{
    if (xfp != NULL && xfp->xf_trie != NULL
	    && xfp->xf_status == XO_STATUS_PRED)
	return xo_filter_close_pending(xop, xfp, tag, "container");

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
	xo_float_t xevd_float;	    /* If C_FLOAT (never NaN; see C_NAN) */
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
#define XEVF_NODE	(1<<4)  /* Value is from a node that exists */
#define XEVF_EMPTYSET	(1<<5)  /* Value is from a node that's absent */

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

/*
 * One calling convention for every XPath operation, leaf or binary: a
 * leaf/unary construct runs with argc == 0 (or 1), a binary operator
 * runs with argc == 2 and reads its operands from argv[0]/argv[1].
 * This keeps "one function per operation" as the only extension point.
 */
#define XO_EVAL_ARGS \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	xo_tframe_t *framep UNUSED, \
	xo_xparse_node_t *xnp UNUSED, int indent UNUSED, \
	int argc UNUSED, xo_eval_value_t *argv UNUSED
#define XO_EVAL_PASS xop, xfp, framep, xnp, indent, argc, argv

typedef xo_eval_value_t (*xo_eval_fn_t)(XO_EVAL_ARGS);

#define XO_EVAL_CALC_ARGS \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	xo_eval_value_t left UNUSED, xo_eval_value_t right UNUSED

typedef xo_eval_value_t (*xo_eval_calc_fn_t)(XO_EVAL_CALC_ARGS);

/*
 * Reports an evaluation-time error through xfp's data context, the same
 * path xo_eval_compile() will use for compile-time errors.  Replaces a
 * direct xo_failure_filter(xop, ...) call so these errors also reach a
 * non-libxo backend (e.g. libpin) when it is running the same eval code.
 */
XO_PRINTFLIKE(2, 3)
static void
xo_eval_fail (xo_filter_t *xfp, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_filter_data_verror(xfp->xf_data, xfp->xf_ops, fmt, vap);
    va_end(vap);
}

/*
 * Line/column of the node passed to the most recent xo_eval_compile_fail()
 * call, so a caller with no (dp, ops)-reachable error channel (e.g. libpin,
 * which runs compile-time checks with ops->xfdo_error unset) can still
 * recover *where* a failed xo_eval_compile() pointed, via
 * xo_eval_compile_fail_location() below.  0 means "no node"/unknown.
 */
static unsigned xo_eval_compile_fail_line;
static unsigned xo_eval_compile_fail_col;

/*
 * The compile-time twin of xo_eval_fail(): xo_eval_compile() and its
 * helpers only ever see the (dp, ops) pair, not a full xo_filter_t, so
 * they report through xo_filter_data_verror() directly instead.
 *
 * xdp/id identify the AST node the failure is attributed to, so its
 * line/col (stamped by the lexer; see xo_xparse_node_t) can be stashed
 * for xo_eval_compile_fail_location() regardless of whether ops has an
 * xfdo_error to report through.
 */
XO_PRINTFLIKE(5, 6)
static void
xo_eval_compile_fail (xo_filter_data_t *dp, xo_filter_data_ops_t *ops,
		       xo_xparse_data_t *xdp, xo_xparse_node_id_t id,
		       const char *fmt, ...)
{
    va_list vap;

    if (xdp != NULL) {
	xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);
	xo_eval_compile_fail_line = xnp ? xnp->xn_line : 0;
	xo_eval_compile_fail_col = xnp ? xnp->xn_col : 0;
    } else {
	xo_eval_compile_fail_line = 0;
	xo_eval_compile_fail_col = 0;
    }

    va_start(vap, fmt);
    xo_filter_data_verror(dp, ops, fmt, vap);
    va_end(vap);
}

/*
 * Returns the line/col stashed by the most recent xo_eval_compile_fail()
 * call (0/0 if none or unknown).  See xo_filter.h for the public
 * declaration; used by callers (e.g. libpin) that invoke xo_eval_compile()
 * directly with no xfdo_error set, and so get no other way to recover
 * *where* a compile failure occurred.
 */
void
xo_eval_compile_fail_location (unsigned *linep, unsigned *colp)
{
    if (linep)
	*linep = xo_eval_compile_fail_line;
    if (colp)
	*colp = xo_eval_compile_fail_col;
}

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

/*
 * NaN gets its own type (C_NAN, with no data) rather than living in a
 * C_FLOAT, so it can be detected by type alone.  xo_eval_value_float
 * makes sure a C_FLOAT never holds NaN.
 */
static inline xo_eval_value_t
xo_eval_value_nan (unsigned flags)
{
    return xo_eval_value_make(C_NAN, flags, 0);
}

static inline xo_eval_value_t
xo_eval_value_float (unsigned flags, xo_float_t val)
{
    xo_eval_value_t value = XO_EVAL_VALUE_ZERO;

    if (isnan(val))
	return xo_eval_value_nan(flags);

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
	 xo_xparse_node_id_t id, xo_eval_fn_t op_fn);

/*
 * True if only whitespace remains after a number
 */
static int
xo_eval_number_end (const char *cp)
{
    while (isspace((unsigned char) *cp))
	cp += 1;

    return (*cp == '\0');
}

/*
 * Turn a string into a number, as XPath's number() does, but keep it
 * as strongly typed as we can: C_INT64, then C_UINT64 (for "0x" hex
 * or values past INT64_MAX), and C_FLOAT only when there's no other
 * choice, since floats are expensive and lose precision for large
 * integers.  Anything else (including the empty string we get for a
 * missing field) is XPath's NaN, which we return as C_NAN.  Leading
 * and trailing whitespace is allowed.  This doesn't complain; in
 * XPath, a non-numeric string is a normal value.
 */
static xo_eval_value_t
xo_eval_parse_number (const char *str)
{
    xo_eval_value_t value;
    char *ep;

    if (str == NULL)
	return xo_eval_value_nan(0);

    while (isspace((unsigned char) *str))
	str += 1;

    /* XPath numbers are digits, '.', and '-'; no "inf", "nan", or '+' */
    if (!isdigit((unsigned char) *str) && *str != '.' && *str != '-')
	return xo_eval_value_nan(0);

    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
	errno = 0;
	uint64_t uval = strtoull(str, &ep, 16);
	if (errno != 0 || ep == str + 2 || !xo_eval_number_end(ep))
	    return xo_eval_value_nan(0);

	value = xo_eval_value_make(C_UINT64, 0, 0);
	value.xev_uint64 = uval;
	return value;
    }

    /* Base 10, since XPath has no octal ("010" is ten) */
    errno = 0;
    int64_t ival = strtoll(str, &ep, 10);
    if (ep != str && xo_eval_number_end(ep)) {
	if (errno == 0) {
	    value = xo_eval_value_make(C_INT64, 0, 0);
	    value.xev_int64 = ival;
	    return value;
	}

	if (*str != '-') {	/* Too big for int64; try uint64 */
	    errno = 0;
	    uint64_t uval = strtoull(str, &ep, 10);
	    if (errno == 0) {
		value = xo_eval_value_make(C_UINT64, 0, 0);
		value.xev_uint64 = uval;
		return value;
	    }
	}
    }

    xo_float_t fval = strtod(str, &ep);
    if (ep != str && xo_eval_number_end(ep))
	return xo_eval_value_float(0, fval);

    return xo_eval_value_nan(0);
}

/*
 * Like xo_eval_parse_number, but complains about a string that isn't
 * a number.  Blank strings (missing fields) are NaN without complaint.
 */
static xo_eval_value_t
xo_eval_make_number (xo_handle_t *xop, const char *str)
{
    xo_eval_value_t value = xo_eval_parse_number(str);

    if (value.xev_type == C_NAN && str != NULL && !xo_eval_number_end(str))
	/*
	 * No xfp here (xo_eval_cast_boolean/float/int64 pass only xop
	 * down this chain, and widening them is out of proportion to
	 * one rare, best-effort diagnostic), so this stays on the
	 * handle-based path rather than xo_eval_fail().
	 */
	xo_failure_filter(xop, "invalid number value: '%s'", str);

    return value;
}

static xo_eval_value_t
xo_eval_make_number_from_value (xo_handle_t *xop, xo_eval_value_t value)
{
    switch (value.xev_type) {
    case C_DSTRING:
    case C_STRING:;
	return xo_eval_make_number(xop, value.xev_str);

    case C_NAN:
    case C_FLOAT:
    case C_BOOLEAN:
    case C_UINT64:
    case C_INT64:
    default:
	return value;
    }
}

static xo_eval_value_t
xo_eval_position (XO_EVAL_ARGS)
{
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    uint64_t idx = str ? (uint64_t) strtoul(str, NULL, 10) : 0;
    uint32_t pos = framep->xtf_position_cur;
    xo_eval_value_t value = xo_eval_value_make(C_BOOLEAN, 0, 0);
    value.xev_uint64 = (pos != 0 && pos == idx) ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_number (XO_EVAL_ARGS)
{
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    return xo_eval_make_number(xop, str);
}

static xo_eval_value_t
xo_eval_quoted (XO_EVAL_ARGS)
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
xo_eval_attribute (XO_EVAL_ARGS)
{
    xo_eval_value_t value = { .xev_flags = 0 };
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    const char *aval;
    if (xfp->xf_ops && xfp->xf_ops->xfdo_value_of) {
	/*
	 * Mark this as a bare attribute reference (vs a compound element
	 * path, which xo_eval_path builds and passes unmarked) by
	 * prefixing '@', matching the convention xfdo_value_of backends
	 * are documented against (see pin_filter_data_value_of_tree's
	 * "trailing @x" doc comment in libpin).  Without this, a backend
	 * whose xfdo_value_of has no other way to tell "@foo" apart from
	 * a child-element path named "foo" has no choice but to guess.
	 */
	size_t slen = strlen(str) + 1; /* Length of "@" + str */
	char buf[slen + 1];

	buf[0] = '@';
	memcpy(buf + 1, str, slen - 1);
	buf[slen] = '\0';
	aval = xfp->xf_ops->xfdo_value_of(xfp->xf_data, buf,
					    (ssize_t) slen);
    } else {
	aval = xo_filter_attr_find(xfp, framep, str);
    }
    if (aval) {
	value = xo_eval_value_make(C_STRING, XEVF_NODE, 0);
	value.xev_str = aval;
    } else {
	value.xev_flags |= XEVF_MISSING;
    }
    return value;
}

/*
 * Find the value of a child element (or attribute) of the current
 * frame.  A value we find is marked XEVF_NODE, since XPath tests a
 * node for existence, not content: foo[name] is true for any "name",
 * even an empty or zero one.
 */
static xo_eval_value_t
xo_eval_node_value (xo_filter_t *xfp, xo_tframe_t *framep,
		    const char *str, int is_attr)
{
    xo_eval_value_t value = { .xev_flags = 0 };
    const char *sval;

    /*
     * A walk backend with xfdo_value_of does its own lookup and
     * doesn't feed keys via xo_filter_walk_key, so xtf_keys is empty
     * and the find functions would see nothing.
     */
    if (xfp->xf_ops && xfp->xf_ops->xfdo_value_of)
	sval = xfp->xf_ops->xfdo_value_of(xfp->xf_data, str, -1);
    else
	sval = is_attr
	    ? xo_filter_attr_find(xfp, framep, str)
	    : xo_filter_key_find(xfp, framep, str);

    if (sval && xfp->xf_ops && xfp->xf_ops->xfdo_value_of) {
	/*
	 * xfdo_value_of's result is only good until the next call, and
	 * an expression like concat(a, b) holds both values at once,
	 * so we need our own copy.
	 */
	char *copy = xo_strdup(sval);
	if (copy == NULL)
	    return xo_eval_value_invalid();
	value = xo_eval_value_make(C_DSTRING, XEVF_NODE, 0);
	value.xev_str = copy;
    } else if (sval) {
	/* Frame-buffered keys/attrs stay put for the whole eval */
	value = xo_eval_value_make(C_STRING, XEVF_NODE, 0);
	value.xev_str = sval;
    } else if (xfp->xf_flags & XFSF_FORCE_RESOLVE) {
	/*
	 * The field is absent: an empty node-set, whose string value
	 * is "", but which compares false to everything (see
	 * xo_eval_compare).
	 */
	value = xo_eval_value_make(C_STRING, XEVF_EMPTYSET, 0);
	value.xev_str = "";
    } else {
	value.xev_flags |= XEVF_MISSING;
    }

    return value;
}

static xo_eval_value_t
xo_eval_element (XO_EVAL_ARGS)
{
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);

    return xo_eval_node_value(xfp, framep, str, FALSE);
}

static xo_eval_value_t
xo_eval_variable (XO_EVAL_ARGS)
{
    xo_eval_value_t value = { .xev_flags = 0 };
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    const char *vval = NULL;

    if (str && str[0] == '$')
	str += 1;

    if (str && xfp->xf_ops && xfp->xf_ops->xfdo_variable_of)
	vval = xfp->xf_ops->xfdo_variable_of(xfp->xf_data, str, -1);

    if (vval) {
	value = xo_eval_value_make(C_STRING, 0, 0);
	value.xev_str = vval;
    } else {
	value.xev_flags |= XEVF_MISSING;
    }

    return value;
}

static xo_eval_value_t
xo_eval_path (XO_EVAL_ARGS)
{
    xo_eval_value_t value = { .xev_flags = 0 };
    char path_buf[256];
    ssize_t path_len = 0;
    int step_count = 0;
    int last_is_attr = FALSE;
    xo_xparse_node_id_t id;
    /*
     * Only xfdo_value_of backends use the '@'-marker convention (see
     * xo_eval_attribute); the generic framep-based lookups below
     * (xo_filter_attr_find/xo_filter_key_find) key on the bare name.
     */
    int use_at_marker = (xfp->xf_ops && xfp->xf_ops->xfdo_value_of) ? TRUE : FALSE;

    /* Collect all C_ELEMENT / C_ATTRIBUTE steps into a compound key string */
    for (id = xnp->xn_contents; id; id = xnp->xn_next) {
	xnp = xo_xparse_node(&xfp->xf_xd, id);
	if (xnp->xn_type == C_ELEMENT || xnp->xn_type == C_ATTRIBUTE) {
	    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
	    ssize_t slen = strlen(str);
	    if (step_count > 0) {
		if (path_len + 1 >= (ssize_t) sizeof(path_buf) - 1)
		    return xo_eval_value_invalid();
		path_buf[path_len++] = '/';
	    }
	    /*
	     * Mark an attribute step with a leading '@' (e.g. "a/b/@x" or
	     * bare "@x"), matching xo_eval_attribute's convention, so a
	     * backend's xfdo_value_of can tell an attribute step apart
	     * from a child-element step of the same name.
	     */
	    if (use_at_marker && xnp->xn_type == C_ATTRIBUTE) {
		if (path_len + 1 >= (ssize_t) sizeof(path_buf) - 1)
		    return xo_eval_value_invalid();
		path_buf[path_len++] = '@';
	    }
	    if (path_len + slen >= (ssize_t) sizeof(path_buf) - 1)
		return xo_eval_value_invalid();
	    memcpy(path_buf + path_len, str, slen);
	    path_len += slen;
	    path_buf[path_len] = '\0';
	    last_is_attr = (xnp->xn_type == C_ATTRIBUTE);
	    step_count += 1;
	} else if (xnp->xn_type == C_ABSOLUTE) {
	    /* skip */
	} else {
	    xo_eval_fail(xfp, "filter: non-element path member (%s)",
			 xo_xparse_fancy_token_name(xnp->xn_type));
	}
    }

    if (step_count == 0)
	return value;

    /* A compound path is always a key lookup; only a lone @attr isn't */
    int is_attr = (last_is_attr && step_count == 1);

    return xo_eval_node_value(xfp, framep, path_buf, is_attr);
}

static xo_eval_value_t
xo_eval_dot (XO_EVAL_ARGS)
{
    xo_eval_value_t value = { .xev_flags = 0 };

    /* '.' is the context node: this frame's own value */
    if (framep->xtf_self) {
	value = xo_eval_value_make(C_STRING, XEVF_NODE, 0);
	value.xev_str = framep->xtf_self;
    } else if (xfp->xf_flags & XFSF_FORCE_RESOLVE) {
	/* Absent value = empty string per XPath semantics */
	value = xo_eval_value_make(C_STRING, 0, 0);
	value.xev_str = "";
    } else {
	value.xev_flags |= XEVF_MISSING;
    }

    return value;
}

/*
 * Cast a value to a boolean, as XPath's boolean() does.  A node is
 * true because it exists, whatever its content, so foo[name] and
 * not(name) ask if "name" is there, even when its value is "0".
 * Other strings are true unless they are empty, and numbers are true
 * unless they are zero or NaN (a float is not truncated first, so 0.5
 * is true).  Nothing parses a string as a number here; a filter that
 * wants a field's value judged as a number says so, with number(name)
 * or name != 0.
 */
static int
xo_eval_cast_boolean (xo_handle_t *xop UNUSED, xo_eval_value_t value)
{
    if (value.xev_flags & XEVF_NODE)
	return 1;

    switch (value.xev_type) {
    case C_STRING:
    case C_DSTRING:
	return value.xev_str != NULL && value.xev_str[0] != '\0';

    case C_NAN:			/* XPath: NaN is false */
	return 0;

    case C_FLOAT:
	return value.xev_float != 0;

    case C_UINT64:
	return value.xev_uint64 != 0;

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

    case C_NAN:
	return NAN;

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

/*
 * Room for any number we turn into a string.  The integers need at
 * most 21 bytes, but XPath never uses an exponent, so a double is
 * written out in full.  The largest has 309 digits before the decimal
 * point; the smallest (denormal) needs "0.", 323 zeros and 17
 * significant digits.  With the sign and the NUL, that's 344.
 */
#define XO_EVAL_NUMBUF	352

/*
 * Format a finite, non-integral (or very large) double the way
 * XPath's number-to-string conversion (4.2) wants it: the fewest
 * significant digits that read back as the same value, and no
 * exponent ("0.1", not "0.10000000000000001" or "1e-01").  We use
 * "%.*e" to get the digits and the exponent, find the shortest
 * precision that round-trips through strtod, then place the decimal
 * point ourselves.  Digits are picked out with isdigit, so a locale
 * that uses ',' for the decimal point doesn't confuse us.
 */
static void
xo_eval_format_float (char *buf, size_t bufsiz, xo_float_t val)
{
    char tmp[64];
    char digits[24];
    int ndigits = 0;
    int exp10 = 0;
    int prec;

    for (prec = 1; prec <= 17; prec++) {
	snprintf(tmp, sizeof(tmp), "%.*e", prec - 1, val);
	if (strtod(tmp, NULL) == val)
	    break;
    }

    /* tmp looks like "-d.ddde+XX"; pull out the digits and exponent */
    const char *cp = tmp;
    if (*cp == '-')
	cp += 1;

    for (; *cp && *cp != 'e' && *cp != 'E'; cp++)
	if (isdigit((unsigned char) *cp) && ndigits < (int) sizeof(digits) - 1)
	    digits[ndigits++] = *cp;

    if (*cp)
	exp10 = atoi(cp + 1);

    /* Trailing zeros carry no information */
    while (ndigits > 1 && digits[ndigits - 1] == '0')
	ndigits -= 1;

    /* 'point' is how many digits come before the decimal point */
    int point = exp10 + 1;
    char *op = buf, *ep = buf + bufsiz - 1;

    if (val < 0 && op < ep)
	*op++ = '-';

    if (point <= 0) {
	/* "0.000ddd" */
	if (op < ep)
	    *op++ = '0';
	if (op < ep)
	    *op++ = '.';
	for (int i = point; i < 0 && op < ep; i++)
	    *op++ = '0';
	for (int i = 0; i < ndigits && op < ep; i++)
	    *op++ = digits[i];

    } else if (point >= ndigits) {
	/* "ddd000", no decimal point */
	for (int i = 0; i < ndigits && op < ep; i++)
	    *op++ = digits[i];
	for (int i = ndigits; i < point && op < ep; i++)
	    *op++ = '0';

    } else {
	/* "dd.ddd" */
	for (int i = 0; i < ndigits && op < ep; i++) {
	    if (i == point)
		*op++ = '.';
	    if (op < ep)
		*op++ = digits[i];
	}
    }

    *op = '\0';
}

static char *
xo_eval_cast_string (xo_handle_t *xop UNUSED, xo_eval_value_t value)
{
    char buf[XO_EVAL_NUMBUF];
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
	/*
	 * XPath's number-to-string conversion (4.2): integral values are
	 * rendered with no decimal point ("2", not "2.000000"); "%lf"
	 * always emits 6 decimal digits, which is wrong for the common
	 * case of integral arithmetic results (e.g. "$n - 1").  Test the
	 * magnitude before the cast, since casting a double outside
	 * int64_t's range is undefined.  Everything else goes through
	 * xo_eval_format_float.
	 */
	if (isinf(value.xev_float)) {
	    bp = (value.xev_float < 0) ? "-Infinity" : "Infinity";
	} else if (fabs(value.xev_float) < 1e18
		   && value.xev_float == (double) (int64_t) value.xev_float) {
	    snprintf(buf, sizeof(buf), "%" PRId64, (int64_t) value.xev_float);
	} else {
	    xo_eval_format_float(buf, sizeof(buf), value.xev_float);
	}
	break;

    case C_NAN:
	bp = "NaN";
	break;

    case M_ERROR:
	bp = "";		/* Silently ignore; cast to "" */
	break;

    default:
	bp = "(unknown)";
    }

    return xo_strdup(bp);
}

static void
xo_eval_dump_value (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		     xo_eval_value_t value,
		     int indent UNUSED, const char *title UNUSED)
{
    char buf[XO_EVAL_NUMBUF];
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

    case C_NAN:
	bp = "NaN";
	break;

    default:
	bp = "(unknown)";
    }

    const char *type UNUSED = xo_xparse_fancy_token_name(value.xev_type) ?: "";

    XO_DBG(xop, "%*s%s: type '%s' (%u), flags %#x(%s%s%s%s%s%s), "
	   "node %lu, val '%s'",
	   indent, "", title ?: "",
	   type, value.xev_type, value.xev_flags,
	   (value.xev_flags & XEVF_INVALID) ? "+invalid" : "",
	   (value.xev_flags & XEVF_MISSING) ? "+missing" : "",
	   (value.xev_flags & XEVF_UNSUPPORTED) ? "+unsupported" : "",
	   (value.xev_flags & XEVF_FINAL) ? "+final" : "",
	   (value.xev_flags & XEVF_NODE) ? "+node" : "",
	   (value.xev_flags & XEVF_EMPTYSET) ? "+emptyset" : "",
	   value.xev_node, bp);
}

#define TYPE_CMP(_a, _b) (((_a) << 16) | (_b))

#define XO_REL_NAN	2	/* xo_eval_compare: comparison is NaN */

static inline int
xo_eval_is_number_type (unsigned type)
{
    return (type == C_INT64 || type == C_UINT64 || type == C_FLOAT
	    || type == C_NAN);
}

/*
 * Compare two floats.  C_FLOAT never holds NaN (that's C_NAN), so
 * this is a simple three-way compare.
 */
static inline int
xo_eval_fcmp (xo_float_t left, xo_float_t right)
{
    return (left > right) ? 1 : (left < right) ? -1 : 0;
}

/*
 * Three-way compare of two values, giving -1, 0, or 1 as a C_INT64.
 * NaN is unordered (not equal to, less than, or greater than anything,
 * itself included), which we report as XO_REL_NAN.  A string compared
 * with a number is turned into a number (XPath 1.0, section 3.4),
 * keeping both as strongly typed as we can, so integers are compared
 * as integers, not floats.
 */
static xo_eval_value_t
xo_eval_compare (XO_EVAL_ARGS)
{
    xo_eval_value_t left = argv[0];
    xo_eval_value_t right = argv[1];
    xo_eval_value_t value = XO_EVAL_VALUE_ZERO;
    int rc = 0;
    int lbool, rbool;

    xo_eval_dump_value(xop, xfp, left, indent, "compare: left");
    xo_eval_dump_value(xop, xfp, right, indent, "compare: right");

    unsigned left_type
	= (left.xev_type == C_DSTRING) ? C_STRING : left.xev_type;
    unsigned right_type
	= (right.xev_type == C_DSTRING) ? C_STRING : right.xev_type;

    /*
     * XPath compares a node-set by its members: true if any one of
     * them compares true.  An absent field is an empty node-set, so
     * name = '' and name != '' are both false when there's no name.
     * Against a boolean, a node-set compares as boolean(), which is
     * false for an empty one, so the boolean case below handles it.
     */
    if (((left.xev_flags | right.xev_flags) & XEVF_EMPTYSET)
	    && left_type != C_BOOLEAN && right_type != C_BOOLEAN)
	return xo_eval_value_invalid();

    /* The converted strings are numbers, so there's nothing to free */
    if (left_type == C_STRING && xo_eval_is_number_type(right_type)) {
	left = xo_eval_make_number(xop, left.xev_str);
	left_type = left.xev_type;
    } else if (right_type == C_STRING && xo_eval_is_number_type(left_type)) {
	right = xo_eval_make_number(xop, right.xev_str);
	right_type = right.xev_type;
    }

    /* Against a boolean, NaN is false (handled below); else unordered */
    if ((left_type == C_NAN && right_type != C_BOOLEAN)
	    || (right_type == C_NAN && left_type != C_BOOLEAN)) {
	value.xev_int64 = XO_REL_NAN;
	xo_eval_dump_value(xop, xfp, value, indent, "compare: results");
	return value;
    }

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
	rc = xo_eval_fcmp(left.xev_float, right.xev_float);
	break;

    case TYPE_CMP(C_INT64, C_FLOAT):
    case TYPE_CMP(C_UINT64, C_FLOAT):
	rc = xo_eval_fcmp(xo_eval_cast_float(xop, left), right.xev_float);
	break;

    case TYPE_CMP(C_FLOAT, C_INT64):
    case TYPE_CMP(C_FLOAT, C_UINT64):
	rc = xo_eval_fcmp(left.xev_float, xo_eval_cast_float(xop, right));
	break;

    case TYPE_CMP(C_BOOLEAN, C_BOOLEAN):
    case TYPE_CMP(C_INT64, C_BOOLEAN):
    case TYPE_CMP(C_BOOLEAN, C_INT64):
    case TYPE_CMP(C_UINT64, C_BOOLEAN):
    case TYPE_CMP(C_BOOLEAN, C_UINT64):
    case TYPE_CMP(C_FLOAT, C_BOOLEAN):
    case TYPE_CMP(C_BOOLEAN, C_FLOAT):
    case TYPE_CMP(C_STRING, C_BOOLEAN):
    case TYPE_CMP(C_BOOLEAN, C_STRING):
    case TYPE_CMP(C_NAN, C_BOOLEAN):
    case TYPE_CMP(C_BOOLEAN, C_NAN):
	/*
	 * XPath converts the other operand to a boolean when either
	 * one is a boolean; false orders before true.
	 */
	lbool = xo_eval_cast_boolean(xop, left);
	rbool = xo_eval_cast_boolean(xop, right);
	rc = (lbool == rbool) ? 0 : lbool ? 1 : -1;
	break;

    default:
	if (xop != NULL && (xo_get_flags(xop) & XOF_WARN)) {
	    static const char unk[] = "(unknown)";
	    const char *lname = xo_xparse_token_name(left.xev_type) ?: unk;
	    const char *rname = xo_xparse_token_name(right.xev_type) ?: unk;
	    xo_eval_fail(xfp,
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
xo_eval_op_and (XO_EVAL_ARGS)
{
    xo_eval_value_t left = argv[0];
    xo_eval_value_t right = argv[1];
    xo_eval_value_t value = xo_eval_value_make(C_BOOLEAN, 0, 0);

    int bool_val = xo_eval_cast_boolean(xop, left);
    if (!bool_val && !(left.xev_flags & XEVF_MISSING)) {
	value.xev_int64 = 0;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    bool_val = xo_eval_cast_boolean(xop, right);
    if (!bool_val) {
	value.xev_int64 = 0;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    value.xev_int64 = 1;
    return value;
}

static xo_eval_value_t
xo_eval_op_or (XO_EVAL_ARGS)
{
    xo_eval_value_t left = argv[0];
    xo_eval_value_t right = argv[1];
    xo_eval_value_t value = xo_eval_value_make(C_BOOLEAN, 0, 0);

    int bool_val = xo_eval_cast_boolean(xop, left);
    if (bool_val) {
	value.xev_int64 = 1;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    bool_val = xo_eval_cast_boolean(xop, right);
    if (bool_val) {
	value.xev_int64 = 1;
	value.xev_flags |= XEVF_FINAL;

	return value;
    }

    value.xev_int64 = 0;
    return value;
}

/*
 * Turn a comparison into a boolean result.  xo_eval_compare's invalid
 * value has an xev_int64 of zero, which would read as "equal", so an
 * unsupported comparison is false whatever the operator.
 */
static xo_eval_value_t
xo_eval_compare_result (xo_eval_value_t cmp, int result)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;

    if (!(cmp.xev_flags & XEVF_INVALID))
	value.xev_int64 = result ? 1 : 0;

    return value;
}

static xo_eval_value_t
xo_eval_op_equals (XO_EVAL_ARGS)
{
    xo_eval_value_t cmp = xo_eval_compare(XO_EVAL_PASS);

    return xo_eval_compare_result(cmp, cmp.xev_int64 == 0);
}

static xo_eval_value_t
xo_eval_op_notequals (XO_EVAL_ARGS)
{
    xo_eval_value_t cmp = xo_eval_compare(XO_EVAL_PASS);

    return xo_eval_compare_result(cmp, cmp.xev_int64 != 0);
}

/*
 * Compare for the relational operators (<, <=, >, >=).  XPath 1.0
 * (section 3.4) compares two strings with these operators by first
 * converting both to numbers, not as strings, so "42" > "144" is
 * false.  We convert with xo_eval_parse_number, which keeps them as
 * strongly typed as it can and makes a non-number C_NAN, so
 * xo_eval_compare does the rest (and reports NaN as XO_REL_NAN).  No
 * complaints: in XPath, a non-numeric string is a normal value.
 */
static xo_eval_value_t
xo_eval_relational (XO_EVAL_ARGS)
{
    xo_eval_value_t left = argv[0];
    xo_eval_value_t right = argv[1];
    int left_str = (left.xev_type == C_STRING || left.xev_type == C_DSTRING);
    int right_str = (right.xev_type == C_STRING
		     || right.xev_type == C_DSTRING);

    if (!left_str || !right_str)
	return xo_eval_compare(XO_EVAL_PASS);

    /* Numbers own no memory; argv still owns (and frees) the strings */
    xo_eval_value_t nums[2] = {
	xo_eval_parse_number(left.xev_str),
	xo_eval_parse_number(right.xev_str),
    };

    return xo_eval_compare(xop, xfp, framep, xnp, indent, 2, nums);
}

static xo_eval_value_t
xo_eval_op_lt (XO_EVAL_ARGS)
{
    xo_eval_value_t cmp = xo_eval_relational(XO_EVAL_PASS);

    return xo_eval_compare_result(cmp, cmp.xev_int64 != XO_REL_NAN
				  && cmp.xev_int64 < 0);
}

static xo_eval_value_t
xo_eval_op_le (XO_EVAL_ARGS)
{
    xo_eval_value_t cmp = xo_eval_relational(XO_EVAL_PASS);

    return xo_eval_compare_result(cmp, cmp.xev_int64 != XO_REL_NAN
				  && cmp.xev_int64 <= 0);
}

static xo_eval_value_t
xo_eval_op_gt (XO_EVAL_ARGS)
{
    xo_eval_value_t cmp = xo_eval_relational(XO_EVAL_PASS);

    return xo_eval_compare_result(cmp, cmp.xev_int64 != XO_REL_NAN
				  && cmp.xev_int64 > 0);
}

static xo_eval_value_t
xo_eval_op_ge (XO_EVAL_ARGS)
{
    xo_eval_value_t cmp = xo_eval_relational(XO_EVAL_PASS);

    return xo_eval_compare_result(cmp, cmp.xev_int64 != XO_REL_NAN
				  && cmp.xev_int64 >= 0);
}

/* The magnitude of a negative int64 (safe for INT64_MIN) */
static inline uint64_t
xo_int64_mag (int64_t val)
{
    return (uint64_t) -(val + 1) + 1;
}

static inline xo_eval_value_t
xo_eval_value_int64 (int64_t ival)
{
    xo_eval_value_t value = xo_eval_value_make(C_INT64, 0, 0);

    value.xev_int64 = ival;
    return value;
}

/* Results that fit are C_INT64, matching xo_eval_parse_number */
static inline xo_eval_value_t
xo_eval_value_uint64 (uint64_t uval)
{
    if (uval <= INT64_MAX)
	return xo_eval_value_int64((int64_t) uval);

    xo_eval_value_t value = xo_eval_value_make(C_UINT64, 0, 0);

    value.xev_uint64 = uval;
    return value;
}

/*
 * XPath does all its math in floats, but floats are expensive and
 * lose precision past 2^53, so we keep integer math in C_INT64 or
 * C_UINT64 as long as the result fits, and fall back to C_FLOAT only
 * when it doesn't.  These helpers return the result, or an invalid
 * value (XEVF_INVALID) on overflow.
 */
static xo_eval_value_t
xo_int64_add (int64_t left, int64_t right)
{
    if ((right > 0 && left > INT64_MAX - right)
	    || (right < 0 && left < INT64_MIN - right))
	return xo_eval_value_invalid();

    return xo_eval_value_int64(left + right);
}

static xo_eval_value_t
xo_int64_sub (int64_t left, int64_t right)
{
    if ((right < 0 && left > INT64_MAX + right)
	    || (right > 0 && left < INT64_MIN + right))
	return xo_eval_value_invalid();

    return xo_eval_value_int64(left - right);
}

static xo_eval_value_t
xo_int64_mul (int64_t left, int64_t right)
{
    if (left > 0) {
	if (right > 0 ? left > INT64_MAX / right : right < INT64_MIN / left)
	    return xo_eval_value_invalid();
    } else if (left < 0) {
	if (right > 0 ? left < INT64_MIN / right
		: right != 0 && left < INT64_MAX / right)
	    return xo_eval_value_invalid();
    }

    return xo_eval_value_int64(left * right);
}

static xo_eval_value_t
xo_uint64_add (uint64_t left, uint64_t right)
{
    if (left > UINT64_MAX - right)
	return xo_eval_value_invalid();

    return xo_eval_value_uint64(left + right);
}

static xo_eval_value_t
xo_uint64_sub (uint64_t left, uint64_t right)
{
    if (left < right)
	return xo_eval_value_invalid();

    return xo_eval_value_uint64(left - right);
}

static xo_eval_value_t
xo_uint64_mul (uint64_t left, uint64_t right)
{
    if (right != 0 && left > UINT64_MAX / right)
	return xo_eval_value_invalid();

    return xo_eval_value_uint64(left * right);
}

/* Get an integer value as an int64, if it fits */
static inline int
xo_eval_get_int64 (xo_eval_value_t value, int64_t *valp)
{
    if (value.xev_type == C_INT64) {
	*valp = value.xev_int64;
	return TRUE;
    }

    if (value.xev_type == C_UINT64 && value.xev_uint64 <= INT64_MAX) {
	*valp = (int64_t) value.xev_uint64;
	return TRUE;
    }

    return FALSE;
}

/* Get an integer value as a uint64, if it fits */
static inline int
xo_eval_get_uint64 (xo_eval_value_t value, uint64_t *valp)
{
    if (value.xev_type == C_UINT64) {
	*valp = value.xev_uint64;
	return TRUE;
    }

    if (value.xev_type == C_INT64 && value.xev_int64 >= 0) {
	*valp = (uint64_t) value.xev_int64;
	return TRUE;
    }

    return FALSE;
}

/* True for a negative int64 paired with a uint64 past INT64_MAX */
static inline int
xo_eval_neg_big (xo_eval_value_t neg, xo_eval_value_t big)
{
    return (neg.xev_type == C_INT64 && neg.xev_int64 < 0
	    && big.xev_type == C_UINT64 && big.xev_uint64 > INT64_MAX);
}

/*
 * Turn an operand into a number for arithmetic, keeping it as
 * strongly typed as we can: strings are parsed, booleans are 0 or 1.
 */
static xo_eval_value_t
xo_eval_to_number (xo_handle_t *xop, xo_eval_value_t value)
{
    switch (value.xev_type) {
    case C_DSTRING:
    case C_STRING:
	return xo_eval_make_number(xop, value.xev_str);

    case C_INT64:
    case C_UINT64:
    case C_FLOAT:
    case C_NAN:
	return value;

    case C_BOOLEAN:
	return xo_eval_value_int64(value.xev_int64 ? 1 : 0);

    default:
	return xo_eval_value_int64(value.xev_int64);
    }
}

/*
 * Run a binary arithmetic operator.  The calc_fn gets two numbers
 * (from xo_eval_to_number), neither of them NaN; NaN in is NaN out.
 */
static xo_eval_value_t
xo_eval_calc (XO_EVAL_ARGS, const char *name, xo_eval_calc_fn_t calc_fn)
{
    xo_eval_value_t left = xo_eval_to_number(xop, argv[0]);
    xo_eval_value_t right = xo_eval_to_number(xop, argv[1]);
    xo_eval_value_t result;

    if (left.xev_type == C_NAN || right.xev_type == C_NAN)
	result = xo_eval_value_nan(0);
    else
	result = calc_fn(xop, xfp, left, right);

    if (XO_HAS_DEBUG(xop)) {
	xo_eval_dump_value(xop, xfp, left, indent, "eval_calc: left");
	xo_eval_dump_value(xop, xfp, right, indent, "eval_calc: right");
	char nbuf[128];
	snprintf(nbuf, sizeof(nbuf), "eval_calc: %s", name);

	xo_eval_dump_value(xop, xfp, result, indent, nbuf);
    }

    return result;
}

static xo_eval_value_t
xo_eval_calc_plus (XO_EVAL_CALC_ARGS)
{
    int64_t li, ri;
    uint64_t lu, ru;
    xo_eval_value_t result;

    if (xo_eval_get_int64(left, &li) && xo_eval_get_int64(right, &ri)) {
	result = xo_int64_add(li, ri);
	if (!(result.xev_flags & XEVF_INVALID))
	    return result;
    }

    if (xo_eval_get_uint64(left, &lu) && xo_eval_get_uint64(right, &ru)) {
	result = xo_uint64_add(lu, ru);
	if (!(result.xev_flags & XEVF_INVALID))
	    return result;
    }

    /* A negative int64 plus a uint64 past INT64_MAX always fits */
    if (xo_eval_neg_big(left, right))
	return xo_eval_value_uint64(right.xev_uint64
				    - xo_int64_mag(left.xev_int64));
    if (xo_eval_neg_big(right, left))
	return xo_eval_value_uint64(left.xev_uint64
				    - xo_int64_mag(right.xev_int64));

    return xo_eval_value_float(0, xo_eval_cast_float(xop, left)
			       + xo_eval_cast_float(xop, right));
}

static xo_eval_value_t
xo_eval_op_plus (XO_EVAL_ARGS)
{
    return xo_eval_calc(XO_EVAL_PASS, "plus",
			       xo_eval_calc_plus);
}

static xo_eval_value_t
xo_eval_calc_minus (XO_EVAL_CALC_ARGS)
{
    int64_t li, ri;
    uint64_t lu, ru;
    xo_eval_value_t result;

    if (xo_eval_get_int64(left, &li) && xo_eval_get_int64(right, &ri)) {
	result = xo_int64_sub(li, ri);
	if (!(result.xev_flags & XEVF_INVALID))
	    return result;
    }

    if (xo_eval_get_uint64(left, &lu) && xo_eval_get_uint64(right, &ru)) {
	result = xo_uint64_sub(lu, ru);
	if (!(result.xev_flags & XEVF_INVALID))
	    return result;
    }

    /* A uint64 past INT64_MAX minus a negative int64 */
    if (xo_eval_neg_big(right, left)) {
	result = xo_uint64_add(left.xev_uint64,
			       xo_int64_mag(right.xev_int64));
	if (!(result.xev_flags & XEVF_INVALID))
	    return result;
    }

    return xo_eval_value_float(0, xo_eval_cast_float(xop, left)
			       - xo_eval_cast_float(xop, right));
}

static xo_eval_value_t
xo_eval_op_minus (XO_EVAL_ARGS)
{
    return xo_eval_calc(XO_EVAL_PASS, "minus",
			       xo_eval_calc_minus);
}

/*
 * Unary minus (negation), the compiled form of a C_UNI_MINUS node
 * (e.g. "-4"), as opposed to xo_eval_op_minus() above, which combines
 * two operands ("a - b").  Same semantics as xo_eval_negate(): an
 * integer stays an integer; everything else is negated as a number.
 * The runner frees argv[0], so we always return a fresh value.
 */
static xo_eval_value_t
xo_eval_op_negate (XO_EVAL_ARGS)
{
    xo_eval_value_t value = xo_eval_to_number(xop, argv[0]);

    switch (value.xev_type) {
    case C_INT64:
	if (value.xev_int64 != INT64_MIN)
	    return xo_eval_value_int64(-value.xev_int64);
	return xo_eval_value_uint64(xo_int64_mag(value.xev_int64));

    case C_UINT64:
	if (value.xev_uint64 <= INT64_MAX)
	    return xo_eval_value_int64(-(int64_t) value.xev_uint64);
	if (value.xev_uint64 == xo_int64_mag(INT64_MIN))
	    return xo_eval_value_int64(INT64_MIN);
	return xo_eval_value_float(0, -(xo_float_t) value.xev_uint64);

    case C_FLOAT:
	return xo_eval_value_float(0, -value.xev_float);

    default:			/* C_NAN */
	return value;
    }
}

/*
 * XPath's "div" is float division, but when integers divide evenly,
 * the result is still an integer.
 */
static xo_eval_value_t
xo_eval_calc_div (XO_EVAL_CALC_ARGS)
{
    int64_t li, ri;
    uint64_t lu, ru;

    if (xo_eval_cast_float(xop, right) == 0)
	return xo_eval_value_invalid();

    if (xo_eval_get_int64(left, &li) && xo_eval_get_int64(right, &ri)
	    && !(li == INT64_MIN && ri == -1) && li % ri == 0)
	return xo_eval_value_int64(li / ri);

    if (xo_eval_get_uint64(left, &lu) && xo_eval_get_uint64(right, &ru)
	    && lu % ru == 0)
	return xo_eval_value_uint64(lu / ru);

    return xo_eval_value_float(0, xo_eval_cast_float(xop, left)
			       / xo_eval_cast_float(xop, right));
}

static xo_eval_value_t
xo_eval_op_div (XO_EVAL_ARGS)
{
    return xo_eval_calc(XO_EVAL_PASS, "div",
			       xo_eval_calc_div);
}

static xo_eval_value_t
xo_eval_calc_mul (XO_EVAL_CALC_ARGS)
{
    int64_t li, ri;
    uint64_t lu, ru;
    xo_eval_value_t result;

    if (xo_eval_get_int64(left, &li) && xo_eval_get_int64(right, &ri)) {
	result = xo_int64_mul(li, ri);
	if (!(result.xev_flags & XEVF_INVALID))
	    return result;
    }

    if (xo_eval_get_uint64(left, &lu) && xo_eval_get_uint64(right, &ru)) {
	result = xo_uint64_mul(lu, ru);
	if (!(result.xev_flags & XEVF_INVALID))
	    return result;
    }

    return xo_eval_value_float(0, xo_eval_cast_float(xop, left)
			       * xo_eval_cast_float(xop, right));
}

static xo_eval_value_t
xo_eval_op_mul (XO_EVAL_ARGS)
{
    return xo_eval_calc(XO_EVAL_PASS, "mul",
			       xo_eval_calc_mul);
}

static xo_float_t
xo_fmod (xo_float_t x, xo_float_t y)
{
    if (y == 0)
	return 0;

    /*
     * XPath's mod takes the sign of the dividend, as C's fmod() does,
     * and fmod() works at any magnitude, where casting x / y to an
     * integer type is undefined past that type's range.
     */
    return fmod(x, y);
}

/*
 * XPath's "mod" truncates, like C's '%', so the result has the sign
 * of the dividend.  As with xo_fmod, "mod 0" gives zero.
 */
static xo_eval_value_t
xo_eval_calc_mod (XO_EVAL_CALC_ARGS)
{
    int64_t li, ri;
    uint64_t lu, ru;

    if (xo_eval_get_int64(left, &li) && xo_eval_get_int64(right, &ri))
	return xo_eval_value_int64((ri == 0 || ri == -1) ? 0 : li % ri);

    if (xo_eval_get_uint64(left, &lu) && xo_eval_get_uint64(right, &ru))
	return xo_eval_value_uint64((ru == 0) ? 0 : lu % ru);

    /* A negative int64 is smaller than any uint64 past INT64_MAX */
    if (xo_eval_neg_big(left, right))
	return (xo_int64_mag(left.xev_int64) == right.xev_uint64)
	    ? xo_eval_value_int64(0) : left;

    if (xo_eval_neg_big(right, left))
	return xo_eval_value_uint64(left.xev_uint64
				    % xo_int64_mag(right.xev_int64));

    return xo_eval_value_float(0, xo_fmod(xo_eval_cast_float(xop, left),
					  xo_eval_cast_float(xop, right)));
}

static xo_eval_value_t
xo_eval_op_mod (XO_EVAL_ARGS)
{
    return xo_eval_calc(XO_EVAL_PASS, "mod",
			       xo_eval_calc_mod);
}

static xo_eval_value_t
xo_eval_not (XO_EVAL_ARGS)
{
    if (argv[0].xev_flags & XEVF_MISSING)
	return xo_eval_value_missing();

    int bool_val = xo_eval_cast_boolean(xop, argv[0]);
    xo_eval_value_t value = xo_eval_value_make(C_BOOLEAN, 0, 0);
    value.xev_int64 = bool_val ? 0 : 1; /* Perform the 'not' */
    return value;
}

/*
 * Unary minus: the grammar gives us a C_UNI_MINUS node, whose
 * contents are the single operand.  An integer stays an integer, so
 * "-5" is still "5" with a sign when it becomes a string; everything
 * else is negated as a number.
 */
static xo_eval_value_t
xo_eval_negate (XO_EVAL_ARGS)
{
    xo_eval_value_t value;

    value = xo_eval(xop, xfp, framep, "arguments", indent,
                       xnp->xn_contents, NULL);
    xo_eval_dump_value(xop, xfp, value, indent, "xo_eval_negate");

    if (value.xev_flags & XEVF_MISSING)
	return value;

    /* The most negative integer has no positive twin; use a float */
    if (value.xev_type == C_INT64 && value.xev_int64 != INT64_MIN) {
	value.xev_int64 = -value.xev_int64;
	return value;
    }

    xo_float_t fval = xo_eval_cast_float(xop, value);
    xo_eval_value_free(value);

    return xo_eval_value_float(0, -fval);
}

/*
 * Look at a arguments to a function and return the count
 */
static int UNUSED
xo_eval_argument_count (XO_EVAL_ARGS)
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
xo_eval_arguments (XO_EVAL_ARGS,
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
xo_eval_arguments_free (XO_EVAL_ARGS)
{
    for (int i = 0; i < argc; i++)
	xo_eval_value_free(argv[i]);
}

static xo_eval_value_t
xo_eval_func_starts_with (XO_EVAL_ARGS)
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
xo_eval_func_ends_with (XO_EVAL_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;

    char *base = xo_eval_cast_string(xop, argv[0]);
    char *endp = xo_eval_cast_string(xop, argv[1]);
    XO_DBG(xop, "ends_with: '%s' '%s'", base ?: "", endp ?: "");

    if (base && endp) {
	size_t blen = strlen(base);
	size_t elen = strlen(endp);

	/*
	 * A suffix longer than the string can't match, and would
	 * index before the start of base
	 */
	if (elen <= blen && memcmp(base + blen - elen, endp, elen) == 0)
	    value.xev_int64 = TRUE;
    }

    if (base)
	xo_free(base);
    if (endp)
	xo_free(endp);

    return value;
}

static xo_eval_value_t
xo_eval_func_true (XO_EVAL_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_TRUE;
    return value;
}

static xo_eval_value_t
xo_eval_func_false (XO_EVAL_ARGS)
{
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;
    return value;
}

static xo_eval_value_t
xo_eval_func_boolean (XO_EVAL_ARGS)
{
    int bool_val = xo_eval_cast_boolean(xop, argv[0]);

    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;
    value.xev_int64 = bool_val ? 1 : 0;
    return value;
}

static xo_eval_value_t
xo_eval_func_string (XO_EVAL_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    return xo_eval_value_string(C_DSTRING, 0, str);
}

static xo_eval_value_t
xo_eval_func_normalize_space (XO_EVAL_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    const char *p = str ?: "";

    char *out = xo_realloc(NULL, strlen(p) + 1);
    if (out == NULL) {
	xo_free(str);
	xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
	result.xev_str = xo_strdup("");
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
xo_eval_func_not (XO_EVAL_ARGS)
{
    if (argc < 1)
	return xo_eval_value_invalid();

    int bool_val = xo_eval_cast_boolean(xop, argv[0]);
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;
    value.xev_int64 = bool_val ? 0 : 1;
    return value;
}

/*
 * ceiling(), floor(), and round(): integers (and NaN) are already
 * their own answer.  A float's integral result is a C_INT64 when it
 * fits.
 */
static xo_eval_value_t
xo_eval_round_common (XO_EVAL_ARGS, xo_float_t (*round_fn)(xo_float_t))
{
    xo_eval_value_t value = xo_eval_to_number(xop, argv[0]);

    if (value.xev_type != C_FLOAT)
	return value;

    xo_float_t fval = round_fn(value.xev_float);

    /* -2^63 and 2^63 are exact as doubles; INT64_MAX isn't */
    if (fval >= (xo_float_t) INT64_MIN && fval < -(xo_float_t) INT64_MIN)
	return xo_eval_value_int64((int64_t) fval);

    return xo_eval_value_float(0, fval);
}

static xo_eval_value_t
xo_eval_func_ceiling (XO_EVAL_ARGS)
{
    return xo_eval_round_common(XO_EVAL_PASS, ceil);
}

static xo_eval_value_t
xo_eval_func_floor (XO_EVAL_ARGS)
{
    return xo_eval_round_common(XO_EVAL_PASS, floor);
}

/*
 * Round a value as XPath's round() does, keeping to integers when we
 * can.  Most arguments are integers already (a literal, or a field
 * that xo_eval_parse_number reads as one), and those come back as
 * C_INT64 without touching floating point.  Only a real C_FLOAT is
 * rounded, by floor(x + 0.5), since XPath rounds a half up, toward
 * positive infinity (round(-2.5) is -2, where C's round() gives -3).
 * A result that fits comes back as C_INT64 too; the infinities and
 * anything else beyond int64_t stay C_FLOAT, and NaN stays C_NAN, for
 * the caller's floating point path.
 */
static xo_eval_value_t
xo_eval_cast_round (xo_handle_t *xop, xo_eval_value_t value)
{
    value = xo_eval_make_number_from_value(xop, value);

    switch (value.xev_type) {
    case C_INT64:
	return value;

    case C_UINT64:
	if (value.xev_uint64 <= INT64_MAX) {
	    int64_t ival = (int64_t) value.xev_uint64;
	    value = xo_eval_value_make(C_INT64, 0, 0);
	    value.xev_int64 = ival;
	} else {
	    xo_float_t fval = (xo_float_t) value.xev_uint64;
	    value = xo_eval_value_float(0, fval);
	}
	return value;

    case C_FLOAT:;
	xo_float_t fval = floor(value.xev_float + 0.5);

	/* 2^63 is exact as a double */
	if (fval >= -9223372036854775808.0 && fval < 9223372036854775808.0) {
	    value = xo_eval_value_make(C_INT64, 0, 0);
	    value.xev_int64 = (int64_t) fval;
	} else {
	    value = xo_eval_value_float(0, fval);
	}
	return value;

    case C_NAN:			/* Not zero; xo_eval_cast_float gives NAN */
	return value;

    case C_BOOLEAN:
    default:
	value.xev_int64 = value.xev_int64 ? 1 : 0;
	value.xev_type = C_INT64;
	return value;
    }
}

static xo_eval_value_t
xo_eval_func_substring (XO_EVAL_ARGS)
{
    if (argc < 2 || argc > 3) {
	xo_eval_fail(xfp, "substring() requires 2 or 3 arguments, got %d",
		     argc);
	return xo_eval_value_invalid();
    }

    /* Defer if arguments aren't resolved yet (field not yet seen) */
    for (int i = 0; i < argc; i++)
	if (argv[i].xev_flags & XEVF_MISSING)
	    return xo_eval_value_missing();

    xo_eval_value_t value = xo_eval_value_make(C_STRING, 0, 0);
    value.xev_str = "";

    char *str = xo_eval_cast_string(xop, argv[0]);

    /*
     * The spec says the result holds the characters whose position
     * is at least round(pos) and, with a third argument, less than
     * round(pos) + round(len), with IEEE 754 comparisons, which gives
     * these odd cases:
     *
     *   substring("12345", 0, 3)              "12"
     *   substring("12345", 1.5, 2.6)          "234"
     *   substring("12345", 0 div 0, 3)        ""      (NaN)
     *   substring("12345", -42, 1 div 0)      "12345"
     *   substring("12345", -1 div 0, 1 div 0) ""      (-inf + inf is NaN)
     *
     * We clamp [start, limit) to the string's own positions,
     * [1, nchars + 1), and take what's left, if anything.  Positions
     * are in characters, not bytes.
     */
    xo_eval_value_t pv = xo_eval_cast_round(xop, argv[1]);
    xo_eval_value_t lv = XO_EVAL_VALUE_ZERO;
    if (argc == 3)
	lv = xo_eval_cast_round(xop, argv[2]);

    if (str == NULL)
	return value;

    int64_t nchars = xo_ustrlen(str);
    int64_t first = 1, last = 0;

    if (pv.xev_type == C_INT64 && lv.xev_type == C_INT64) {
	/*
	 * The common case, all in integers.  A sum past int64_t
	 * saturates, which leaves the clamping below unchanged.
	 */
	int64_t start = pv.xev_int64, len = lv.xev_int64, limit;

	if (argc == 2)
	    limit = INT64_MAX;
	else if (len > 0 && start > INT64_MAX - len)
	    limit = INT64_MAX;
	else if (len < 0 && start < INT64_MIN - len)
	    limit = INT64_MIN;
	else
	    limit = start + len;

	first = (start < 1) ? 1 : start;
	last = (limit > nchars + 1) ? nchars + 1 : limit;

    } else {
	/*
	 * NaN, an infinity, or something too big for int64_t.  Since
	 * every comparison with NaN is false, a NaN start or limit
	 * survives the clamping and fails "ffirst < flast", giving
	 * the empty string.
	 */
	xo_float_t start = xo_eval_cast_float(xop, pv);
	xo_float_t limit = (argc == 2) ? INFINITY
	    : start + xo_eval_cast_float(xop, lv);
	xo_float_t ffirst = (start < 1) ? 1 : start;
	xo_float_t flast = (limit > nchars + 1) ? nchars + 1 : limit;

	/* Both are now finite, within [1, nchars + 1] */
	if (ffirst < flast) {
	    first = (int64_t) ffirst;
	    last = (int64_t) flast;
	}
    }

    if (first < last) {
	/*
	 * Walk to the first character we keep and then past the last,
	 * using xo_utf8_nnext so an invalid byte counts as one
	 * character, as it does in xo_ustrlen.  Both are within
	 * [1, nchars + 1], so we never step past the NUL.
	 */
	const char *strend = str + strlen(str);
	const char *cp = str, *ep;
	int64_t i;

	for (i = 1; i < first; i++)
	    cp = xo_utf8_nnext(cp, strend - cp);
	for (ep = cp; i < last; i++)
	    ep = xo_utf8_nnext(ep, strend - ep);

	char *sub = xo_strndup(cp, ep - cp);

	if (sub) {
	    value.xev_type = C_DSTRING;
	    value.xev_str = sub;
	}
    }

    xo_free(str);
    return value;
}

static xo_eval_value_t
xo_eval_func_substring_before (XO_EVAL_ARGS)
{
    xo_eval_value_t value = xo_eval_value_make(C_STRING, 0, 0);

    char *haystack = xo_eval_cast_string(xop, argv[0]);
    char *needle = xo_eval_cast_string(xop, argv[1]);
    char *found = (haystack && needle) ? strstr(haystack, needle) : NULL;

    if (found) {
	value.xev_type = C_DSTRING;
	value.xev_str = xo_strndup(haystack, found - haystack);
    } else {
	value.xev_str = "";
    }

    xo_free(haystack);
    xo_free(needle);
    return value;
}

static xo_eval_value_t
xo_eval_func_substring_after (XO_EVAL_ARGS)
{
    xo_eval_value_t value = xo_eval_value_make(C_STRING, 0, 0);

    char *haystack = xo_eval_cast_string(xop, argv[0]);
    char *needle = xo_eval_cast_string(xop, argv[1]);
    char *found = (haystack && needle) ? strstr(haystack, needle) : NULL;

    if (found) {
	const char *after = found + strlen(needle);
	value.xev_type = C_DSTRING;
	value.xev_str = xo_strdup(after);
    } else {
	value.xev_str = "";
    }

    xo_free(haystack);
    xo_free(needle);
    return value;
}

static xo_eval_value_t
xo_eval_func_choose (XO_EVAL_ARGS)
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

    int bool_val = xo_eval_cast_boolean(xop, cond);
    xo_eval_value_free(cond);

    /* Locate the then and else node ids */
    xo_xparse_node_id_t then_id = xnp->xn_next;
    xnp = xo_xparse_node(&xfp->xf_xd, then_id);
    xo_xparse_node_id_t else_id = xnp->xn_next;

    xo_xparse_node_id_t branch_id = bool_val ? then_id : else_id;
    return xo_eval(xop, xfp, framep, bool_val ? "choose-then" : "choose-else",
		   indent + XO_INDENT, branch_id, NULL);
}

static xo_eval_value_t
xo_eval_func_choose2 (XO_EVAL_ARGS)
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
xo_eval_func_concat (XO_EVAL_ARGS)
{
    for (int i = 0; i < argc; i++)
	if (argv[i].xev_flags & XEVF_MISSING)
	    return xo_eval_value_missing();

    if (argc < 2) {
	xo_eval_fail(xfp, "function 'concat' requires "
		     "at least 2 arguments, got %d",
		     argc);
	xo_eval_value_t false_val = XO_EVAL_VALUE_BOOLEAN_FALSE;
	return false_val;
    }

    xo_buffer_t buf;
    xo_buf_init(&buf);

    for (int i = 0; i < argc; i++) {
	char *str = xo_eval_cast_string(xop, argv[i]);
	if (str) {
	    xo_buf_append(&buf, str, strlen(str));
	    xo_free(str);
	}
    }

    xo_buf_append(&buf, "", 1);	/* null terminator */
    xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
    result.xev_str = xo_strdup(xo_buf_data(&buf, 0));
    xo_buf_cleanup(&buf);
    return result;
}

static xo_eval_value_t
xo_eval_func_contains (XO_EVAL_ARGS)
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
xo_eval_func_number (XO_EVAL_ARGS)
{
    return xo_eval_make_number_from_value(xop, argv[0]);
}

static xo_eval_value_t
xo_eval_func_round (XO_EVAL_ARGS)
{
    return xo_eval_round_common(XO_EVAL_PASS, round);
}

static xo_eval_value_t
xo_eval_func_string_length (XO_EVAL_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    int64_t len = str ? (int64_t) xo_ustrlen(str) : 0;
    xo_free(str);
    return xo_eval_value_int64(len);
}

/*
 * isempty(x) is true when every node in x is empty.  Since a field
 * exists, x[name] can't ask if it has content, and string-length()
 * takes a number back to compare, so this is the direct test.  A
 * value holds at most one node, so "every node" is just the one; an
 * absent field is an empty nodeset, and so is empty as well.  That
 * comes for free: until the instance closes, a missing argument
 * leaves the predicate undecided (xo_eval_function returns missing),
 * and at close it resolves to "", which is empty.
 */
static xo_eval_value_t
xo_eval_func_isempty (XO_EVAL_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    xo_eval_value_t value = XO_EVAL_VALUE_BOOLEAN_FALSE;

    value.xev_int64 = (str == NULL || *str == '\0') ? 1 : 0;
    xo_free(str);
    return value;
}

/*
 * sum(a, b, ...) - sum all arguments converted to numbers.
 */
static xo_eval_value_t
xo_eval_func_sum (XO_EVAL_ARGS)
{
    for (int i = 0; i < argc; i++)
	if (argv[i].xev_flags & XEVF_MISSING)
	    return xo_eval_value_missing();

    /* Same rules as '+': integers as long as they fit, and NaN wins */
    xo_eval_value_t total = xo_eval_value_int64(0);

    for (int i = 0; i < argc; i++) {
	xo_eval_value_t value = xo_eval_to_number(xop, argv[i]);
	if (value.xev_type == C_NAN)
	    return value;

	total = xo_eval_calc_plus(xop, xfp, total, value);
    }

    return total;
}

/*
 * translate(string, from, to) - replace each char in string that appears
 * in from with the corresponding char in to; delete chars with no mapping.
 */
static xo_eval_value_t
xo_eval_func_translate (XO_EVAL_ARGS)
{
    char *str = xo_eval_cast_string(xop, argv[0]);
    char *from = xo_eval_cast_string(xop, argv[1]);
    char *to = xo_eval_cast_string(xop, argv[2]);

    if (str == NULL || from == NULL) {
	xo_free(str);
	xo_free(from);
	xo_free(to);
	xo_eval_value_t result = xo_eval_value_make(C_DSTRING, 0, 0);
	result.xev_str = xo_strdup("");
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
	result.xev_str = xo_strdup("");
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
	    /* else: no mapping - delete the character */
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
 * rematch(pattern, input, options?) - POSIX regex match.
 *
 * Options (each is a single character in the options string):
 *   'b'    Use basic RE (BRE) instead of the default extended RE (ERE)
 *   'i'    REG_ICASE - case-insensitive matching
 *   'n'    REG_NEWLINE - newline-sensitive matching
 *   '^'    REG_NOTBOL - '^' does not match at start of string
 *   '$'    REG_NOTEOL - '$' does not match at end of string
 *   's'    Return full match text (pmatch[0]) as C_DSTRING
 *   'm'    Return first capture group (pmatch[1]) as C_DSTRING;
 *   'mN'   Return capture group N (pmatch[N]) as C_DSTRING (N: 0-9)
 *   'p'    REG_POSIX (platform-specific; ignored if unavailable)
 *
 * Default (no 's' or 'm'): returns C_BOOLEAN true/false.
 * With 's' or 'm'/'mN': returns C_DSTRING - empty string if no match
 * or the requested group did not participate.
 *
 * Don't call it a regex, though I don't know what it is...
 */
static xo_eval_value_t
xo_eval_func_rematch (XO_EVAL_ARGS)
{
    if (argc < 2 || argc > 3) {
	xo_eval_fail(xfp, "rematch() requires 2 or 3 arguments, got %d",
		     argc);
	return xo_eval_value_invalid();
    }

    /* Defer if arguments aren't resolved yet (field not yet seen) */
    for (int i = 0; i < argc; i++)
	if (argv[i].xev_flags & XEVF_MISSING)
	    return xo_eval_value_missing();

    char *pattern = xo_eval_cast_string(xop, argv[0]);
    char *input   = xo_eval_cast_string(xop, argv[1]);

    /* Third arg is optional; absent means "" */
    char *opts = (argc >= 3) ? xo_eval_cast_string(xop, argv[2])
	: xo_strdup("");

    /* want_group: -1 = boolean; 0 = 's' (pmatch[0]); 1+ = 'm'/'mN' */
    int rflags = REG_EXTENDED;
    int want_group = -1;

    for (const char *op = opts; op && *op; op++) {
	switch (*op) {
	case 'b': rflags &= ~REG_EXTENDED; break;
	case 'i': rflags |= REG_ICASE;    break;
	case 'n': rflags |= REG_NEWLINE;  break;
	case '^': rflags |= REG_NOTBOL;   break;
	case '$': rflags |= REG_NOTEOL;   break;
	case 's': want_group = 0;         break;

	case 'm':
	    want_group = 1;		/* default: first capture group */
	    if (op[1] >= '0' && op[1] <= '9')
		want_group = *++op - '0';
	    break;

	case 'p':
#ifdef REG_POSIX
	    rflags |= REG_POSIX;
#endif
	    break;

	default:
	    xo_eval_fail(xfp, "unknown rematch() flag: '%c'", *op);
	}
    }

    /* For boolean mode we don't need match offsets */
    int ngroups = (want_group < 0) ? 0 : want_group + 1;

    xo_eval_value_t result;
    if (want_group < 0) {
	result = xo_eval_value_make(C_BOOLEAN, 0, 0); /* false */
    } else {
	result = xo_eval_value_make(C_DSTRING, 0, 0);
    }

    if (pattern == NULL || input == NULL)
	goto rematch_done;

    regex_t re;
    char errbuf[128];
    int rc = regcomp(&re, pattern, rflags);
    if (rc != 0) {
	regerror(rc, &re, errbuf, sizeof(errbuf));
	xo_eval_fail(xfp, "rematch: bad pattern '%s': %s", pattern, errbuf);
	goto rematch_done;
    }

    regmatch_t pmatch[10];
    bzero(pmatch, sizeof(pmatch));

    rc = regexec(&re, input, ngroups > 0 ? (size_t) ngroups : 0,
		 ngroups > 0 ? pmatch : NULL, 0);
    regfree(&re);

    if (rc == 0) {
	if (want_group < 0) {
	    result.xev_int64 = 1;	/* boolean true */
	} else {
	    regmatch_t *m = &pmatch[want_group];
	    if (m->rm_so >= 0) {
		int mlen = (int)(m->rm_eo - m->rm_so);
		result.xev_str = xo_strndup(input + m->rm_so, mlen);
	    }
	    /* else: group didn't participate - leave empty string */
	}
    }

 rematch_done:
    xo_free(pattern);
    xo_free(input);
    xo_free(opts);

    if (result.xev_type == C_DSTRING && result.xev_str == NULL)
	result.xev_str = xo_strdup(""); /* default value */

    return result;
}

/*
 * Map between names and numbers and functions, searchable by string
 * name.
 */
typedef uint32_t xo_eval_func_flags_t;

#define XEFF_NO_EVAL	(1<<0)	/* Function evaluates its own args (no infra) */
#define XEFF_NODE	(1<<1)	/* Function can return a node */

/*
 * Functions that return nodes:
 *
 * A value marked XEVF_NODE is a node that exists, and a predicate that
 * is just such a value (foo[name]) is an existence test, true whatever
 * the node's content.  Most functions compute a new value, but some
 * hand back one of their arguments as-is (choose2() returns its first
 * argument's own value), and an argument that was a node would carry
 * its mark out with it, turning foo[choose2(a, b)] into an existence
 * test of "a".  So xo_eval_function strips the mark from every result
 * unless the function's table entry has XEFF_NODE.
 *
 * A function that really does return a node must:
 * - have XEFF_NODE in its xo_eval_functions entry
 * - set XEVF_NODE itself on the value when it has a node to return
 * - return a value without XEVF_NODE (an empty string) when it finds
 *   no node, so the predicate is false, as it is for an absent element
 * - leave XEVF_NODE off anything it returns that is not a node, since
 *   XEFF_NODE means its results are trusted as marked
 *
 * Issues:
 * - A value holds a single string, so XEVF_NODE means "one node, and
 *   it exists".  There is no value type for a real nodeset of several
 *   nodes; a function that needs to return one needs a new type and
 *   the comparison rules that go with it (true if any member matches).
 * - Operators always strip the mark (see xo_eval), so "fn() + 1" and
 *   "fn() = 'x'" are computed values and never existence tests.
 */

typedef struct xo_eval_func_map_s {
    xo_eval_fn_t xfm_func;	/* The function that implements the logic */
    const char *xfm_name;	/* Name (e.g. "plus") */
    xo_eval_func_flags_t xfm_flags; /* Flags (XEFF_*) */
    int xfm_nargs;		/* Required arg count; -1 means don't checks */
    int xfm_min_args;		/* Minimum argc, checked at compile time
				   when xfm_nargs == -1; 0 means not
				   checked here, left to the function's
				   own runtime check */
} xo_eval_func_map_t;

xo_eval_func_map_t xo_eval_functions[] = {
    { xo_eval_func_boolean, "boolean", 0, 1, 0 },
    { xo_eval_func_ceiling, "ceiling", 0, 1, 0 },
    { xo_eval_func_choose, "choose", XEFF_NO_EVAL, 3, 0 },
    { xo_eval_func_choose2, "choose2", XEFF_NO_EVAL, 2, 0 },
    { xo_eval_func_concat, "concat", 0, -1, 2 },
    { xo_eval_func_contains, "contains", 0, 2, 0 },
    { xo_eval_func_ends_with, "ends-with", 0, 2, 0 },
    { xo_eval_func_false, "false", 0, 0, 0 },
    { xo_eval_func_floor, "floor", 0, 1, 0 },
    { xo_eval_func_isempty, "isempty", 0, 1, 0 },
    { xo_eval_func_normalize_space, "normalize-space", 0, 1, 0 },
    { xo_eval_func_not, "not", 0, 1, 0 },
    { xo_eval_func_number, "number", 0, 1, 0 },
    { xo_eval_func_rematch, "rematch", 0, -1, 2 },
    { xo_eval_func_round, "round", 0, 1, 0 },
    { xo_eval_func_starts_with, "starts-with", 0, 2, 0 },
    { xo_eval_func_string, "string", 0, 1, 0 },
    { xo_eval_func_string_length, "string-length", 0, 1, 0 },
    { xo_eval_func_substring, "substring", 0, -1, 2 },
    { xo_eval_func_substring_after, "substring-after", 0, 2, 0 },
    { xo_eval_func_substring_before, "substring-before", 0, 2, 0 },
    { xo_eval_func_sum, "sum", 0, -1, 1 },
    { xo_eval_func_translate, "translate", 0, 3, 0 },
    { xo_eval_func_true, "true", 0, 0, 0 },

    { NULL, NULL, 0, 0, 0 }
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
xo_eval_function (XO_EVAL_ARGS)
{
    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    if (str == NULL) {
	xo_eval_fail(xfp, "unknown function in filter expression");
	return xo_eval_value_invalid();
    }

    xo_eval_func_map_t *entry = xo_eval_find_func(xo_eval_functions, str);
    if (entry == NULL) {
	xo_eval_fail(xfp, "unknown function in filter expression: '%s'",
		     str);
	return xo_eval_value_invalid();
    }

    int fn_argc = xo_eval_argument_count(XO_EVAL_PASS);

    if (entry->xfm_nargs >= 0 && fn_argc != entry->xfm_nargs) {
	xo_eval_fail(xfp, "function '%s' requires %d argument(s), got %d",
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
	if (fn_argc && fn_argv == NULL) {
	    xo_failure_filter(xop, "function '%s': out of memory", str);
	    return xo_eval_value_invalid();
	}

	xo_eval_arguments(XO_EVAL_PASS, fn_argc, fn_argv);

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

    /*
     * Only a function that says it returns nodes gets to keep the
     * mark; for the rest it leaked in from an argument.
     */
    if (!(entry->xfm_flags & XEFF_NODE))
	value.xev_flags &= ~(XEVF_NODE | XEVF_EMPTYSET);

    xo_eval_dump_value(xop, xfp, value, indent, str);

    return value;
}

/*
 * Runtime op for a function call resolved at compile time by a
 * dynamic-load backend's xfdo_find_function hook (e.g. libslax's
 * "slax:" extension functions; see xo_eval_compile_function() below).
 * Re-derives the function's name from the AST node and re-calls the
 * hook here, rather than caching the resolved xo_eval_ext_fn_t in
 * xo_eval_instr_t: these calls are rare next to the built-in XPath
 * functions, so paying for the lookup again is cheaper than widening
 * every instruction to carry a backend-specific pointer.
 */
static xo_eval_value_t
xo_eval_func_external (XO_EVAL_ARGS)
{
    xo_filter_data_t *dp = xfp->xf_data;
    xo_filter_data_ops_t *ops = xfp->xf_ops;

    if (ops == NULL || ops->xfdo_find_function == NULL)
	return xo_eval_value_invalid();

    const char *str = xo_xparse_str(&xfp->xf_xd, xnp->xn_str);
    if (str == NULL)
	return xo_eval_value_invalid();

    int nargs, min_args;
    xo_eval_ext_fn_t fn = ops->xfdo_find_function(dp, str, &nargs, &min_args);
    if (fn == NULL) {
	xo_eval_fail(xfp, "unknown function in filter expression: '%s'", str);
	return xo_eval_value_invalid();
    }

    const char **args = alloca(argc * sizeof(*args));
    for (int i = 0; i < argc; i++)
	args[i] = xo_eval_cast_string(xop, argv[i]);

    xo_eval_ext_value_t rv = fn(dp, argc, args);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    for (int i = 0; i < argc; i++)
	xo_free((char *) args[i]);
#pragma GCC diagnostic pop

    switch (rv.xee_type) {
    case XO_EVAL_EXT_NUMBER:
	return xo_eval_value_float(0, rv.xee_num);

    case XO_EVAL_EXT_BOOLEAN:
	return rv.xee_bool ? xo_eval_value_boolean_true()
			    : xo_eval_value_boolean_false();

    case XO_EVAL_EXT_MISSING:
	return xo_eval_value_missing();

    case XO_EVAL_EXT_STRING:
    default:
	return xo_eval_value_string(C_DSTRING, 0,
				     strdup(rv.xee_str ? rv.xee_str : ""));
    }
}

static xo_eval_value_t
xo_eval (xo_handle_t *xop, xo_filter_t *xfp, xo_tframe_t *framep,
	 const char *pname, int indent,
	 xo_xparse_node_id_t id, xo_eval_fn_t op_fn)
{
    xo_eval_value_t value = xo_eval_value_invalid();
    xo_eval_value_t last = XO_EVAL_VALUE_ZERO;
    int first = 1;

    xo_xparse_node_t *xnp;
    xo_eval_fn_t node_fn = NULL;
    xo_eval_fn_t nested_op_fn = NULL;
    xo_eval_fn_t unary_op_fn = NULL;

    for (; id; id = xnp->xn_next) {
	xo_xparse_dump_one_node(&xfp->xf_xd, id, indent, "eval (loop): ");

	xnp = xo_xparse_node(&xfp->xf_xd, id);
	const char *cname = xo_xparse_token_name(xnp->xn_type);

	node_fn = NULL;
	nested_op_fn = NULL;
	unary_op_fn = NULL;

	switch (xnp->xn_type) {

	case C_ATTRIBUTE:
	    node_fn = xo_eval_attribute;
	    break;

	case C_PATH:
	    node_fn = xo_eval_path;
	    break;

	case C_ELEMENT:
	    node_fn = xo_eval_element;
	    break;

	case L_DOT:
	    node_fn = xo_eval_dot;
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

	case C_UNI_MINUS:
	    node_fn = xo_eval_negate;
	    break;

	case C_NOT:
	    unary_op_fn = xo_eval_not;
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

	case T_VAR:
	    node_fn = xo_eval_variable;
	    break;

	case C_EXPR:
	    if (xnp->xn_contents)
		value = xo_eval(xop, xfp, framep, pname, indent + XO_INDENT,
				xnp->xn_contents, NULL);
	    break;

	default:		/* For now; should be XEVF_UNSUPPORTED */
	    xo_eval_fail(xfp, "filter: unhandle type: '%s'",
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
	else if (unary_op_fn) {
	    xo_eval_value_t operand = xo_eval(xop, xfp, framep, cname,
					      indent + XO_INDENT,
					      xnp->xn_contents, NULL);
	    value = unary_op_fn(xop, xfp, framep, xnp, indent + XO_INDENT,
				1, &operand);
	    xo_eval_value_free(operand);
	}

	/*
	 * Operators and not() hand back computed values, even when
	 * they build them from an operand that was a node.  Functions
	 * are handled in xo_eval_function, which knows which ones
	 * return nodes.
	 */
	if (xnp->xn_type == C_NOT || xnp->xn_type == C_UNI_MINUS
	        || nested_op_fn)
	    value.xev_flags &= ~(XEVF_NODE | XEVF_EMPTYSET);

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
		xo_eval_value_t op_argv[2] = { last, value };
		xo_eval_value_t result = op_fn(xop, xfp, framep, xnp,
					       indent + XO_INDENT, 2, op_argv);
		xo_eval_value_free(last);
		xo_eval_value_free(value);
		value = result;
		value.xev_flags &= ~(XEVF_NODE | XEVF_EMPTYSET);
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

/*
 * A compiled XPath predicate: a flat, stack-machine instruction list
 * produced once by xo_eval_compile() and then replayed cheaply by the
 * runner for every node under test, instead of re-walking the AST
 * every time.
 */
/* Flags for xei_flags */
#define XEIF_CHECK_MISSING	(1<<0) /* Return MISSING instead of calling xei_fn if any arg is missing */

typedef struct xo_eval_instr_s {
	xo_eval_fn_t		xei_fn;		/* Operation to perform */
	xo_xparse_node_id_t	xei_node;	/* Source AST node */
	uint8_t			xei_argc;	/* Number of values this op consumes */
	uint8_t			xei_flags;	/* XEIF_* flags */
	uint16_t		xei_jump;	/* Short-circuit target; 0 means none */
} xo_eval_instr_t;

typedef struct xo_eval_prog_s {
	xo_eval_instr_t	*xep_instr;	/* Flat instruction array */
	uint32_t	 xep_count;	/* Number of instructions emitted */
	uint32_t	 xep_max;	/* Allocated size of xep_instr */
} xo_eval_prog_t;

/*
 * Forward decls.  All three are called from xo_filter_pred_prog() and
 * xo_filter_pred_eval() below, and are left non-static since they also
 * serve non-libxo backends (e.g. libpin) that compile their own select=/
 * test= expressions against this same eval engine.  dp/ops are the same
 * data-context pair xo_eval_fail() reports evaluation-time errors
 * through; passing NULL for both is fine and just means compile errors
 * go unreported, same as before this pair of parameters existed.
 */
xo_eval_prog_t *
xo_eval_compile (xo_xparse_data_t *xdp, xo_xparse_node_id_t root,
		  xo_filter_data_t *dp, xo_filter_data_ops_t *ops);

void
xo_eval_prog_destroy (xo_eval_prog_t *prog);

xo_eval_value_t
xo_eval_prog_run (xo_handle_t *xop, xo_filter_t *xfp, xo_tframe_t *framep,
		   xo_xparse_data_t *xdp, xo_eval_prog_t *prog, int indent);

/*
 * Append one instruction to 'prog', growing the array as needed.
 * Returns the new instruction's index, or -1 on allocation failure.
 */
static int
xo_eval_prog_emit (xo_eval_prog_t *prog, xo_eval_fn_t fn,
		    xo_xparse_node_id_t node, int argc, int flags)
{
    if (prog->xep_count >= prog->xep_max) {
	uint32_t new_max = prog->xep_max ? prog->xep_max * 2 : 16;
	xo_eval_instr_t *new_instr
	    = xo_realloc(prog->xep_instr, new_max * sizeof(*new_instr));
	if (new_instr == NULL)
	    return -1;

	prog->xep_instr = new_instr;
	prog->xep_max = new_max;
    }

    xo_eval_instr_t *xip = &prog->xep_instr[prog->xep_count];

    xip->xei_fn = fn;
    xip->xei_node = node;
    xip->xei_argc = argc;
    xip->xei_flags = flags;
    xip->xei_jump = 0;

    return prog->xep_count++;
}

/*
 * Count the nodes in an xn_next-linked operand chain.  Used to size
 * the backpatch array for short-circuiting K_AND/K_OR chains before
 * we know how many combine instructions the chain will emit.
 */
static int
xo_eval_count_chain (xo_xparse_data_t *xdp, xo_xparse_node_id_t id)
{
    int count = 0;

    for ( ; id; ) {
	xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);

	count += 1;
	id = xnp->xn_next;
    }

    return count;
}

static int
xo_eval_compile_node (xo_eval_prog_t *prog, xo_xparse_data_t *xdp,
		       xo_xparse_node_id_t id,
		       xo_filter_data_t *dp, xo_filter_data_ops_t *ops);

/*
 * Compile an xn_next-linked chain of operands under a binary
 * operator, emitting 'combine_fn' between each pair.  For K_AND/K_OR
 * (shortcircuit is TRUE), every combine instruction's xei_jump is
 * backpatched to the instruction past the end of the chain, so the
 * runner can bail out of the rest of the chain the moment a combine
 * sets XEVF_FINAL, matching xo_eval()'s own short-circuit behavior.
 */
static int
xo_eval_compile_chain (xo_eval_prog_t *prog, xo_xparse_data_t *xdp,
			xo_xparse_node_id_t id, xo_eval_fn_t combine_fn,
			int shortcircuit,
			xo_filter_data_t *dp, xo_filter_data_ops_t *ops)
{
    int nops = xo_eval_count_chain(xdp, id);
    if (nops < 2)
	return -1;

    int ncombines = nops - 1;
    uint32_t *combine_idx = NULL;

    if (shortcircuit) {
	combine_idx = xo_realloc(NULL, ncombines * sizeof(*combine_idx));
	if (combine_idx == NULL)
	    return -1;
    }

    int first = 1;
    int ci = 0;

    for ( ; id; ) {
	xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);

	if (xo_eval_compile_node(prog, xdp, id, dp, ops) < 0) {
	    if (combine_idx)
		xo_free(combine_idx);
	    return -1;
	}

	if (first) {
	    first = 0;

	} else {
	    /*
	     * Binary combine ops always require both operands present;
	     * mirrors the missing-check xo_eval()'s dispatch loop does
	     * before calling op_fn (it never reaches into xo_eval_compare()
	     * or the calc ops with a missing operand).
	     */
	    int idx = xo_eval_prog_emit(prog, combine_fn, id, 2,
					 XEIF_CHECK_MISSING);
	    if (idx < 0) {
		if (combine_idx)
		    xo_free(combine_idx);
		return -1;
	    }

	    if (shortcircuit)
		combine_idx[ci++] = idx;
	}

	id = xnp->xn_next;
    }

    if (shortcircuit) {
	uint16_t end_pc = prog->xep_count;

	for (int i = 0; i < ncombines; i++)
	    prog->xep_instr[combine_idx[i]].xei_jump = end_pc;

	xo_free(combine_idx);
    }

    return 0;
}

/*
 * Compile the xn_next-linked argument list under a function-call node,
 * emitting each argument's instructions in order.  Shared by the
 * built-in and xfdo_find_function fallback paths in
 * xo_eval_compile_function() below.  Returns the argument count, or -1
 * on a compile failure (already reported by the recursive call).
 */
static int
xo_eval_compile_function_args (xo_eval_prog_t *prog, xo_xparse_data_t *xdp,
				xo_xparse_node_t *xnp,
				xo_filter_data_t *dp, xo_filter_data_ops_t *ops)
{
    int argc = 0;
    xo_xparse_node_id_t aid = xnp->xn_contents;

    for ( ; aid; ) {
	xo_xparse_node_t *anp = xo_xparse_node(xdp, aid);

	if (xo_eval_compile_node(prog, xdp, aid, dp, ops) < 0)
	    return -1;

	argc += 1;
	aid = anp->xn_next;
    }

    return argc;
}

/*
 * Reject wrong-arity calls at compile time, matching
 * xo_eval_function()'s own runtime "nargs >= 0" check.  Without this, a
 * call like not() or contains(name) would silently compile and run
 * with the wrong argc.  Shared by the built-in and xfdo_find_function
 * fallback paths, which pass xfm_nargs/xfm_min_args and the hook's
 * nargs/min_args outputs respectively -- same -1/min_args convention
 * either way.
 */
static int
xo_eval_compile_check_arity (xo_filter_data_t *dp, xo_filter_data_ops_t *ops,
			      xo_xparse_data_t *xdp, xo_xparse_node_id_t id,
			      const char *str, int argc,
			      int nargs, int min_args)
{
    if (nargs >= 0 && argc != nargs) {
	xo_eval_compile_fail(dp, ops, xdp, id,
			      "function '%s' requires %d argument(s), got %d",
			      str, nargs, argc);
	return -1;
    }

    /*
     * Variable-arity functions (nargs == -1) normally defer their own
     * arity checking to runtime. concat()/rematch()/substring()/sum()
     * each have a compile-time-knowable minimum below which no amount
     * of data will ever make the call valid, so they set min_args to
     * catch that here.
     */
    if (nargs < 0 && min_args > 0 && argc < min_args) {
	xo_eval_compile_fail(dp, ops, xdp, id,
			      "function '%s' requires at least %d "
			      "arguments, got %d",
			      str, min_args, argc);
	return -1;
    }

    return 0;
}

/*
 * Compile a function call.  XEFF_NO_EVAL functions (choose/choose2)
 * manage their own lazy argument evaluation and still depend on the
 * old recursive xo_eval(), so they compile to a self-managing leaf
 * instruction; every other function compiles each argument first and
 * then emits one instruction with a static argc, matching the
 * argv[]-based convention the rest of the operations already use.
 *
 * A name that matches no built-in falls back to ops->xfdo_find_function,
 * giving a dynamic-load backend (e.g. libslax's "slax:" namespace
 * functions) a chance to resolve it; see that hook's doc comment in
 * xo_filter.h and xo_eval_func_external()'s above.  Only the arity
 * (returned by the hook itself) is used at compile time -- the
 * resolved xo_eval_ext_fn_t is not cached and is looked up again at
 * runtime.
 */
static int
xo_eval_compile_function (xo_eval_prog_t *prog, xo_xparse_data_t *xdp,
			   xo_xparse_node_id_t id, xo_xparse_node_t *xnp,
			   xo_filter_data_t *dp, xo_filter_data_ops_t *ops)
{
    const char *str = xo_xparse_str(xdp, xnp->xn_str);
    if (str == NULL) {
	xo_eval_compile_fail(dp, ops, xdp, id,
			      "unknown function in filter expression");
	return -1;
    }

    xo_eval_func_map_t *entry = xo_eval_find_func(xo_eval_functions, str);
    if (entry != NULL) {
	if (entry->xfm_flags & XEFF_NO_EVAL)
	    return xo_eval_prog_emit(prog, entry->xfm_func, id, 0, 0) >= 0
		? 0 : -1;

	int argc = xo_eval_compile_function_args(prog, xdp, xnp, dp, ops);
	if (argc < 0)
	    return -1;

	if (xo_eval_compile_check_arity(dp, ops, xdp, id, str, argc,
					 entry->xfm_nargs,
					 entry->xfm_min_args) < 0)
	    return -1;

	/*
	 * Fixed-arity functions get the same missing-check short-circuit
	 * as binary combine ops, matching xo_eval_function()'s own
	 * "entry->xfm_nargs > 0" check.  Variable-arity functions
	 * (xfm_nargs == -1, e.g. concat()/substring()) check
	 * XEVF_MISSING on their own argv where it matters, since not
	 * every argument position is required.
	 */
	int flags = (entry->xfm_nargs >= 0) ? XEIF_CHECK_MISSING : 0;

	return xo_eval_prog_emit(prog, entry->xfm_func, id, argc, flags)
	    >= 0 ? 0 : -1;
    }

    if (ops != NULL && ops->xfdo_find_function != NULL) {
	int nargs, min_args;
	xo_eval_ext_fn_t fn = ops->xfdo_find_function(dp, str,
						       &nargs, &min_args);
	if (fn != NULL) {
	    int argc = xo_eval_compile_function_args(prog, xdp, xnp, dp, ops);
	    if (argc < 0)
		return -1;

	    if (xo_eval_compile_check_arity(dp, ops, xdp, id, str, argc,
					     nargs, min_args) < 0)
		return -1;

	    int flags = (nargs >= 0) ? XEIF_CHECK_MISSING : 0;

	    return xo_eval_prog_emit(prog, xo_eval_func_external, id,
				      argc, flags) >= 0 ? 0 : -1;
	}
    }

    xo_eval_compile_fail(dp, ops, xdp, id,
			  "unknown function in filter expression: '%s'",
			  str);
    return -1;
}

/*
 * Compile a single expression node, mirroring the dispatch switch in
 * xo_eval().  Unlike xo_eval(), this never combines across an
 * xn_next chain itself; chains are handled explicitly by the
 * operators (and function arguments) that walk them.
 */
static int
xo_eval_compile_node (xo_eval_prog_t *prog, xo_xparse_data_t *xdp,
		       xo_xparse_node_id_t id,
		       xo_filter_data_t *dp, xo_filter_data_ops_t *ops)
{
    xo_xparse_node_t *xnp = xo_xparse_node(xdp, id);

    switch (xnp->xn_type) {

    case C_ATTRIBUTE:
	return xo_eval_prog_emit(prog, xo_eval_attribute, id, 0, 0) >= 0
	    ? 0 : -1;

    case C_PATH:
	return xo_eval_prog_emit(prog, xo_eval_path, id, 0, 0) >= 0
	    ? 0 : -1;

    case L_DOT:
	return xo_eval_prog_emit(prog, xo_eval_dot, id, 0, 0) >= 0
	    ? 0 : -1;

    case C_INDEX:
	return xo_eval_prog_emit(prog, xo_eval_position, id, 0, 0) >= 0
	    ? 0 : -1;

    case T_NUMBER:
	return xo_eval_prog_emit(prog, xo_eval_number, id, 0, 0) >= 0
	    ? 0 : -1;

    case T_QUOTED:
	return xo_eval_prog_emit(prog, xo_eval_quoted, id, 0, 0) >= 0
	    ? 0 : -1;

    case T_VAR:
	return xo_eval_prog_emit(prog, xo_eval_variable, id, 0, 0) >= 0
	    ? 0 : -1;

    case C_NOT:
	if (xo_eval_compile_node(prog, xdp, xnp->xn_contents, dp, ops) < 0)
	    return -1;
	/* xo_eval_not() already returns MISSING on its own if argv[0] is */
	return xo_eval_prog_emit(prog, xo_eval_not, id, 1, 0) >= 0
	    ? 0 : -1;

    case K_AND:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_and, TRUE, dp, ops);

    case K_OR:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_or, TRUE, dp, ops);

    case L_EQUALS:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_equals, FALSE, dp, ops);

    case L_NOTEQUALS:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_notequals, FALSE, dp, ops);

    case L_GRTR:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_gt, FALSE, dp, ops);

    case L_GRTREQ:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_ge, FALSE, dp, ops);

    case L_LESS:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_lt, FALSE, dp, ops);

    case L_LESSEQ:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_le, FALSE, dp, ops);

    case L_PLUS:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_plus, FALSE, dp, ops);

    case C_UNI_MINUS:
	/* Unary minus: "-4", not a binary "a - b" (that's L_MINUS) */
	if (xo_eval_compile_node(prog, xdp, xnp->xn_contents, dp, ops) < 0)
	    return -1;
	return xo_eval_prog_emit(prog, xo_eval_op_negate, id, 1,
				  XEIF_CHECK_MISSING) >= 0 ? 0 : -1;

    case L_MINUS:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_minus, FALSE, dp, ops);

    case L_STAR:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_mul, FALSE, dp, ops);

    case K_DIV:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_div, FALSE, dp, ops);

    case K_MOD:
	return xo_eval_compile_chain(prog, xdp, xnp->xn_contents,
				      xo_eval_op_mod, FALSE, dp, ops);

    case T_FUNCTION_NAME:
	return xo_eval_compile_function(prog, xdp, id, xnp, dp, ops);

    case C_EXPR:
	if (xnp->xn_contents)
	    return xo_eval_compile_node(prog, xdp, xnp->xn_contents, dp, ops);
	return -1;

    default:
	if (xnp->xn_contents)
	    return xo_eval_compile_node(prog, xdp, xnp->xn_contents, dp, ops);
	return -1;
    }
}

void
xo_eval_prog_destroy (xo_eval_prog_t *prog)
{
    if (prog == NULL)
	return;

    if (prog->xep_instr)
	xo_free(prog->xep_instr);

    xo_free(prog);
}

/*
 * Compile the XPath expression rooted at 'root' into a flat program
 * that can be replayed by the runner without re-walking the AST.
 * Returns NULL on any compile failure (unknown function, wrong arity,
 * unsupported node type); any failure specific enough to explain is
 * reported through (dp, ops) as it's found, same as xo_eval_fail()
 * does for evaluation-time errors.  Pass NULL for both if the caller
 * has no data context to report through (compile still fails the same
 * way; it's just silent).
 */
xo_eval_prog_t *
xo_eval_compile (xo_xparse_data_t *xdp, xo_xparse_node_id_t root,
		  xo_filter_data_t *dp, xo_filter_data_ops_t *ops)
{
    xo_eval_prog_t *prog = xo_realloc(NULL, sizeof(*prog));
    if (prog == NULL)
	return NULL;

    prog->xep_instr = NULL;
    prog->xep_count = 0;
    prog->xep_max = 0;

    if (xo_eval_compile_node(prog, xdp, root, dp, ops) < 0) {
	xo_eval_prog_destroy(prog);
	return NULL;
    }

    return prog;
}

/*
 * Replay a program produced by xo_eval_compile() without re-walking the
 * AST: a flat loop over an explicit value stack, sized once since each
 * instruction's net effect on depth is "push 1, pop xei_argc", so the
 * stack can never grow past the instruction count.
 *
 * Two things need to match xo_eval()'s recursive dispatch exactly, since
 * that's the behavior this replaces:
 *
 * - A combine instruction whose result comes back with XEVF_FINAL set
 *   (AND/OR short-circuiting) jumps to xei_jump, skipping the rest of
 *   the chain, instead of folding in the remaining operands.
 * - An instruction with XEIF_CHECK_MISSING set never runs xei_fn at all if
 *   any of its args are missing; it produces MISSING directly, the same
 *   way xo_eval()'s loop short-circuits op_fn for a missing operand
 *   before combining (xo_eval_compare() and the calc ops don't expect to
 *   be handed a missing value).  The chain still keeps running afterward,
 *   again matching xo_eval(): only XEVF_FINAL cuts a chain short.
 */
xo_eval_value_t
xo_eval_prog_run (xo_handle_t *xop, xo_filter_t *xfp, xo_tframe_t *framep,
		   xo_xparse_data_t *xdp, xo_eval_prog_t *prog, int indent)
{
    xo_eval_value_t *stack = alloca(prog->xep_count * sizeof(*stack));
    uint32_t sp = 0;
    uint32_t pc = 0;

    while (pc < prog->xep_count) {
	xo_eval_instr_t *xip = &prog->xep_instr[pc];
	int argc = xip->xei_argc;
	xo_eval_value_t *argv = &stack[sp - argc];
	xo_eval_value_t result;

	if (xip->xei_flags & XEIF_CHECK_MISSING) {
	    int missing = FALSE;

	    for (int i = 0; i < argc; i++) {
		if (argv[i].xev_flags & XEVF_MISSING) {
		    missing = TRUE;
		    break;
		}
	    }

	    if (missing) {
		for (int i = 0; i < argc; i++)
		    xo_eval_value_free(argv[i]);

		sp -= argc;
		stack[sp++] = xo_eval_value_missing();
		pc += 1;
		continue;
	    }
	}

	xo_xparse_node_t *xnp = xo_xparse_node(xdp, xip->xei_node);
	result = xip->xei_fn(xop, xfp, framep, xnp, indent, argc, argv);

	for (int i = 0; i < argc; i++)
	    xo_eval_value_free(argv[i]);

	sp -= argc;

	if (result.xev_flags & XEVF_FINAL) {
	    result.xev_flags &= ~XEVF_FINAL; /* Not final for our caller */
	    stack[sp++] = result;
	    pc = xip->xei_jump;
	    continue;
	}

	stack[sp++] = result;
	pc += 1;
    }

    return stack[0];
}

/*
 * Public wrapper around xo_eval_prog_run() for non-libxo backends (e.g.
 * libpin) that don't know xo_eval_value_t's layout: reduce the result to
 * plain C types instead.  See the declaration in xo_filter.h.
 */
char *
xo_eval_prog_run_simple (xo_handle_t *xop, xo_filter_t *xfp,
			  xo_tframe_t *framep, xo_xparse_data_t *xdp,
			  xo_eval_prog_t *prog,
			  int *is_booleanp, int *bool_valuep,
			  int *is_missingp)
{
    if (is_booleanp)
	*is_booleanp = 0;
    if (bool_valuep)
	*bool_valuep = 0;
    if (is_missingp)
	*is_missingp = 0;

    xo_eval_value_t value
	= xo_eval_prog_run(xop, xfp, framep, xdp, prog, XO_INDENT);

    if (value.xev_flags & (XEVF_MISSING | XEVF_INVALID | XEVF_UNSUPPORTED)) {
	xo_eval_value_free(value);
	if (is_missingp)
	    *is_missingp = 1;
	return NULL;
    }

    if (value.xev_type == C_BOOLEAN) {
	if (is_booleanp)
	    *is_booleanp = 1;
	if (bool_valuep)
	    *bool_valuep = xo_eval_cast_boolean(xop, value);
	xo_eval_value_free(value);
	return NULL;
    }

    char *str = xo_eval_cast_string(xop, value);
    xo_eval_value_free(value);
    return str;
}

/*
 * Find 'root' in the sorted xf_pred_progs[] cache.  Returns the index of
 * the matching entry if found, or -(insertion point) - 1 if not (so the
 * caller can recover the insertion point as -(result) - 1 without
 * confusing "not found at 0" with "found at 0").
 */
static int
xo_filter_pred_prog_find (xo_filter_t *xfp, xo_xparse_node_id_t root)
{
    int lo = 0, hi = (int) xfp->xf_pred_progs_count - 1;

    while (lo <= hi) {
	int mid = (lo + hi) / 2;
	xo_xparse_node_id_t mid_id = xfp->xf_pred_progs[mid].xpe_node_id;

	if (mid_id == root)
	    return mid;
	if (mid_id < root)
	    lo = mid + 1;
	else
	    hi = mid - 1;
    }

    return -(lo + 1);
}

/*
 * Return the compiled program for the expression rooted at 'root',
 * compiling and caching it on first use.  The cache lives on xfp as a
 * packed array of {node id, program} pairs kept sorted by node id, so
 * its size tracks the number of distinct predicates actually seen
 * (typically tiny) rather than the much larger, sparse AST node id
 * space that 'root' is drawn from; lookups and inserts use binary
 * search on that sorted order.  It survives xo_filter_walk_add
 * rebuilding xfp->xf_trie from scratch to add a new pattern: node ids
 * in xfp->xf_xd stay stable even though the trie itself does not.
 * Returns NULL if 'root' is 0 or the expression fails to compile.
 */
static xo_eval_prog_t *
xo_filter_pred_prog (xo_filter_t *xfp, xo_xparse_node_id_t root)
{
    if (root == 0)
	return NULL;

    int slot = xo_filter_pred_prog_find(xfp, root);
    if (slot >= 0)
	return xfp->xf_pred_progs[slot].xpe_prog;

    uint32_t insert_at = (uint32_t) (-(slot + 1));

    xo_eval_prog_t *prog = xo_eval_compile(&xfp->xf_xd, root,
					   xfp->xf_data, xfp->xf_ops);
    if (prog == NULL)
	return NULL;

    if (xfp->xf_pred_progs_count >= xfp->xf_pred_progs_max) {
	uint32_t new_cap = xfp->xf_pred_progs_max
	    ? xfp->xf_pred_progs_max * 2 : 4;
	xo_filter_pred_entry_t *newp = realloc(xfp->xf_pred_progs,
						new_cap * sizeof(*newp));
	if (newp == NULL) {
	    xo_eval_prog_destroy(prog);
	    return NULL;
	}
	xfp->xf_pred_progs = newp;
	xfp->xf_pred_progs_max = new_cap;
    }

    xo_filter_pred_entry_t *entries = xfp->xf_pred_progs;
    memmove(entries + insert_at + 1, entries + insert_at,
	    (xfp->xf_pred_progs_count - insert_at) * sizeof(*entries));
    entries[insert_at].xpe_node_id = root;
    entries[insert_at].xpe_prog = prog;
    xfp->xf_pred_progs_count += 1;

    return prog;
}

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

	xo_eval_prog_t *prog = xo_filter_pred_prog(xfp, xnp->xn_contents);
	xo_eval_value_t pv = prog
	    ? xo_eval_prog_run(xop, xfp, framep, &xfp->xf_xd, prog, XO_INDENT)
	    : xo_eval_value_invalid();
	xo_eval_dump_value(xop, xfp, pv, XO_INDENT,
			   "xo_filter_pred_eval: working");

	if (pv.xev_flags & XEVF_MISSING) {
	    /* Key not yet seen - can't resolve this predicate yet */
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
 * The caller records the match.
 */
static int
xo_tmatch_try_eager (xo_handle_t *xop, xo_filter_t *xfp,
		     xo_tframe_t *framep, xo_xparse_node_id_t pred)
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
    return live ? XTFS_LIVE : XTFS_DEAD;
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

	xo_eval_prog_t *prog = xo_filter_pred_prog(xfp, cid);
	value = prog
	    ? xo_eval_prog_run(xop, xfp, framep, &xfp->xf_xd, prog, XO_INDENT)
	    : xo_eval_value_invalid();
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
    xo_tslot_t *slotp = &framep->xtf_slots[slot];

    /* Already counted for this slot: return saved qualified position */
    if (slotp->xts_flags & XTFF_QUAL_COUNTED)
	return slotp->xts_qual_position;

    /* No trailing C_INDEX: use normal open-time position */
    if (!xo_pred_has_trailing_cindex(xfp, pred_id))
	return slotp->xts_position;

    /* Evaluate leading predicates to see if they currently pass */
    xo_eval_value_t lv;

    lv = xo_filter_leading_preds_eval(xop, xfp, framep, pred_id);
    if (lv.xev_flags & XEVF_MISSING) {
	xo_eval_value_free(lv);
	return slotp->xts_position; /* not yet decidable */
    }
    int passes = xo_eval_cast_boolean(xop, lv);
    xo_eval_value_free(lv);

    if (!passes)
	return slotp->xts_position; /* C_INDEX outcome won't matter */

    /*
     * Leading predicates passed: advance the qualified counter in
     * parent frame
     */
    xo_tframe_t *parentp = xtmp->xtm_depth > 0
	? &xtmp->xtm_stack[xtmp->xtm_depth - 1] : NULL;
    if (parentp == NULL)
	return slotp->xts_position;

    uint32_t qpos;

    qpos = xo_tframe_child_qualified_position(parentp, slotp->xts_node);
    slotp->xts_qual_position = qpos;
    slotp->xts_flags |= XTFF_QUAL_COUNTED;
    return qpos;
}

/*
 * Scan ancestor frames for PRED-state slots that may be waiting for a
 * compound-path key that just arrived at a deeper level.  For example,
 * if an ancestor frame has a predicate author[life-span/born] and we
 * just received key("born", value) inside a "life-span" element, we
 * reconstruct the compound path "life-span/born" and buffer it in the
 * ancestor frame so xo_eval_path can find it.
 *
 * Maximum ancestor scan depth is bounded by XO_ANCESTOR_SCAN_MAX to
 * avoid O(n) overhead on deep documents with no multi-step predicates.
 */
#define XO_ANCESTOR_SCAN_MAX 8

static void
xo_tmatch_key_ancestors (xo_handle_t *xop, xo_filter_t *xfp,
			  xo_tmatch_t *xtmp,
			  const char *tag, xo_ssize_t tlen,
			  const char *value, xo_ssize_t vlen)
{
    xo_trie_t *xtp = xtmp->xtm_trie;
    uint32_t cur = xtmp->xtm_depth;

    if (cur < 2)
	return;

    uint32_t scan_limit = cur - 1;
    if (scan_limit > XO_ANCESTOR_SCAN_MAX)
	scan_limit = XO_ANCESTOR_SCAN_MAX;

    for (uint32_t gap = 1; gap <= scan_limit; gap++) {
	uint32_t anc = cur - gap;
	xo_tframe_t *ancp = &xtmp->xtm_stack[anc];

	/* Quick check: any PRED slots in this ancestor frame? */
	int has_pred = FALSE;
	for (uint32_t s = 0; s < ancp->xtf_slots_count; s++) {
	    if (xo_tslot_is_pending(&ancp->xtf_slots[s])) {
		has_pred = TRUE;
		break;
	    }
	}
	if (!has_pred)
	    continue;

	/* Build the compound path: stack[anc+1].tag/.../stack[cur].tag/tag */
	char path_buf[256];
	ssize_t path_len = 0;
	int overflow = FALSE;

	for (uint32_t d = anc + 1; d <= cur; d++) {
	    xo_tframe_t *fp = &xtmp->xtm_stack[d];
	    if (fp->xtf_tag == NULL)
		continue;
	    ssize_t slen = fp->xtf_tag_len;
	    if (path_len + slen + 1 >= (ssize_t) sizeof(path_buf) - 1) {
		overflow = TRUE;
		break;
	    }
	    if (path_len > 0)
		path_buf[path_len++] = '/';
	    memcpy(path_buf + path_len, fp->xtf_tag, slen);
	    path_len += slen;
	    path_buf[path_len] = '\0';
	}
	if (overflow)
	    continue;

	if (path_len + tlen + 1 >= (ssize_t) sizeof(path_buf) - 1)
	    continue;
	if (path_len > 0)
	    path_buf[path_len++] = '/';
	memcpy(path_buf + path_len, tag, tlen);
	path_len += tlen;
	path_buf[path_len] = '\0';

	/* For each PRED slot in ancestor, check if predicate needs the leaf */
	for (uint32_t i = 0; i < ancp->xtf_slots_count; i++) {
	    if (!xo_tslot_is_pending(&ancp->xtf_slots[i]))
		continue;

	    xo_tnode_t *tn = &xtp->xt_nodes[ancp->xtf_slots[i].xts_node];

	    /* Use the single-step check on the leaf tag as a gate */
	    if (!xo_filter_pred_needs(&xfp->xf_xd, xfp, tn->xtn_pred,
				      tag, tlen, FALSE))
		continue;

	    /* Buffer the compound key in the ancestor frame */
	    xo_tframe_key_add(ancp, path_buf, path_len, value, vlen);

	    ancp->xtf_position_cur = xo_tmatch_slot_position(xop, xfp, xtmp,
							      ancp, i,
							      tn->xtn_pred);
	    xo_eval_value_t result =
		xo_filter_pred_eval(xop, xfp, ancp, tn->xtn_pred);

	    if (result.xev_flags & XEVF_MISSING)
		continue;

	    int live = xo_eval_cast_boolean(xop, result);
	    xo_eval_value_free(result);
	    xo_tmatch_resolve(xfp, xtmp, ancp, &ancp->xtf_slots[i], tn, live);
	}
    }
}

static xo_filter_status_t
xo_tmatch_key (xo_handle_t *xop, xo_filter_t *xfp, xo_tmatch_t *xtmp,
	       const char *tag, xo_ssize_t tlen,
	       const char *value, xo_ssize_t vlen)
{
    if (!xo_tmatch_has_frame(xtmp))
	return xfp->xf_status;

    xo_trie_t *xtp = xtmp->xtm_trie;
    xo_tframe_t *framep = &xtmp->xtm_stack[xtmp->xtm_depth];

    xtmp->xtm_event += 1;

    for (uint32_t i = 0; i < framep->xtf_slots_count; i++) {
	if (!xo_tslot_is_pending(&framep->xtf_slots[i]))
	    continue;

	xo_tnode_t *tn = &xtp->xt_nodes[framep->xtf_slots[i].xts_node];

	if (!xo_filter_pred_needs(&xfp->xf_xd, xfp, tn->xtn_pred,
				  tag, tlen, FALSE))
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
	xo_tmatch_resolve(xfp, xtmp, framep, &framep->xtf_slots[i], tn, live);
    }

    /* Check ancestor frames for compound-path predicates */
    xo_tmatch_key_ancestors(xop, xfp, xtmp, tag, tlen, value, vlen);

    return xfp->xf_status;
}

/*
 * If resolving a key or field just passed a predicate on a path step
 * that isn't the last (so nothing became FULL), report XO_STATUS_LIVE
 * instead of 'rc', telling libxo that content it kept conditionally
 * beneath this instance is wanted.  LIVE is only a return value; it's
 * never recorded as xf_status.
 */
static xo_filter_status_t
xo_filter_promoted (xo_filter_t *xfp, xo_filter_status_t rc)
{
    int promoted = (xfp->xf_flags & XFSF_PROMOTED) ? TRUE : FALSE;

    xfp->xf_flags &= ~XFSF_PROMOTED;
    if (promoted && xfp->xf_status != XO_STATUS_FULL
	    && xfp->xf_status != XO_STATUS_COND)
	return XO_STATUS_LIVE;

    return rc;
}

static int
xo_filter_op_key (XO_FILTER_KEY_SIGNATURE)
{
    if (xfp == NULL || xfp->xf_trie == NULL)
	return 0;

    XO_DBG(xop, "xo_filter_key: '%.*s' = '%.*s'",
	   (int) tlen, tag, (int) vlen, value);

    xfp->xf_flags &= ~XFSF_PROMOTED;
    xo_tmatch_key(xop, xfp, &xfp->xf_tmatch, tag, tlen, value, vlen);

    xo_filter_change_status(xop, xfp, "key", tag, tlen);

    /*
     * Return the status as it stands after this key, so libxo sees
     * a predicate that just failed (or passed; see above).
     */
    xo_filter_status_t rc = xo_filter_promoted(xfp, xfp->xf_status);

    XO_DBG(xop, "xo_filter_key: '%.*s' = '%.*s' --> status %s",
	   (int) tlen, tag, (int) vlen, value,
	   xo_filt_status_name(xfp->xf_status));

    return rc;
}

static xo_filter_status_t
xo_tmatch_attr (xo_handle_t *xop, xo_filter_t *xfp, xo_tmatch_t *xtmp,
		const char *tag, xo_ssize_t tlen,
		const char *value, xo_ssize_t vlen)
{
    if (!xo_tmatch_has_frame(xtmp))
	return xfp->xf_status;

    xo_trie_t *xtp = xtmp->xtm_trie;
    xo_tframe_t *framep = &xtmp->xtm_stack[xtmp->xtm_depth];

    xtmp->xtm_event += 1;

    for (uint32_t i = 0; i < framep->xtf_slots_count; i++) {
	if (!xo_tslot_is_pending(&framep->xtf_slots[i]))
	    continue;

	xo_tnode_t *tn = &xtp->xt_nodes[framep->xtf_slots[i].xts_node];

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
	xo_tmatch_resolve(xfp, xtmp, framep, &framep->xtf_slots[i], tn, live);
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

    XO_DBG(xop, "xo_filter_attribute: '@%.*s' = '%.*s'",
	   (int) tlen, tag, (int) vlen, value);

    xo_filter_status_t rc = xo_tmatch_attr(xop, xfp, &xfp->xf_tmatch,
					    tag, tlen, value, vlen);

    xo_filter_change_status(xop, xfp, "attr", tag, tlen);

    XO_DBG(xop, "xo_filter_attribute: '@%.*s' = '%.*s' --> status %s",
	   (int) tlen, tag, (int) vlen, value,
	   xo_filt_status_name(xfp->xf_status));

    return rc;
}

int
xo_filter_walk_attr (xo_handle_t *xop, xo_filter_t *xfp,
		     const char *tag, xo_ssize_t tlen,
		     const char *value, xo_ssize_t vlen)
{
    /* Like walk_open/close, a negative length means "use strlen" */
    if (tlen < 0)
	tlen = (xo_ssize_t) strlen(tag);
    if (value && vlen < 0)
	vlen = (xo_ssize_t) strlen(value);
    return xo_filter_op_attribute(xop, xfp, tag, tlen, value, vlen);
}

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
    if (!xo_tmatch_has_frame(xtmp))
	return FALSE;

    xo_trie_t *xtp = xtmp->xtm_trie;
    xo_tframe_t *framep = &xtmp->xtm_stack[xtmp->xtm_depth];

    for (uint32_t i = 0; i < framep->xtf_slots_count; i++) {
	if (!xo_tslot_is_pending(&framep->xtf_slots[i]))
	    continue;
	xo_tnode_t *tn = &xtp->xt_nodes[framep->xtf_slots[i].xts_node];
	if (xo_filter_pred_needs(&xfp->xf_xd, xfp, tn->xtn_pred,
				 tag, tlen, FALSE))
	    return TRUE;
    }

    return FALSE;
}

/*
 * Force-evaluate all XTFS_PRED slots in the current trie frame, treating
 * any missing (never-seen) field as an empty node-set, as XPath does.
 * Called from xo_filter_close_pending when the predicate field was
 * never emitted inside this instance or container.
 */
static void
xo_filter_force_resolve_pred (xo_handle_t *xop, xo_filter_t *xfp,
			      const char *tag, ssize_t tlen)
{
    xo_tmatch_t *xtmp = &xfp->xf_tmatch;
    if (!xo_tmatch_has_frame(xtmp))
	return;

    xo_tframe_t *framep = &xtmp->xtm_stack[xtmp->xtm_depth];

    /*
     * PRED status can also come from a COND slot that isn't terminal
     * (see xo_filter_change_status); there's nothing to resolve then
     */
    if (!xo_tframe_has_pred(framep))
	return;

    xfp->xf_flags |= XFSF_FORCE_RESOLVE;
    xtmp->xtm_event += 1;

    for (uint32_t i = 0; i < framep->xtf_slots_count; i++) {
	xo_tslot_t *slotp = &framep->xtf_slots[i];
	if (!xo_tslot_is_pending(slotp))
	    continue;
	xo_tnode_t *tn = &xtmp->xtm_trie->xt_nodes[slotp->xts_node];
	if (tn->xtn_pred == 0) {
	    xo_tmatch_resolve(xfp, xtmp, framep, slotp, tn, TRUE);
	    continue;
	}

	/*
	 * For "foo[A][N]", N counts only siblings where A passed.  A
	 * backend that never calls walk_key (e.g. one that answers
	 * value_of from a buffered tree) reaches here without that
	 * count having been taken, so take it now.  XFSF_FORCE_RESOLVE
	 * is already set, so A can't come back MISSING.
	 */
	framep->xtf_position_cur = xo_tmatch_slot_position(xop, xfp, xtmp,
							    framep, i,
							    tn->xtn_pred);
	xo_eval_value_t result =
	    xo_filter_pred_eval(xop, xfp, framep, tn->xtn_pred);
	if (!(result.xev_flags & XEVF_MISSING)) {
	    int live = xo_eval_cast_boolean(xop, result);
	    xo_eval_value_free(result);
	    xo_tmatch_resolve(xfp, xtmp, framep, slotp, tn, live);
	}
    }

    xfp->xf_flags &= ~XFSF_FORCE_RESOLVE;
    xo_filter_change_status(xop, xfp, "force-resolve-pred", tag, tlen);
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

    xfp->xf_flags &= ~XFSF_PROMOTED;
    xo_tmatch_key(xop, xfp, &xfp->xf_tmatch, tag, tlen, value, vlen);

    xo_filter_change_status(xop, xfp, "pred-field", tag, tlen);

    return xo_filter_promoted(xfp, xfp->xf_status);
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

/*
 * Public direct-call ops for non-libxo callers (e.g. libpin).
 *
 * All xo_filter_op_* counterparts above are static; these wrappers
 * provide external linkage while remaining in the same translation
 * unit so they can call the static helpers.
 *
 * xop may be NULL; it is only used for debug logging.
 */
int
xo_filter_walk_open (xo_handle_t *xop, xo_filter_t *xfp,
		     const char *tag, ssize_t tlen)
{
    if (tlen < 0)
	tlen = (ssize_t) strlen(tag);
    return xo_filter_open(xop, xfp, tag, tlen, "container", NULL, 0);
}

int
xo_filter_walk_close (xo_handle_t *xop, xo_filter_t *xfp,
		      const char *tag, ssize_t tlen)
{
    if (tlen < 0)
	tlen = (ssize_t) strlen(tag);

    /*
     * Callers of the walk_* API (e.g. libpin) drive this FSM one XML
     * element at a time, and each such element is a potential match
     * instance (the same "instance" semantics xo_filter_op_close_instance
     * uses for repeated list items).  If the instance is still in PRED
     * state at close time, the predicate field was never seen while
     * walking the subtree; force-resolve it now, treating any absent
     * field as empty string (XPath: absent node -> empty nodeset ->
     * string("")), so xo_filter_walk_status() reflects the real outcome.
     */
    if (xfp != NULL && xfp->xf_trie != NULL && xfp->xf_status == XO_STATUS_PRED)
	xo_filter_force_resolve_pred(xop, xfp, tag, tlen);

    xo_filter_status_t pre_close = xfp ? xfp->xf_status : XO_STATUS_ZERO;
    xo_filter_close(xop, xfp, tag, tlen, "container");

    return (pre_close == XO_STATUS_FULL) ? XO_STATUS_FULL
	: (xfp ? xfp->xf_status : XO_STATUS_ZERO);
}

int
xo_filter_walk_key (xo_handle_t *xop, xo_filter_t *xfp,
		    const char *tag, ssize_t tlen,
		    const char *value, ssize_t vlen)
{
    /* Like walk_open/close, a negative length means "use strlen" */
    if (tlen < 0)
	tlen = (ssize_t) strlen(tag);
    if (value && vlen < 0)
	vlen = (ssize_t) strlen(value);
    return xo_filter_op_key(xop, xfp, tag, tlen, value, vlen);
}

xo_filter_status_t
xo_filter_walk_status (xo_handle_t *xop UNUSED, xo_filter_t *xfp)
{
    return xfp ? xfp->xf_status : XO_STATUS_ZERO;
}

int
xo_filter_walk_add (xo_handle_t *xop, xo_filter_t *xfp, const char *xpath)
{
    if (xfp == NULL)
	return -1;
    return xo_filter_compile_xpath(xop, xfp, xpath);
}

int
xo_filter_walk_add_with_action (xo_handle_t *xop, xo_filter_t *xfp,
				 const char *xpath, uint32_t action)
{
    if (xfp == NULL)
	return -1;
    return xo_filter_compile_xpath_action(xop, xfp, xpath, action);
}

int
xo_filter_walk_add_with_action_priority (xo_handle_t *xop, xo_filter_t *xfp,
					 const char *xpath, uint32_t action,
					 double priority, int16_t import_prec)
{
    if (xfp == NULL)
	return -1;
    return xo_filter_compile_xpath_action_priority(xop, xfp, xpath, action,
						    priority, import_prec);
}

uint32_t
xo_filter_walk_get_action (xo_handle_t *xop UNUSED, xo_filter_t *xfp)
{
    return xfp ? xfp->xf_cur_action : 0;
}

xo_filter_data_t *
xo_filter_get_data_ptr (xo_filter_t *xfp)
{
    return xfp ? xfp->xf_data : NULL;
}

xo_filter_data_ops_t *
xo_filter_get_data_ops (xo_filter_t *xfp)
{
    return xfp ? xfp->xf_ops : NULL;
}
