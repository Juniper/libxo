/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * xpath_02: drive the xpath_01 input files through the filter "walk"
 * API (xo_filter_create_with_data / xo_filter_walk_*), the API libpin
 * uses, instead of through libxo's own emit path.  Output mimics the
 * non-pretty "fullpath" encoder, so it can be diffed against the
 * xpath_01 fullpath output.
 *
 * Each case (text between 'r' lines) is parsed into a tree, then the
 * tree is replayed through the walk API with one of two backends:
 *
 *   tree:   xfdo_value_of answers from the whole buffered node (as
 *           libpin does); xo_filter_walk_key is never called.
 *   stream: no xfdo_value_of; keys and (under TRACK/PRED) fields are
 *           fed through xo_filter_walk_key, as libxo itself does.
 *
 * Fields are walked as leaf frames (walk_open/walk_close).  A field's
 * line is kept if its frame is FULL, or if any frame that was PRED
 * when the line was made later resolves to FULL.  A frame whose
 * predicate passes on a path that continues below it (walk_key or
 * walk_close returns LIVE, as for "item[size = 10]/sub") keeps only
 * the lines that were COND, i.e. that matched the rest of the path.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/param.h>

#include "xo_config.h"

#include <libxo/xo.h>
#include <libxo/xo_encoder.h>
#include <libxo/xo_buf.h>

#define LIBXO_NEED_FILTER

#include "xo_xpath.tab.h"
#include "xo_xparse.h"
#include "xo_filter.h"

static int opt_debug;
static int opt_case;
static int opt_quiet;
static int opt_tree = 1;
static const char *opt_filter;
static int this_case = 1;

/*
 * Parsed input: a node holds an ordered list of items.  KEY and ATTR
 * items carry name/value; FIELD and CHILD items carry a child node
 * (a FIELD's child is a leaf node with a value).
 */
#define IT_KEY		1
#define IT_ATTR		2
#define IT_FIELD	3
#define IT_CHILD	4
#define IT_FILTER	5	/* it_name is the xpath; added when reached */

typedef struct node_s node_t;

typedef struct item_s {
    int it_kind;
    char *it_name;
    char *it_value;
    node_t *it_child;
} item_t;

struct node_s {
    char *n_tag;
    char *n_value;		/* Leaf value (FIELD nodes only) */
    node_t *n_parent;
    item_t *n_items;
    int n_items_count;
    int n_items_max;
};

/* Our backend's data context (opaque to the filter) */
struct xo_filter_data_s {
    char **xfd_names;		/* Interned names; id is index + 1 */
    int xfd_names_count;
    int xfd_names_max;
    node_t *xfd_next;		/* Node the next event_open will push */
    node_t **xfd_stack;		/* Node stack, kept by event_open/close */
    int xfd_stack_count;
    int xfd_stack_max;
    xo_buffer_t xfd_value;	/* Scratch for value_of results */
};

/* A frame in the walker's own stack */
typedef struct frame_s {
    int f_id;
    xo_filter_status_t f_status;
    xo_off_t f_leader;		/* Leader length before this frame */
} frame_t;

/* An output line, decided at flush time */
typedef struct line_s {
    char *l_text;
    xo_filter_status_t l_status;
    int *l_pend;		/* Frame ids that were PRED at emit time */
    int l_pend_count;
} line_t;

typedef struct walker_s {
    xo_filter_t *w_xfp;
    xo_filter_data_t *w_dp;
    xo_buffer_t w_leader;
    frame_t *w_frames;
    int w_frames_count;
    int w_frames_max;
    int w_next_id;
    char *w_resolved;		/* Indexed by frame id */
    int w_resolved_max;
    line_t *w_lines;
    int w_lines_count;
    int w_lines_max;
} walker_t;

/* Per-case parse state */
typedef struct segment_s {
    char **s_filters;		/* Command-line filter (lasts all run) */
    int s_filters_count;
    int s_filters_max;
    node_t *s_root;
    node_t *s_cur;
} segment_t;

