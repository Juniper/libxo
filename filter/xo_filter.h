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

#define XO_FILTER_OPS_VERSION 2	/* Current API version number */

#define XO_FILTER_MISS	1	/* Missing information, might work later */
#define XO_FILTER_FAIL	2	/* Test failed; will never succeed */

struct xo_xparse_data_s;

/*
 * We treat xo_filter_t structure as opaque in the core of libxo, so we
 * just need the opaque definition here.
 */
struct xo_filter_s;
typedef struct xo_filter_s xo_filter_t;

/*
 * Opaque data context: only the implementation outside xo_filter.so knows
 * the real layout.  libxo's own implementation (using heap + xparse string
 * table) defines struct xo_filter_data_s inside filter/xo_filter.c.
 * Alternative backends (e.g. libpin, using parrotdb/paistr atoms) define
 * their own struct xo_filter_data_s in their own translation unit.
 */
struct xo_filter_data_s;
typedef struct xo_filter_data_s xo_filter_data_t;

/*
 * Strongly-typed interned name id stored inside trie nodes.  The core
 * never interprets ni_id; only the data implementation does.  For the
 * default backend ni_id is an xo_xparse_str_id_t (string-table offset).
 * For a libpin backend ni_id would be a paistr atom.
 */
typedef struct { uint32_t ni_id; } xo_name_id_t;

/*
 * Filter status type and values.  Also defined in libxo/xo_private.h for
 * internal use; duplicated here so external consumers (e.g. libpin) can
 * compare status values without pulling in the private header.
 */
#ifndef XO_STATUS_ZERO
typedef uint32_t xo_filter_status_t;
#define XO_STATUS_ZERO  0   /* not enabled / no filter loaded */
#define XO_STATUS_FULL  1   /* fully matched: let output flow */
#define XO_STATUS_TRACK 2   /* tracking path, no data output yet */
#define XO_STATUS_PRED  3   /* matched path; waiting for predicate data */
#define XO_STATUS_DEAD  4   /* no match possible under this subtree */
#endif /* XO_STATUS_ZERO */

/*
 * Data operations vtable — the "inner" ops (filter -> data representation),
 * complementing the existing "outer" ops (caller -> filter: open/close/status).
 *
 * xfdo_realloc / xfdo_free: replace xo_realloc/xo_free for trie storage.
 *     Routing trie allocation through the data context lets a pin backend
 *     place the compiled trie in an mmap region for persistence.
 *
 * xfdo_name_intern (optional, may be NULL):
 *     Called once per path step at trie-compile time to convert a name
 *     string into an xo_name_id_t.  If NULL, the core wraps the raw
 *     xparse string-table offset directly (correct for the default backend).
 *
 * xfdo_name_eq (required):
 *     Called in the hot path of xo_tmatch_open to compare a stored
 *     xo_name_id_t against an incoming tag string.  For the default backend
 *     this resolves the string-table offset and calls xo_streqn; for a
 *     pin backend it interns the tag and compares atoms.
 */
#define XO_FILTER_DATA_OPS_VERSION 1

typedef struct xo_filter_data_ops_s {
    uint32_t       xfdo_version;
    void        *(*xfdo_realloc)(xo_filter_data_t *, void *, size_t);
    void         (*xfdo_free)(xo_filter_data_t *, void *);
    xo_name_id_t (*xfdo_name_intern)(xo_filter_data_t *,
				     const char *, ssize_t);	/* optional */
    int          (*xfdo_name_eq)(xo_filter_data_t *, xo_name_id_t,
				 const char *, ssize_t);
    /*
     * xfdo_value_of (optional, may be NULL):
     *     Called by xo_eval_attribute / xo_eval_path to look up the value
     *     of a named field in the current element context.  'name' is the
     *     XPath attribute or element name being tested; returns a
     *     NUL-terminated string valid until the next xfdo_value_of call,
     *     or NULL if the field is not present.  When NULL, the core falls
     *     back to xo_filter_attr_find / xo_filter_key_find (xtf_keys buffer).
     */
    const char *(*xfdo_value_of)(xo_filter_data_t *, const char *, ssize_t);
} xo_filter_data_ops_t;

/* Default data ops: heap allocation + xparse string-table name resolution */
extern xo_filter_data_ops_t xo_filter_data_ops_default;

#define XO_FILTER_DEFAULT_ARGS xop, xfp
#define XO_FILTER_DEFAULT_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED

#define XO_FILTER_DEFAULT_TAG_ARGS xop, xfp, tag
#define XO_FILTER_DEFAULT_TAG_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	const char *tag UNUSED

