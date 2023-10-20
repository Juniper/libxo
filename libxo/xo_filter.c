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
#include "xo_filter.h"

#define XO_MATCHES_DEF	32	/* Number of states allocated by default */

typedef uint32_t xo_filter_flags_t; /* Flags from filter test */

/* Flags for xo_filter_flags_t: */
#define XFIF_TRUE	(1 << 0) /* This part is true */
#define XFFI_MISSING	(1 << 1) /* A referenced element is missing  */

typedef struct xo_match_s {
    xo_xparse_node_id_t xm_base; /* Start node of this path */
    xo_xparse_node_id_t xm_next; /* Next node we are looking to match */
    xo_xparse_node_id_t xm_prev; /* Previous node: what we just matched */
    xo_xparse_node_id_t xm_predicates; /* Predicates */
    uint32_t xm_depth;	         /* Number of open containers past match */
    uint32_t xm_flags;	         /* Flags for this match instance (XMF_*) */
    char *xm_keys;		 /* Keys stored as "key\0val\0k2\0v2\0\0"*/
    xo_ssize_t xm_keys_len;	 /* Length of xm_keys */
} xo_match_t;

#define xm_next_free xm_depth	/* Reuse field when free */

/* Flags fpr xm_flags */
#define XMF_NOT		(1<<0)	/* Not expression ("!a") */
#define XMF_PREDICATE	(1<<1)	/* Predicate expression */

struct xo_filter_s {		   /* Forward/typdef decl in xo_private.h */
    struct xo_xparse_data_s xf_xd; /* Main parsing structure */
    uint32_t xf_allow;		   /* Number of successful matches */
    uint32_t xf_deny;		   /* Number of successful not matches */

    xo_match_t *xf_matches;	/* Current states */
    uint32_t xf_matches_cur;	/* Current depth of xf_paths[] */
    uint32_t xf_matches_max;	/* Max depth of xf_paths[] */
    uint32_t xf_matches_free;	/* List of free matches */

    unsigned xf_flags;		/* Flags (XFSF_*) */
};

/* Flags for xf_flags */
#define XFSF_BLOCK	(1<<0)	/* Block emitting data */

xo_filter_t *
xo_filter_create (xo_handle_t *xop)
{
    xo_filter_t *xfp = xo_realloc(NULL, sizeof(*xfp));
    if (xfp == NULL)
	return NULL;

    bzero(xfp, sizeof(*xfp));

    xo_xparse_init(&xfp->xf_xd);
    xfp->xf_xd.xd_xop = xop;

    return xfp;
}

xo_xparse_data_t *
xo_filter_data (xo_filter_t *xfp)
{
    return &xfp->xf_xd;
}

void
xo_filter_destroy (xo_filter_t *xfp)
{
    xo_xparse_clean(&xfp->xf_xd);

    if (xfp->xf_matches)
	xo_free(xfp->xf_matches);

    xo_free(xfp);
}

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