static void *
grow (void *ptr, int *maxp, int need, size_t size)
{
    if (need < *maxp)
	return ptr;

    int new_max = *maxp ? *maxp * 2 : 8;
    while (new_max <= need)
	new_max *= 2;

    ptr = realloc(ptr, new_max * size);
    if (ptr == NULL)
	xo_errx(1, "out of memory");

    bzero((char *) ptr + *maxp * size, (new_max - *maxp) * size);
    *maxp = new_max;
    return ptr;
}

static const char *
status_name (xo_filter_status_t st)
{
    return xo_filt_status_name(st);
}

/* ---------------------------------------------------------------- */
/* Input parsing (same line format as xpath_01) */

static char *
trim (char *cp)
{
    while (isspace((int) *cp))
	cp += 1;

    char *ep = cp + strlen(cp);

    for (ep--; ep > cp && isspace((int) *ep); ep--)
	continue;
    if (++ep >= cp && (*ep == '\n' || *ep == '\r'))
	*ep = '\0';

    return cp;
}

/*
 * A line with no value is skipped, so an empty value is written as
 * "", as in '$ name ""', to make a field that exists but has no
 * content.  (As in xpath_01, this is for fields and keys only.)
 */
static char *
empty_token (char *cp)
{
    return (cp[0] == '"' && cp[1] == '"' && cp[2] == '\0') ? cp + 2 : cp;
}

static char *
clean_token (char *cp)
{
    while (*cp && !isspace((int) *cp))
	cp += 1;

    if (*cp == '\0')
	return cp;

    *cp++ = '\0';

    while (isspace((int) *cp))
	cp += 1;

    char *ep = cp + strlen(cp);

    for (ep--; ep > cp && isspace((int) *ep); ep--)
	continue;
    if (++ep >= cp && (*ep == '\n' || *ep == '\r'))
	*ep = '\0';

    return cp;
}

static node_t *
node_new (const char *tag, const char *value, node_t *parent)
{
    node_t *np = calloc(1, sizeof(*np));
    if (np == NULL)
	xo_errx(1, "out of memory");

    np->n_tag = strdup(tag);
    np->n_value = value ? strdup(value) : NULL;
    np->n_parent = parent;
    return np;
}

static void
node_free (node_t *np)
{
    if (np == NULL)
	return;

    for (int i = 0; i < np->n_items_count; i++) {
	item_t *ip = &np->n_items[i];
	free(ip->it_name);
	free(ip->it_value);
	node_free(ip->it_child);
    }

    free(np->n_items);
    free(np->n_tag);
    free(np->n_value);
    free(np);
}

static item_t *
node_add_item (node_t *np, int kind, const char *name, const char *value)
{
    np->n_items = grow(np->n_items, &np->n_items_max,
		       np->n_items_count, sizeof(*np->n_items));

    item_t *ip = &np->n_items[np->n_items_count++];
    ip->it_kind = kind;

    if (kind == IT_FILTER) {
	ip->it_name = strdup(name);
    } else if (kind == IT_KEY || kind == IT_ATTR) {
	ip->it_name = strdup(name);
	ip->it_value = strdup(value);
    } else {
	ip->it_child = node_new(name, value, np);
    }

    return ip;
}

static void
segment_init (segment_t *sp)
{
    bzero(sp, sizeof(*sp));
    sp->s_root = sp->s_cur = node_new("", NULL, NULL);

    if (opt_filter) {		/* A command-line filter lasts all run */
	sp->s_filters = grow(NULL, &sp->s_filters_max, 0,
			     sizeof(*sp->s_filters));
	sp->s_filters[sp->s_filters_count++] = strdup(opt_filter);
    }
}

static void
segment_clean (segment_t *sp)
{
    for (int i = 0; i < sp->s_filters_count; i++)
	free(sp->s_filters[i]);
    free(sp->s_filters);
    node_free(sp->s_root);
    bzero(sp, sizeof(*sp));
}

/*
 * Copy the chain of open nodes from the root down to `np` (tags plus
 * keys and attributes) under `parent`, returning the copy of `np`.
 */