/*
 * The outer ops (caller -> filter: open_container/close/key/get_status/…)
 * reference internal libxo types (xo_filter_status_t, XO_ENCODER_HANDLER_ARGS)
 * that are not available to external consumers.  Guard the include so that
 * code outside xo_filter.so can include this header for the data-ops types
 * without pulling in the internal ops declarations.
 */
#ifdef LIBXO_NEED_FILTERS
#include "xo_filter_ops.h"
#endif /* LIBXO_NEED_FILTERS */

void
xo_set_filter_data (xo_handle_t *xop UNUSED, xo_filter_t *);

struct xo_filter_s *
xo_get_filter_data (xo_handle_t *xop, int create);

xo_filter_t *
xo_filter_create (xo_handle_t *xop);

/*
 * Create a filter with a caller-supplied data context and ops vtable.
 * Used by non-libxo consumers (e.g. libpin) that want to plug in their
 * own name interning and trie storage.  The caller owns the data struct
 * and ops; neither is freed when the filter is destroyed.
 */
xo_filter_t *
xo_filter_create_with_data (xo_handle_t *xop, xo_filter_data_t *dp,
			     xo_filter_data_ops_t *ops);

struct xo_xparse_data_s *
xo_filter_xparse_data (xo_handle_t *xop, xo_filter_t *xfp);

/* Return the opaque data pointer set by xo_filter_create_with_data. */
xo_filter_data_t *
xo_filter_get_data_ptr (xo_filter_t *xfp);

#ifdef LIBXO_NEED_FILTERS
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
#endif /* LIBXO_NEED_FILTERS */

/*
 * Direct-call filter ops for non-libxo callers (e.g. libpin) that hold an
 * xo_filter_t * from xo_filter_create_with_data() and drive the FSM
 * themselves rather than going through the dlopen vtable.
 *
 * xop may be NULL; it is only used for debug logging inside the filter.
 *
 * xo_filter_walk_open / xo_filter_walk_close:
 *   Must be called in exact sync with the element stream (every open needs
 *   a matching close) to keep the filter's frame-stack depth correct.
 *
 * xo_filter_walk_key:
 *   Feed a key/attribute name+value so predicates can be evaluated when
 *   xfdo_value_of is NULL.  When xfdo_value_of is set, the backend handles
 *   value lookup directly and xo_filter_walk_key need not be called.
 *
 * xo_filter_walk_status:
 *   Query the current match status after xo_filter_walk_open.
 *   XO_STATUS_FULL  → subtree matched; keep it.
 *   XO_STATUS_TRACK → on a partial path; continue tracking.
 *   XO_STATUS_PRED  → path matched; waiting for predicate data.
 *   XO_STATUS_DEAD  → no match possible; discard the subtree.
 *
 * xo_filter_walk_add:
 *   Parse and compile an XPath match expression into the filter.
 *   May be called multiple times; each call accumulates another pattern.
 */
int xo_filter_walk_open (xo_handle_t *xop, xo_filter_t *xfp,
			  const char *tag, ssize_t tlen);
int xo_filter_walk_close (xo_handle_t *xop, xo_filter_t *xfp,
			   const char *tag, ssize_t tlen);
int xo_filter_walk_key (xo_handle_t *xop, xo_filter_t *xfp,
			 const char *tag, ssize_t tlen,
			 const char *value, ssize_t vlen);
xo_filter_status_t xo_filter_walk_status (xo_handle_t *xop, xo_filter_t *xfp);
int xo_filter_walk_add (xo_handle_t *xop, xo_filter_t *xfp, const char *xpath);

/*
 * Like xo_filter_walk_add but associates a backend-opaque action id with
 * the pattern's terminal trie node.  When the filter reports XO_STATUS_FULL,
 * xo_filter_walk_get_action() returns that id so the caller can dispatch
 * directly without consulting a secondary rulebook.  action==0 means "no
 * action attached" (same as xo_filter_walk_add).
 */
int xo_filter_walk_add_with_action (xo_handle_t *xop, xo_filter_t *xfp,
				     const char *xpath, uint32_t action);

/*
 * Return the action id recorded when the last XO_STATUS_FULL status was
 * computed.  Valid to call immediately after xo_filter_walk_status returns
 * XO_STATUS_FULL.  Returns 0 if no action was attached to the matched node.
 */
uint32_t xo_filter_walk_get_action (xo_handle_t *xop, xo_filter_t *xfp);

#endif /* XO_FILTER_H */