static void
xo_filter_dump_matches (xo_filter_t *xfp)
{
    if (xfp == NULL)
	return;

    /*
     * Whiffle thru the states to see if we have any open paths.  We
     * do this first since we'll be pushing new paths.
     */
    xo_xparse_data_t *xdp = &xfp->xf_xd;
    xo_match_t *xmp = xfp->xf_matches;
    uint32_t cur = xfp->xf_matches_cur;
    uint32_t i;
    xo_xparse_node_t *xnp;

    for (i = 0; i < cur; i++, xmp++) {	/* For each active match */
	if (xmp->xm_next == 0)
	    continue;

	xnp = xo_xparse_node(xdp, xmp->xm_next);
	const char *str = xo_xparse_str(xdp, xnp->xn_str) ?: "";

	xo_dbg(NULL, "filter: match [%u] '%s' [base %u/prev %u/next %u]",
	       i, str, xmp->xm_base, xnp->xn_prev, xnp->xn_next);
    }
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

static int
xo_filter_allow (xo_handle_t *xop UNUSED, xo_filter_t *xfp)
{
    /* No filters means always allow */
    if (xfp == NULL || xfp->xf_xd.xd_paths_cur == 0)
	return TRUE;

    int all_nots = (xfp->xf_xd.xd_flags & XDF_ALL_NOTS) ? TRUE : FALSE;

    if (xfp->xf_deny)
	return FALSE;		/* No means no */

    if (xfp->xf_allow)
	return TRUE;

    if (all_nots)
	return TRUE;

    return FALSE;
}

static int
xo_filter_open (xo_handle_t *xop UNUSED, xo_filter_t *xfp,
		const char *tag, const char *type)
{
    if (xfp == NULL)
	return 0;

    /*
     * Whiffle thru the states to see if we have any open paths.  We
     * do this first since we'll be pushing new paths.
     */
    xo_xparse_data_t *xdp = &xfp->xf_xd;
    xo_match_t *xmp = xfp->xf_matches;
    uint32_t cur = xfp->xf_matches_cur;
    uint32_t i;
    xo_xparse_node_t *xnp;

    for (i = 0; i < cur; i++, xmp++) {	/* For each active match */
	if (xmp->xm_next == 0) {	/* Already matched */
	    xmp->xm_depth += 1;
	    continue;
	}

	xnp = xo_xparse_node(xdp, xmp->xm_next);

	if (xnp->xn_type != C_ELEMENT) /* Only type supported */
	    continue;

	const char *str = xo_xparse_str(xdp, xnp->xn_str);
	if (str == NULL || !xo_streq(str, tag))
	    continue;

	const char *label = "";

	xmp->xm_prev = xmp->xm_next; /* Hold on to the last match */
	xmp->xm_next = xnp->xn_next; /* Move along to the next node */
	if (xnp->xn_next == 0) {
	    if (xmp->xm_flags & XMF_NOT) {
		xfp->xf_deny += 1;
		label = " deny++";
	    } else {
		xfp->xf_allow += 1;
		label = " allow++";
	    }
	}

	/* A succesful match */
	xo_dbg(NULL, "filter: open %s: progress match [%u] '%s' "
	       "[base %u/prev %u/next %u] [%u/%u]%s",
	       type, i, tag, xmp->xm_base, xmp->xm_prev, xmp->xm_next,
	       xfp->xf_allow, xfp->xf_deny, label);
    }

    xo_xparse_node_id_t *paths = xfp->xf_xd.xd_paths;
    cur = xfp->xf_xd.xd_paths_cur;
    for (i = 0; i < cur; i++, paths++) {
	xnp = xo_xparse_node(xdp, *paths);
	if (xnp == NULL)
	    continue;

	int not = FALSE;

	switch (xnp->xn_type) {
	case C_ELEMENT:
	    /* Normal case */
	    break;

	case C_NOT:
	    /* A "not" is a path that it negated */
	    not = TRUE;
	    /* fallthru */

	case C_PATH:
	    /* A path contains a set of elements to match */
	    xnp = xo_xparse_node(xdp, xnp->xn_contents);
	    if (xnp->xn_type != C_ELEMENT)
		continue;
	    break;

	default:
	    continue;
	}

	const char *str = xo_xparse_str(xdp, xnp->xn_str);
	if (str == NULL || !xo_streq(str, tag))
	    continue;

	/* A succesful match! Grab a new match struct and fill it in  */
	xmp = xo_filter_match_new(xfp);
	if (xmp == NULL)
	    continue;

	xmp->xm_base = *paths;
	xmp->xm_prev = 0;
	xmp->xm_next = xnp->xn_next;
	if (not)
	    xmp->xm_flags |= XMF_NOT;

	const char *label = "";

	if (xnp->xn_contents) {
	    xo_xparse_node_t *cnp = xo_xparse_node(xdp, xnp->xn_contents);
	    if (cnp->xn_type == C_PREDICATE) {
		/* Mark these predicates as our's */
		xmp->xm_predicates = xnp->xn_contents;
	    }

	} else if (xmp->xm_next == 0) {
	    if (xmp->xm_flags & XMF_NOT) {
		xfp->xf_deny += 1;
		label = " deny++";
	    } else {
		xfp->xf_allow += 1;
		label = " allow++";
	    }
	}

	xo_dbg(NULL, "filter: open %s: new match [%u] '%s' [%u/%u] "
	       "[%u/%u] %s",
	       type, xmp - xfp->xf_matches, tag, *paths, xnp->xn_next,
	       xfp->xf_allow, xfp->xf_deny, label);
    }

    xo_filter_dump_matches(xfp);

    return xo_filter_allow(xop, xfp);
}

int
xo_filter_open_container (xo_handle_t *xop, xo_filter_t *xfp, const char *tag)
{
    return xo_filter_open(xop, xfp, tag, "container");
}

int
xo_filter_open_instance (xo_handle_t *xop, xo_filter_t *xfp, const char *tag)
{
    return xo_filter_open(xop, xfp, tag, "list");
}

/*
 * Add a keys for xm_keys for the match.
 *
 * We store the keys in a simple-but-slow style that might need
 * updated/optimized later.  For now, it "key\0val\0k2\0v2\0\0" So
 * names and values are NUL terminated with another NUL to end the
 * list.  The number of keys should be low (typically one) so the
 * efficiency shouldn't matter.
 */
static void UNUSED
xo_filter_key_add (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		   xo_match_t *xmp,
		   const char *tag, xo_ssize_t tlen,
		   const char *value, xo_ssize_t vlen)
{
    xo_ssize_t new_len = tlen + vlen + 3;
    char *newp = xo_realloc(xmp->xm_keys, xmp->xm_keys_len + new_len);

    if (newp == NULL)
	return;

    char *addp = newp + xmp->xm_keys_len;
    memcpy(addp, tag, tlen);
    addp += tlen;
    *addp++ = '\0';
    memcpy(addp, value, vlen);
    addp += vlen;
    *addp++ = '\0';
    *addp++ = '\0';

    xmp->xm_keys_len += new_len;
    xmp->xm_keys = newp;
}

static const char * UNUSED
xo_filter_key_find (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		    xo_match_t *xmp, const char *tag)
{
    xo_ssize_t off = 0;
    xo_ssize_t len = xmp->xm_keys_len;
    char *cp = xmp->xm_keys;

    while (off < len) {
	if (*cp == '\0')	/* SNO: sanity check */
	    break;

	xo_ssize_t klen = strlen(cp);
	if (xo_streq(tag, cp))	/* Match! */
	    return cp + klen + 1;

	xo_ssize_t vlen = strlen(cp + klen + 1);
	xo_ssize_t tlen = klen + 1 + vlen + 1;

	off += tlen;		/* Skip over this entry */
	cp += tlen;
    }

    return NULL;
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
static xo_filter_flags_t
xo_filter_pred_eval (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		     xo_match_t *xmp UNUSED)
{
    xo_filter_flags_t myflags UNUSED = 0;
    xo_filter_flags_t subflags UNUSED = 0;

    return myflags;
}

/*
 * Recurse down the complete predicate, seeing if it even wants the
 * tag.
 */
static int
xo_filter_pred_needs (xo_handle_t *xop, xo_xparse_data_t *xdp,
		      xo_filter_t *xfp, xo_xparse_node_id_t id,
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
	}

	if (xnp->xn_contents)
	    if (xo_filter_pred_needs(xop, xdp, xfp, xnp->xn_contents,
				     tag, tlen))
		return TRUE;
    }

    return FALSE;
}