static node_t *
segment_copy_chain (node_t *np, node_t *parent)
{
    if (np->n_parent == NULL)	/* The root itself */
	return parent;

    parent = segment_copy_chain(np->n_parent, parent);

    node_t *copy = node_add_item(parent, IT_CHILD, np->n_tag,
				 NULL)->it_child;

    for (int i = 0; i < np->n_items_count; i++) {
	item_t *ip = &np->n_items[i];
	if (ip->it_kind == IT_KEY || ip->it_kind == IT_ATTR)
	    node_add_item(copy, ip->it_kind, ip->it_name, ip->it_value);
    }

    return copy;
}

/*
 * Start the next case.  An 'r' can come while containers are still
 * open (e.g. "+one ... r ... -one"); libxo keeps them open across the
 * reset, so the next case starts inside a copy of them.
 */
static void
segment_reset (segment_t *sp)
{
    segment_t old = *sp;

    segment_init(sp);
    sp->s_cur = segment_copy_chain(old.s_cur, sp->s_root);

    segment_clean(&old);
}

/* ---------------------------------------------------------------- */
/* Data ops */

static void *
data_realloc (xo_filter_data_t *dp UNUSED, void *ptr, size_t sz)
{
    return realloc(ptr, sz);
}

static void
data_free (xo_filter_data_t *dp UNUSED, void *ptr)
{
    free(ptr);
}

static xo_name_id_t
data_name_intern (xo_filter_data_t *dp, const char *name, ssize_t len)
{
    xo_name_id_t nid = { .ni_id = 0 };

    if (name == NULL)
	return nid;
    if (len < 0)
	len = strlen(name);

    for (int i = 0; i < dp->xfd_names_count; i++) {
	if (strncmp(dp->xfd_names[i], name, len) == 0
		&& dp->xfd_names[i][len] == '\0') {
	    nid.ni_id = i + 1;
	    return nid;
	}
    }

    dp->xfd_names = grow(dp->xfd_names, &dp->xfd_names_max,
			 dp->xfd_names_count, sizeof(*dp->xfd_names));
    dp->xfd_names[dp->xfd_names_count++] = strndup(name, len);
    nid.ni_id = dp->xfd_names_count;
    return nid;
}

static int
data_name_eq (xo_filter_data_t *dp, xo_name_id_t id,
	      const char *tag, ssize_t tlen)
{
    if (tag == NULL || id.ni_id == 0
	    || id.ni_id > (uint32_t) dp->xfd_names_count)
	return 0;
    if (tlen < 0)
	tlen = strlen(tag);

    const char *nm = dp->xfd_names[id.ni_id - 1];
    return (strncmp(nm, tag, tlen) == 0 && nm[tlen] == '\0');
}

static void
data_event_open (xo_filter_data_t *dp, const char *tag, ssize_t tlen)
{
    node_t *np = dp->xfd_next;
    dp->xfd_next = NULL;

    if (np == NULL || strncmp(np->n_tag, tag, tlen) != 0
	    || np->n_tag[tlen] != '\0')
	fprintf(stderr, "xpath_02: event_open: unexpected tag '%.*s'\n",
		(int) tlen, tag);

    dp->xfd_stack = grow(dp->xfd_stack, &dp->xfd_stack_max,
			 dp->xfd_stack_count, sizeof(*dp->xfd_stack));
    dp->xfd_stack[dp->xfd_stack_count++] = np;
}

static void
data_event_close (xo_filter_data_t *dp, const char *tag, ssize_t tlen)
{
    if (dp->xfd_stack_count <= 0) {
	fprintf(stderr, "xpath_02: event_close: stack underflow '%.*s'\n",
		(int) tlen, tag);
	return;
    }

    node_t *np = dp->xfd_stack[--dp->xfd_stack_count];
    if (np && (strncmp(np->n_tag, tag, tlen) != 0
	       || np->n_tag[tlen] != '\0'))
	fprintf(stderr, "xpath_02: event_close: '%.*s' closes '%s'\n",
		(int) tlen, tag, np->n_tag);
}

static void
data_error (xo_filter_data_t *dp UNUSED, const char *fmt, va_list vap)
{
    fprintf(stderr, "xpath_02: filter error: ");
    vfprintf(stderr, fmt, vap);
    fprintf(stderr, "\n");
}

/* Append the XPath string-value of a node (its leaf and key text) */
static void
node_string_value (xo_buffer_t *xbp, node_t *np)
{
    if (np->n_value)
	xo_buf_append_str(xbp, np->n_value);

    for (int i = 0; i < np->n_items_count; i++) {
	item_t *ip = &np->n_items[i];
	if (ip->it_kind == IT_KEY)
	    xo_buf_append_str(xbp, ip->it_value);
	else if (ip->it_kind == IT_FIELD || ip->it_kind == IT_CHILD)
	    node_string_value(xbp, ip->it_child);
    }
}

/*
 * Find one step under np: an attribute ("@x"), a key, a field or a
 * child element.  Keys and attributes come back as their value via
 * *valp; elements and fields come back as a node.
 */
static node_t *
node_step (node_t *np, const char *step, ssize_t slen, const char **valp)
{
    int want_attr = (slen > 0 && *step == '@');
    if (want_attr) {
	step += 1;
	slen -= 1;
    }

    for (int i = 0; i < np->n_items_count; i++) {
	item_t *ip = &np->n_items[i];
	const char *name = ip->it_child ? ip->it_child->n_tag : ip->it_name;

	if (want_attr != (ip->it_kind == IT_ATTR))
	    continue;
	if (strncmp(name, step, slen) != 0 || name[slen] != '\0')
	    continue;

	if (ip->it_child)
	    return ip->it_child;

	*valp = ip->it_value;
	return NULL;
    }

    return NULL;
}

static const char *
data_value_of (xo_filter_data_t *dp, const char *name, ssize_t nlen)
{
    if (name == NULL || dp->xfd_stack_count <= 0)
	return NULL;
    if (nlen < 0)
	nlen = strlen(name);

    node_t *np = dp->xfd_stack[dp->xfd_stack_count - 1];
    const char *val = NULL;
    const char *cp = name, *ep = name + nlen;

    while (np && cp < ep) {
	const char *sp = memchr(cp, '/', ep - cp) ?: ep;

	if (sp - cp == 1 && *cp == '.')
	    ;			/* Self step */
	else if (sp - cp == 2 && cp[0] == '.' && cp[1] == '.')
	    np = np->n_parent;
	else {
	    val = NULL;
	    np = node_step(np, cp, sp - cp, &val);
	    if (np == NULL && val && sp < ep)
		val = NULL;	/* Can't step below a key or attribute */
	}

	cp = (sp < ep) ? sp + 1 : ep;
    }

    xo_buf_reset(&dp->xfd_value);

    if (np)
	node_string_value(&dp->xfd_value, np);
    else if (val)
	xo_buf_append_str(&dp->xfd_value, val);
    else {
	if (opt_debug)
	    fprintf(stderr, "xpath_02: value_of '%.*s' -> missing\n",
		    (int) nlen, name);
	return NULL;
    }

    xo_buf_append(&dp->xfd_value, "", 1);

    if (opt_debug)
	fprintf(stderr, "xpath_02: value_of '%.*s' -> '%s'\n",
		(int) nlen, name, dp->xfd_value.xb_bufp);

    return dp->xfd_value.xb_bufp;
}

static void
data_destroy (xo_filter_data_t *dp)
{
    for (int i = 0; i < dp->xfd_names_count; i++)
	free(dp->xfd_names[i]);
    free(dp->xfd_names);
    free(dp->xfd_stack);
    xo_buf_cleanup(&dp->xfd_value);
    free(dp);
}

static xo_filter_data_ops_t data_ops = {
    .xfdo_version = XO_FILTER_DATA_OPS_VERSION,
    .xfdo_realloc = data_realloc,
    .xfdo_free = data_free,
    .xfdo_name_intern = data_name_intern,
    .xfdo_name_eq = data_name_eq,
    .xfdo_value_of = NULL,	/* Set for the tree backend */
    .xfdo_event_open = data_event_open,
    .xfdo_event_close = data_event_close,
    .xfdo_destroy = data_destroy,
    .xfdo_error = data_error,
};