int
xo_filter_key (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,
		      const char *tag UNUSED, xo_ssize_t tlen UNUSED,
		      const char *value UNUSED, xo_ssize_t vlen UNUSED)
{
    xo_xparse_data_t *xdp = &xfp->xf_xd;
    xo_match_t *xmp = xfp->xf_matches;
    uint32_t cur = xfp->xf_matches_cur;
    uint32_t i;
    xo_xparse_node_t *xnp;


    for (i = 0; i < cur; i++, xmp++) {	/* For each active match */
	if (xmp->xm_next == 0) {	/* Already matched */
	    xmp->xm_depth += 1;
	    continue;
	}

	xnp = xo_xparse_node(xdp, xmp->xm_next);

	if (xnp->xn_type != C_PREDICATE) /* Only type supported */
	    continue;

	if (!xo_filter_pred_needs(xop, xdp, xfp, xmp->xm_next, tag, tlen))
	    continue;

	const char *str = xo_xparse_str(xdp, xnp->xn_str);
	if (str == NULL || !xo_streq(str, tag))
	    continue;

	xo_filter_key_add(xop, xfp, xmp, tag, tlen, value, vlen);

	xo_filter_flags_t flags = xo_filter_pred_eval(xop, xfp, xmp);

	xo_dbg(NULL, "filter: new key: pred eval [%u] '%s' "
	       "[base %u/prev %u/next %u] [%u/%u]%s",
	       i, tag, xmp->xm_base, xmp->xm_prev, xmp->xm_next,
	       xfp->xf_allow, xfp->xf_deny, flags);
    }

    return 0;
}