/* ---------------------------------------------------------------- */
/* The walker */

static char *
escape (const char *str)
{
    size_t len = strlen(str);
    char *buf = malloc(6 * len + 1); /* Worst case: "\u00XX" per byte */
    char *op = buf;

    if (buf == NULL)
	xo_errx(1, "out of memory");

    /*
     * Same escaping as the fullpath encoder (fullpath_append_escaped).
     * Bytes with the high bit set are parts of UTF-8 characters and
     * pass thru untouched, so the tests are made on an unsigned value.
     */
    for (const char *cp = str; *cp; cp++) {
	unsigned char ch = (unsigned char) *cp;

	if (ch == '\\' || ch == '\'' || ch == '"') {
	    *op++ = '\\';
	    *op++ = ch;
	} else if (ch == '\n') {
	    *op++ = '\\';
	    *op++ = 'n';
	} else if (ch == '\r') {
	    *op++ = '\\';
	    *op++ = 'r';
	} else if (ch == '\t') {
	    *op++ = '\\';
	    *op++ = 't';
	} else if (ch < 0x20 || ch == 0x7f) {
	    op += sprintf(op, "\\u%04x", ch);
	} else {
	    *op++ = ch;
	}
    }

    *op = '\0';
    return buf;
}

/*
 * Record how a frame's pending predicate resolved: FULL keeps every
 * line made under it, while LIVE (passed, on a path that continues
 * below the frame) keeps only the lines that were COND.  DEAD (failed
 * on a key, as libxo drops XSF_COND_KEPT) drops the lines made under
 * it, whatever an enclosing frame decides.
 */
static void
walk_resolve (walker_t *wp, int id, xo_filter_status_t how)
{
    wp->w_resolved = grow(wp->w_resolved, &wp->w_resolved_max, id, 1);
    if (wp->w_resolved[id] != XO_STATUS_FULL)
	wp->w_resolved[id] = how;
}

/*
 * Record the status of the top frame after a key/attr/field event;
 * 'rc' is the event's return value, which may be LIVE.
 */
static void
walk_note (walker_t *wp, const char *what, const char *name,
	   xo_filter_status_t rc)
{
    xo_filter_status_t st = xo_filter_walk_status(NULL, wp->w_xfp);

    if (opt_debug)
	fprintf(stderr, "xpath_02: %s '%s': rc %s, status %s\n",
		what, name, status_name(rc), status_name(st));

    if (wp->w_frames_count == 0)
	return;

    frame_t *fp = &wp->w_frames[wp->w_frames_count - 1];
    xo_filter_status_t old = fp->f_status;

    fp->f_status = st;
    if (st == XO_STATUS_FULL)
	walk_resolve(wp, fp->f_id, XO_STATUS_FULL);
    else if (rc == XO_STATUS_LIVE)
	walk_resolve(wp, fp->f_id, XO_STATUS_LIVE);
    else if (old == XO_STATUS_PRED
	     && (st == XO_STATUS_TRACK || st == XO_STATUS_DEAD))
	walk_resolve(wp, fp->f_id, XO_STATUS_DEAD);
}

static frame_t *
walk_push (walker_t *wp, node_t *np)
{
    wp->w_dp->xfd_next = np;
    if (np->n_value)		/* A field: pass its value */
	xo_filter_walk_open_field(NULL, wp->w_xfp, np->n_tag, -1,
				  np->n_value, -1);
    else
	xo_filter_walk_open(NULL, wp->w_xfp, np->n_tag, -1);

    wp->w_frames = grow(wp->w_frames, &wp->w_frames_max,
			wp->w_frames_count, sizeof(*wp->w_frames));
    frame_t *fp = &wp->w_frames[wp->w_frames_count++];
    fp->f_id = ++wp->w_next_id;
    fp->f_status = xo_filter_walk_status(NULL, wp->w_xfp);
    fp->f_leader = xo_buf_len(&wp->w_leader);

    if (opt_debug)
	fprintf(stderr, "xpath_02: open '%s' (%d): status %s\n",
		np->n_tag, fp->f_id, status_name(fp->f_status));

    return fp;
}

static void
walk_pop (walker_t *wp, node_t *np)
{
    frame_t *fp = &wp->w_frames[wp->w_frames_count - 1];
    int rc = xo_filter_walk_close(NULL, wp->w_xfp, np->n_tag, -1);

    if (opt_debug)
	fprintf(stderr, "xpath_02: close '%s' (%d): rc %s\n",
		np->n_tag, fp->f_id, status_name(rc));

    if (rc == XO_STATUS_FULL || rc == XO_STATUS_LIVE)
	walk_resolve(wp, fp->f_id, rc);

    xo_buf_set_len(&wp->w_leader, fp->f_leader);
    xo_buf_force_nul(&wp->w_leader);
    wp->w_frames_count -= 1;
}

static void
walk_line (walker_t *wp, node_t *leaf)
{
    char *esc = escape(leaf->n_value);
    frame_t *top = &wp->w_frames[wp->w_frames_count - 1];
    xo_buffer_t xb;

    xo_buf_init(&xb);
    xo_buf_append(&xb, wp->w_leader.xb_bufp, xo_buf_len(&wp->w_leader));
    xo_buf_append_str(&xb, leaf->n_tag);
    xo_buf_append_str(&xb, "='");
    xo_buf_append_str(&xb, esc);
    xo_buf_append_str(&xb, "'\n");
    xo_buf_append(&xb, "", 1);
    free(esc);

    wp->w_lines = grow(wp->w_lines, &wp->w_lines_max,
		       wp->w_lines_count, sizeof(*wp->w_lines));
    line_t *lp = &wp->w_lines[wp->w_lines_count++];
    lp->l_text = strdup(xb.xb_bufp);
    lp->l_status = top->f_status;
    xo_buf_cleanup(&xb);

    int count = 0;
    for (int i = 0; i < wp->w_frames_count; i++)
	if (wp->w_frames[i].f_status == XO_STATUS_PRED)
	    count += 1;

    if (count) {
	lp->l_pend = calloc(count, sizeof(*lp->l_pend));
	for (int i = 0; i < wp->w_frames_count; i++)
	    if (wp->w_frames[i].f_status == XO_STATUS_PRED)
		lp->l_pend[lp->l_pend_count++] = wp->w_frames[i].f_id;
    }
}

static void
walk_add (walker_t *wp, const char *xpath)
{
    if (!opt_quiet)
	fprintf(stderr, "adding filter: '%s'\n", xpath);

    int rc = xo_filter_walk_add(NULL, wp->w_xfp, xpath);

    if (!opt_quiet)
	fprintf(stderr, "added filter: %d\n", rc);
}

static void
walk_node (walker_t *wp, node_t *np)
{
    for (int i = 0; i < np->n_items_count; i++) {
	item_t *ip = &np->n_items[i];
	node_t *cp = ip->it_child;
	xo_filter_status_t st;

	switch (ip->it_kind) {
	case IT_KEY:
	    if (!opt_tree) {
		st = xo_filter_walk_key(NULL, wp->w_xfp, ip->it_name,
					strlen(ip->it_name), ip->it_value,
					strlen(ip->it_value));
		walk_note(wp, "key", ip->it_name, st);
	    }

	    /* Keys turn into predicates in the leader */
	    char *esc = escape(ip->it_value);
	    xo_buf_trim(&wp->w_leader, 1);
	    xo_buf_append(&wp->w_leader, "[", 1);
	    xo_buf_append_str(&wp->w_leader, ip->it_name);
	    xo_buf_append_str(&wp->w_leader, "='");
	    xo_buf_append_str(&wp->w_leader, esc);
	    xo_buf_append_str(&wp->w_leader, "']/");
	    xo_buf_force_nul(&wp->w_leader);
	    free(esc);
	    break;

	case IT_FILTER:
	    walk_add(wp, ip->it_name);
	    break;

	case IT_ATTR:
	    st = xo_filter_walk_attr(NULL, wp->w_xfp, ip->it_name,
				     strlen(ip->it_name), ip->it_value,
				     strlen(ip->it_value));
	    walk_note(wp, "attr", ip->it_name, st);
	    break;

	case IT_FIELD:
	    st = xo_filter_walk_status(NULL, wp->w_xfp);
	    if (!opt_tree
		    && (st == XO_STATUS_TRACK || st == XO_STATUS_PRED)) {
		st = xo_filter_walk_key(NULL, wp->w_xfp, cp->n_tag,
					strlen(cp->n_tag), cp->n_value,
					strlen(cp->n_value));
		walk_note(wp, "field", cp->n_tag, st);
	    }

	    walk_push(wp, cp);
	    walk_line(wp, cp);
	    walk_pop(wp, cp);
	    break;

	case IT_CHILD:
	    walk_push(wp, cp);
	    xo_buf_append_str(&wp->w_leader, cp->n_tag);
	    xo_buf_append(&wp->w_leader, "/", 1);
	    xo_buf_force_nul(&wp->w_leader);

	    walk_node(wp, cp);
	    walk_pop(wp, cp);
	    break;
	}
    }
}