/*
 * We've seen the first non-key child or the end of the instance, so
 * we need to evaluate any open predicates are see if we should discard
 * our buffered output.
 */
int
xo_filter_key_done (xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED)
{
    
    
    return 0;
}

static int
xo_filter_close (xo_handle_t *xop UNUSED, xo_filter_t *xfp,
		 const char *tag, const char *type)
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
    xo_xparse_node_id_t prev;

    for (i = 0; i < cur; i++, xmp++) {	/* For each active match */
	if (xmp->xm_depth != 0) {	/* Already seen nested tags */
	    xmp->xm_depth -= 1;
	    continue;
	}

	/*
	 * xmp->xm_prev is zero, then we are at the start of the path,
	 * so we use xm_base
	 */
	if (xmp->xm_prev == 0) {
	    prev = xmp->xm_base;
	    xnp = xo_xparse_node(xdp, prev);
	    if (xnp == NULL)
		continue;

	    if (xnp->xn_type == C_NOT || xnp->xn_type == C_PATH)
		prev = xnp->xn_contents;

	} else {
	    prev = xmp->xm_prev;
	}

	xnp = xo_xparse_node(xdp, prev);
	if (xnp->xn_type != C_ELEMENT) /* Only type supported */
	    continue;

	const char *str = xo_xparse_str(xdp, xnp->xn_str);
	if (str == NULL || !xo_streq(str, tag))
	    continue;

	const char *label = "";

	/* If we're at the end of the path, adjust the allow/deny numbers */
	if (xnp->xn_next == 0) {
	    if (xmp->xm_flags & XMF_NOT) {
		xfp->xf_deny -= 1;
		label = " deny--";
	    } else {
		xfp->xf_allow -= 1;
		label = " allow--";
	    }
	}

	xmp->xm_next = xmp->xm_prev;
	xmp->xm_prev = xnp->xn_prev;

	/* A succesful un-match */
	xo_dbg(NULL, "filter: close %s match [%u]: progress match '%s' "
	       "[base %u/prev %u/next %u] [%u/%u]%s",
	       type, i, tag, xmp->xm_base, xmp->xm_prev, xmp->xm_next,
	       xfp->xf_allow, xfp->xf_deny, label);
    }

    xo_filter_dump_matches(xfp);

    return xo_filter_allow(xop, xfp);
}

int
xo_filter_close_instance (xo_handle_t *xop, xo_filter_t *xfp, const char *tag)
{
    return xo_filter_close(xop, xfp, tag, "instance");
}

int
xo_filter_close_container (xo_handle_t *xop, xo_filter_t *xfp, const char *tag)
{
    return xo_filter_close(xop, xfp, tag, "container");
}