static int
line_kept (walker_t *wp, line_t *lp)
{
    if (lp->l_status == XO_STATUS_FULL || lp->l_status == XO_STATUS_ZERO)
	return TRUE;

    int kept = FALSE;

    for (int i = 0; i < lp->l_pend_count; i++) {
	int id = lp->l_pend[i];
	if (id >= wp->w_resolved_max)
	    continue;
	if (wp->w_resolved[id] == XO_STATUS_DEAD)
	    return FALSE;
	if (wp->w_resolved[id] == XO_STATUS_FULL)
	    kept = TRUE;
	else if (wp->w_resolved[id] == XO_STATUS_LIVE
		&& lp->l_status == XO_STATUS_COND)
	    kept = TRUE;
    }

    return kept;
}

static void
segment_run (segment_t *sp)
{
    if (sp->s_filters_count == 0 && sp->s_root->n_items_count == 0)
	return;

    walker_t w;
    bzero(&w, sizeof(w));
    xo_buf_init(&w.w_leader);
    xo_buf_append(&w.w_leader, "/", 1);
    xo_buf_force_nul(&w.w_leader);

    w.w_dp = calloc(1, sizeof(*w.w_dp));
    if (w.w_dp == NULL)
	xo_errx(1, "out of memory");
    xo_buf_init(&w.w_dp->xfd_value);

    data_ops.xfdo_value_of = opt_tree ? data_value_of : NULL;

    w.w_xfp = xo_filter_create_with_data(NULL, w.w_dp, &data_ops);
    if (w.w_xfp == NULL)
	xo_errx(1, "allocation of filter failed");

    for (int i = 0; i < sp->s_filters_count; i++)
	walk_add(&w, sp->s_filters[i]);

    /* Walk the top-level items as if under a frame-less root */
    frame_t root = { .f_id = 0, .f_status = XO_STATUS_ZERO };
    w.w_frames = grow(w.w_frames, &w.w_frames_max, 0, sizeof(*w.w_frames));
    w.w_frames[w.w_frames_count++] = root;

    walk_node(&w, sp->s_root);

    for (int i = 0; i < w.w_lines_count; i++) {
	line_t *lp = &w.w_lines[i];
	if (line_kept(&w, lp))
	    fputs(lp->l_text, stdout);
	free(lp->l_text);
	free(lp->l_pend);
    }
    fflush(stdout);

    if (w.w_dp->xfd_stack_count != 0)
	fprintf(stderr, "xpath_02: node stack depth %d at end\n",
		w.w_dp->xfd_stack_count);

    /* Frees w_dp via xfdo_destroy */
    xo_filter_destroy_standalone(w.w_xfp);
    xo_set_filter_data(NULL, NULL);

    free(w.w_lines);
    free(w.w_frames);
    free(w.w_resolved);
    xo_buf_cleanup(&w.w_leader);
}

/* ---------------------------------------------------------------- */
/* Input */

static int
do_work (segment_t *sp, const char *filename, FILE *in)
{
    char *cp, buf[BUFSIZ];
    char incfile[MAXPATHLEN];
    const char *lastslash = filename ? strrchr(filename, '/') : NULL;
    char *field, *value;

    for (;;) {
	cp = fgets(buf, sizeof(buf), in);
	if (cp == NULL)
	    break;

	cp = trim(cp);

	if (*cp == 'r') {
	    /* Reset: run what we have and start the next case */
	    segment_run(sp);
	    segment_reset(sp);
	    this_case += 1;
	    continue;
	}

	if (opt_case && opt_case != this_case)
	    continue;

	switch (*cp) {
	case '#':
	case ' ':
	case '\0':
	case '{':		/* Lists and leaf-lists aren't frames */
	case '}':
	case '[':
	case ']':
	    break;

	case '?':
	    if (opt_filter)
		break;		/* Ignore if there's a command-line filter */

	    /* Kept in place: libxo adds it at this point in the stream */
	    node_add_item(sp->s_cur, IT_FILTER, trim(cp + 1), NULL);
	    break;

	case '+':
	case '<':
	    sp->s_cur = node_add_item(sp->s_cur, IT_CHILD,
				      trim(cp + 1), NULL)->it_child;
	    break;

	case '-':
	case '>':
	    if (sp->s_cur->n_parent)
		sp->s_cur = sp->s_cur->n_parent;
	    else
		fprintf(stderr, "xpath_02: extra close '%s'\n", cp);
	    break;

	case '=':
	case '$':
	case '@':
	    field = trim(cp + 1);
	    value = clean_token(field);
	    if (!*field || !*value)
		break;
	    if (*cp != '@')
		value = empty_token(value);

	    node_add_item(sp->s_cur,
			  (*cp == '=') ? IT_FIELD
			  : (*cp == '$') ? IT_KEY : IT_ATTR,
			  field, value);
	    break;

	case 'i':		/* Include */
	    value = trim(cp + 1);

	    /* If the path isn't absolute, base it on the previous path */
	    if (*value != '/' && lastslash) {
		snprintf(incfile, sizeof(incfile), "%.*s/%s",
			 (int)(lastslash - filename), filename, value);
		value = incfile;
	    }

	    FILE *newp = fopen(value, "r");
	    if (newp == NULL) {
		fprintf(stderr, "xpath_02: include: could not open '%s'\n",
			value);
		break;
	    }

	    do_work(sp, value, newp);
	    fclose(newp);
	    break;

	case 'q':
	    return 0;

	default:
	    fprintf(stderr, "xpath_02: invalid line '%s'\n", cp);
	}
    }

    return 0;
}

static inline const char *
get_arg (const char *arg, const char *msg)
{
    if (arg == NULL)
	xo_errx(1, "missing arg: %s", msg);

    return arg;
}

int
main (int argc, char **argv)
{
    const char *opt_input = NULL;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    for (int i = 1; argv[i]; i++) {
	if (xo_streq(argv[i], "debug"))
	    opt_debug = 1;
	else if (xo_streq(argv[i], "filter"))
	    opt_filter = get_arg(argv[++i], "filter name");
	else if (xo_streq(argv[i], "input"))
	    opt_input = get_arg(argv[++i], "input file");
	else if (xo_streq(argv[i], "case")) /* Execute only the nth case */
	    opt_case = atoi(get_arg(argv[++i], "case number"));
	else if (xo_streq(argv[i], "quiet"))
	    opt_quiet = 1;
	else if (xo_streq(argv[i], "tree"))
	    opt_tree = 1;
	else if (xo_streq(argv[i], "stream"))
	    opt_tree = 0;
	else
	    xo_errx(1, "unknown argument: '%s'", argv[i]);
    }

    if (opt_debug)
	xo_set_flags(NULL, XOF_DEBUG);

    FILE *in = opt_input ? fopen(opt_input, "r") : stdin;
    if (in == NULL)
	xo_err(1, "could not open file '%s'", opt_input);

    segment_t seg;
    segment_init(&seg);

    do_work(&seg, opt_input, in);
    segment_run(&seg);
    segment_clean(&seg);

    return 0;
}
