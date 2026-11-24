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
 * xpath_03: evaluate XPath expressions against a small in-memory tree,
 * through the node-set data ops (xfdo_ns_*, xfdo_step, ...).  This
 * exercises location paths, predicates, comparisons and unions in
 * xo_eval without libpin.
 *
 * The input is an XML document, a line holding "%%", then one
 * expression per line ('#' starts a comment line).  Each expression
 * is evaluated with the document node as the context node, and its
 * result is printed in the same form that xpath_03.xsl gives under
 * xsltproc, so the two can be diffed.
 *
 * The XML reader is deliberately small: elements, attributes, text,
 * and the five predefined entities.  Comments, processing instructions
 * and the XML declaration are skipped.
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

#include "xo_xpath.h"
#include "xo_xparse.h"
#include "xo_filter.h"

static int opt_debug;
static int opt_quiet;

#define NK_DOC		1
#define NK_ELEMENT	2
#define NK_ATTR		3
#define NK_TEXT		4

/*
 * Nodes live in one array, in document order, so a node's id
 * (index + 1) is also its document-order key.  An element's
 * attributes are made before its children, as XPath orders them.
 */
typedef struct tnode_s {
    int tn_kind;
    char *tn_name;
    char *tn_value;		/* Attribute and text nodes */
    xo_eval_node_t tn_parent;
    xo_eval_node_t *tn_kids;
    int tn_kids_count;
    xo_eval_node_t *tn_attrs;
    int tn_attrs_count;
} tnode_t;

typedef struct tset_s {
    xo_eval_node_t *ts_nodes;
    uint32_t ts_count;
    uint32_t ts_max;
    int ts_used;
} tset_t;

struct xo_filter_data_s {
    tnode_t *xfd_nodes;
    uint32_t xfd_nodes_count;
    tset_t *xfd_sets;
    uint32_t xfd_sets_count;
    uint32_t xfd_sets_live;	/* Sets allocated and not freed */
    xo_buffer_t xfd_value;	/* Scratch for string_value */
};

static void *
xrealloc (void *ptr, size_t size)
{
    void *res = realloc(ptr, size);
    if (res == NULL)
	xo_errx(1, "out of memory");

    return res;
}

static tnode_t *
tree_node (xo_filter_data_t *dp, xo_eval_node_t id)
{
    if (id == 0 || id > dp->xfd_nodes_count)
	return NULL;

    return &dp->xfd_nodes[id - 1];
}

static xo_eval_node_t
tree_add (xo_filter_data_t *dp, int kind, const char *name, ssize_t nlen,
	  const char *value, ssize_t vlen, xo_eval_node_t parent)
{
    dp->xfd_nodes = xrealloc(dp->xfd_nodes,
			     (dp->xfd_nodes_count + 1) * sizeof(tnode_t));

    tnode_t *tnp = &dp->xfd_nodes[dp->xfd_nodes_count];
    bzero(tnp, sizeof(*tnp));
    dp->xfd_nodes_count += 1;

    xo_eval_node_t id = dp->xfd_nodes_count;

    tnp->tn_kind = kind;
    tnp->tn_name = name ? strndup(name, nlen) : NULL;
    tnp->tn_value = value ? strndup(value, vlen) : NULL;
    tnp->tn_parent = parent;

    tnode_t *pp = tree_node(dp, parent);
    if (pp && kind == NK_ATTR) {
	pp->tn_attrs = xrealloc(pp->tn_attrs,
				(pp->tn_attrs_count + 1) * sizeof(id));
	pp->tn_attrs[pp->tn_attrs_count++] = id;
    } else if (pp) {
	pp->tn_kids = xrealloc(pp->tn_kids,
			       (pp->tn_kids_count + 1) * sizeof(id));
	pp->tn_kids[pp->tn_kids_count++] = id;
    }

    return id;
}

/*
 * Decode the predefined entities in [cp, ep) into a new string.
 */
static char *
tree_decode (const char *cp, const char *ep, ssize_t *lenp)
{
    char *res = xrealloc(NULL, ep - cp + 1);
    char *rp = res;
    static const struct {
	const char *e_name;
	char e_ch;
    } ents[] = {
	{ "&lt;", '<' }, { "&gt;", '>' }, { "&amp;", '&' },
	{ "&quot;", '"' }, { "&apos;", '\'' }, { NULL, 0 }
    };

    while (cp < ep) {
	int hit = FALSE;

	if (*cp == '&') {
	    for (int i = 0; ents[i].e_name; i++) {
		size_t elen = strlen(ents[i].e_name);
		if ((size_t) (ep - cp) >= elen
			&& strncmp(cp, ents[i].e_name, elen) == 0) {
		    *rp++ = ents[i].e_ch;
		    cp += elen;
		    hit = TRUE;
		    break;
		}
	    }
	}

	if (!hit)
	    *rp++ = *cp++;
    }

    *rp = '\0';
    *lenp = rp - res;
    return res;
}

static void
tree_text (xo_filter_data_t *dp, const char *cp, const char *ep,
	   xo_eval_node_t parent)
{
    if (cp >= ep || parent == 1)
	return;

    ssize_t len;
    char *str = tree_decode(cp, ep, &len);
    tree_add(dp, NK_TEXT, NULL, 0, str, len, parent);
    free(str);
}

/*
 * Parse the document in 'buf' into dp's node array.  Node 1 is the
 * document node.
 */
static void
tree_parse (xo_filter_data_t *dp, const char *buf)
{
    xo_eval_node_t stack[256];
    int depth = 0;
    const char *cp = buf;
    const char *text = cp;

    stack[depth] = tree_add(dp, NK_DOC, NULL, 0, NULL, 0, 0);

    while (*cp) {
	if (*cp != '<') {
	    cp += 1;
	    continue;
	}

	tree_text(dp, text, cp, stack[depth]);

	if (strncmp(cp, "<!--", 4) == 0) {
	    const char *ep = strstr(cp, "-->");
	    cp = ep ? ep + 3 : cp + strlen(cp);
	    text = cp;
	    continue;
	}

	if (cp[1] == '?') {
	    const char *ep = strstr(cp, "?>");
	    cp = ep ? ep + 2 : cp + strlen(cp);
	    text = cp;
	    continue;
	}

	if (cp[1] == '/') {
	    const char *ep = strchr(cp, '>');
	    cp = ep ? ep + 1 : cp + strlen(cp);
	    if (depth > 0)
		depth -= 1;
	    text = cp;
	    continue;
	}

	cp += 1;
	const char *name = cp;
	while (*cp && !isspace((int) *cp) && *cp != '>' && *cp != '/')
	    cp += 1;

	xo_eval_node_t id = tree_add(dp, NK_ELEMENT, name, cp - name,
				     NULL, 0, stack[depth]);

	for (;;) {
	    while (isspace((int) *cp))
		cp += 1;

	    if (*cp == '\0' || *cp == '>' || *cp == '/')
		break;

	    const char *aname = cp;
	    while (*cp && *cp != '=' && !isspace((int) *cp))
		cp += 1;
	    ssize_t alen = cp - aname;

	    while (*cp && *cp != '"' && *cp != '\'')
		cp += 1;
	    if (*cp == '\0')
		break;

	    char quote = *cp++;
	    const char *val = cp;
	    while (*cp && *cp != quote)
		cp += 1;

	    ssize_t vlen;
	    char *str = tree_decode(val, cp, &vlen);
	    tree_add(dp, NK_ATTR, aname, alen, str, vlen, id);
	    free(str);

	    if (*cp)
		cp += 1;
	}

	int empty = (*cp == '/');
	while (*cp && *cp != '>')
	    cp += 1;
	if (*cp)
	    cp += 1;

	if (!empty && depth + 1 < (int) (sizeof(stack) / sizeof(stack[0]))) {
	    depth += 1;
	    stack[depth] = id;
	}

	text = cp;
    }
}

static void *
data_realloc (xo_filter_data_t *dp UNUSED, void *ptr, size_t size)
{
    return xrealloc(ptr, size);
}

static void
data_free (xo_filter_data_t *dp UNUSED, void *ptr)
{
    free(ptr);
}

static int
data_name_eq (xo_filter_data_t *dp UNUSED, xo_name_id_t name UNUSED,
	      const char *tag UNUSED, ssize_t tlen UNUSED)
{
    return FALSE;
}

static tset_t *
set_find (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    if (ns == 0 || ns > dp->xfd_sets_count)
	return NULL;

    tset_t *tsp = &dp->xfd_sets[ns - 1];
    return tsp->ts_used ? tsp : NULL;
}

static xo_eval_ns_t
data_ns_alloc (xo_filter_data_t *dp)
{
    uint32_t i;

    for (i = 0; i < dp->xfd_sets_count; i++)
	if (!dp->xfd_sets[i].ts_used)
	    break;

    if (i == dp->xfd_sets_count) {
	dp->xfd_sets = xrealloc(dp->xfd_sets,
				(dp->xfd_sets_count + 1) * sizeof(tset_t));
	bzero(&dp->xfd_sets[i], sizeof(tset_t));
	dp->xfd_sets_count += 1;
    }

    dp->xfd_sets[i].ts_used = TRUE;
    dp->xfd_sets[i].ts_count = 0;
    dp->xfd_sets_live += 1;

    return i + 1;
}

static void
data_ns_free (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    tset_t *tsp = set_find(dp, ns);
    if (tsp == NULL)
	return;

    tsp->ts_used = FALSE;
    tsp->ts_count = 0;
    dp->xfd_sets_live -= 1;
}

static int
data_ns_append (xo_filter_data_t *dp, xo_eval_ns_t ns, xo_eval_node_t node)
{
    tset_t *tsp = set_find(dp, ns);
    if (tsp == NULL)
	return -1;

    if (tsp->ts_count >= tsp->ts_max) {
	tsp->ts_max = tsp->ts_max ? tsp->ts_max * 2 : 8;
	tsp->ts_nodes = xrealloc(tsp->ts_nodes,
				 tsp->ts_max * sizeof(xo_eval_node_t));
    }

    tsp->ts_nodes[tsp->ts_count++] = node;
    return 0;
}

static uint32_t
data_ns_count (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    tset_t *tsp = set_find(dp, ns);

    return tsp ? tsp->ts_count : 0;
}

static xo_eval_node_t
data_ns_get (xo_filter_data_t *dp, xo_eval_ns_t ns, uint32_t idx)
{
    tset_t *tsp = set_find(dp, ns);
    if (tsp == NULL || idx >= tsp->ts_count)
	return 0;

    return tsp->ts_nodes[idx];
}

static xo_eval_ns_t
data_ns_copy (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    xo_eval_ns_t copy = data_ns_alloc(dp);
    uint32_t count = data_ns_count(dp, ns);

    for (uint32_t i = 0; i < count; i++)
	data_ns_append(dp, copy, data_ns_get(dp, ns, i));

    return copy;
}

/*
 * Does 'id' pass the node test?  The principal node type is attribute
 * on the attribute axis and element elsewhere.
 */
static int
data_test (xo_filter_data_t *dp, xo_eval_node_t id, xo_eval_axis_t axis,
	   xo_eval_ntest_t ntest, const char *name, ssize_t len)
{
    tnode_t *tnp = tree_node(dp, id);
    int principal = (axis == XO_AXIS_ATTRIBUTE) ? NK_ATTR : NK_ELEMENT;

    switch (ntest) {
    case XO_NTEST_NODE:
	return TRUE;

    case XO_NTEST_TEXT:
	return tnp->tn_kind == NK_TEXT;

    case XO_NTEST_STAR:
	return tnp->tn_kind == principal;

    case XO_NTEST_NAME:
	return tnp->tn_kind == principal
	    && (ssize_t) strlen(tnp->tn_name) == len
	    && strncmp(tnp->tn_name, name, len) == 0;
    }

    return FALSE;
}

static void
data_step_add (xo_filter_data_t *dp, xo_eval_node_t id, xo_eval_axis_t axis,
	       xo_eval_ntest_t ntest, const char *name, ssize_t len,
	       xo_eval_ns_t ns)
{
    if (data_test(dp, id, axis, ntest, name, len))
	data_ns_append(dp, ns, id);
}

static void
data_step_descend (xo_filter_data_t *dp, xo_eval_node_t id,
		   xo_eval_ntest_t ntest, const char *name, ssize_t len,
		   xo_eval_ns_t ns)
{
    tnode_t *tnp = tree_node(dp, id);

    for (int i = 0; i < tnp->tn_kids_count; i++) {
	xo_eval_node_t kid = tnp->tn_kids[i];

	data_step_add(dp, kid, XO_AXIS_DESCENDANT, ntest, name, len, ns);
	data_step_descend(dp, kid, ntest, name, len, ns);
    }
}

static int
data_step (xo_filter_data_t *dp, xo_eval_node_t id, xo_eval_axis_t axis,
	   xo_eval_ntest_t ntest, const char *name, ssize_t len,
	   xo_eval_ns_t ns)
{
    tnode_t *tnp = tree_node(dp, id);
    if (tnp == NULL || set_find(dp, ns) == NULL)
	return -1;

    switch (axis) {
    case XO_AXIS_SELF:
	data_step_add(dp, id, axis, ntest, name, len, ns);
	break;

    case XO_AXIS_CHILD:
	for (int i = 0; i < tnp->tn_kids_count; i++)
	    data_step_add(dp, tnp->tn_kids[i], axis, ntest, name, len, ns);
	break;

    case XO_AXIS_ATTRIBUTE:
	for (int i = 0; i < tnp->tn_attrs_count; i++)
	    data_step_add(dp, tnp->tn_attrs[i], axis, ntest, name, len, ns);
	break;

    case XO_AXIS_PARENT:
	if (tnp->tn_parent)
	    data_step_add(dp, tnp->tn_parent, axis, ntest, name, len, ns);
	break;

    case XO_AXIS_ANCESTOR_OR_SELF:
	data_step_add(dp, id, axis, ntest, name, len, ns);
	/* fall through */

    case XO_AXIS_ANCESTOR:
	for (id = tnp->tn_parent; id; id = tree_node(dp, id)->tn_parent)
	    data_step_add(dp, id, axis, ntest, name, len, ns);
	break;

    case XO_AXIS_DESCENDANT_OR_SELF:
	data_step_add(dp, id, axis, ntest, name, len, ns);
	/* fall through */

    case XO_AXIS_DESCENDANT:
	data_step_descend(dp, id, ntest, name, len, ns);
	break;

    default:
	return -1;
    }

    return 0;
}

static void
data_string_append (xo_filter_data_t *dp, xo_eval_node_t id)
{
    tnode_t *tnp = tree_node(dp, id);

    if (tnp->tn_kind == NK_TEXT) {
	xo_buf_append_str(&dp->xfd_value, tnp->tn_value);
	return;
    }

    for (int i = 0; i < tnp->tn_kids_count; i++)
	data_string_append(dp, tnp->tn_kids[i]);
}

static const char *
data_string_value (xo_filter_data_t *dp, xo_eval_node_t id)
{
    tnode_t *tnp = tree_node(dp, id);
    if (tnp == NULL)
	return "";

    if (tnp->tn_kind == NK_ATTR || tnp->tn_kind == NK_TEXT)
	return tnp->tn_value;

    xo_buf_reset(&dp->xfd_value);
    data_string_append(dp, id);
    xo_buf_append(&dp->xfd_value, "", 1);
    return dp->xfd_value.xb_bufp;
}

static const char *
data_node_name (xo_filter_data_t *dp, xo_eval_node_t id)
{
    tnode_t *tnp = tree_node(dp, id);

    return (tnp && tnp->tn_name) ? tnp->tn_name : "";
}

static int
data_doc_compare (xo_filter_data_t *dp UNUSED, xo_eval_node_t one,
		  xo_eval_node_t two)
{
    return (one < two) ? -1 : (one > two) ? 1 : 0;
}

static xo_filter_data_ops_t data_ops = {
    .xfdo_version = XO_FILTER_DATA_OPS_VERSION,
    .xfdo_realloc = data_realloc,
    .xfdo_free = data_free,
    .xfdo_name_eq = data_name_eq,
    .xfdo_ns_alloc = data_ns_alloc,
    .xfdo_ns_free = data_ns_free,
    .xfdo_ns_append = data_ns_append,
    .xfdo_ns_count = data_ns_count,
    .xfdo_ns_get = data_ns_get,
    .xfdo_ns_copy = data_ns_copy,
    .xfdo_step = data_step,
    .xfdo_string_value = data_string_value,
    .xfdo_node_name = data_node_name,
    .xfdo_doc_compare = data_doc_compare,
};

/*
 * Print a string with newlines as spaces, matching the
 * translate() in xpath_03.xsl.
 */
static void
print_flat (const char *str)
{
    for (const char *cp = str; *cp; cp++)
	putchar(*cp == '\n' ? ' ' : *cp);
}

static void
print_nodes (xo_filter_data_t *dp, xo_eval_ns_t ns)
{
    static const char *kinds[] = {
	"", "root", "element", "attribute", "text"
    };
    uint32_t count = data_ns_count(dp, ns);

    printf("node-set %u\n", count);

    for (uint32_t i = 0; i < count; i++) {
	xo_eval_node_t id = data_ns_get(dp, ns, i);
	tnode_t *tnp = tree_node(dp, id);

	printf("  %u: %s %s \"", i + 1, kinds[tnp->tn_kind],
	       tnp->tn_name ?: "");
	print_flat(data_string_value(dp, id));
	printf("\"\n");
    }
}

static void
run_expr (xo_filter_t *xfp, xo_filter_data_t *dp, const char *expr)
{
    printf("expr: %s\n", expr);

    ssize_t root = xo_filter_parse_expr(NULL, xfp, expr);
    if (root < 0) {
	printf("parse failed\n");
	return;
    }

    struct xo_xparse_data_s *xdp = xo_filter_xparse_data(NULL, xfp);
    xo_eval_prog_t *prog = xo_eval_compile(xdp, root, dp, &data_ops);
    if (prog == NULL) {
	printf("compile failed\n");
	return;
    }

    xo_eval_ctx_t ctx = { 1, 1, 1 };
    int not_ns, missing;

    xo_eval_ns_t ns = xo_eval_prog_run_nodes(NULL, xfp, xdp, prog, &ctx,
					     &not_ns, &missing);
    if (ns) {
	print_nodes(dp, ns);
	data_ns_free(dp, ns);

    } else if (missing) {
	printf("missing\n");

    } else {
	int is_bool, bool_val;
	char *str = xo_eval_prog_run_ctx(NULL, xfp, xdp, prog, &ctx,
					 &is_bool, &bool_val, &missing);
	if (missing)
	    printf("missing\n");
	else if (is_bool)
	    printf("boolean %s\n", bool_val ? "true" : "false");
	else {
	    printf("value \"");
	    print_flat(str ?: "");
	    printf("\"\n");
	}

	free(str);
    }

    xo_eval_prog_destroy(prog);
}

static char *
read_all (FILE *in)
{
    char *buf = NULL;
    size_t len = 0;
    char chunk[4096];
    size_t rc;

    while ((rc = fread(chunk, 1, sizeof(chunk), in)) > 0) {
	buf = xrealloc(buf, len + rc + 1);
	memcpy(buf + len, chunk, rc);
	len += rc;
    }

    buf = xrealloc(buf, len + 1);
    buf[len] = '\0';
    return buf;
}

static const char *
get_arg (const char *arg, const char *what)
{
    if (arg == NULL)
	xo_errx(1, "missing %s argument", what);

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
	else if (xo_streq(argv[i], "input"))
	    opt_input = get_arg(argv[++i], "input file");
	else if (xo_streq(argv[i], "quiet"))
	    opt_quiet = 1;
	else
	    xo_errx(1, "unknown argument: '%s'", argv[i]);
    }

    if (opt_debug)
	xo_set_flags(NULL, XOF_DEBUG);

    FILE *in = opt_input ? fopen(opt_input, "r") : stdin;
    if (in == NULL)
	xo_err(1, "could not open file '%s'", opt_input);

    char *buf = read_all(in);
    if (in != stdin)
	fclose(in);

    char *exprs = strstr(buf, "\n%%\n");
    if (exprs == NULL)
	xo_errx(1, "missing '%%%%' line");

    *exprs = '\0';
    exprs += 4;

    xo_filter_data_t data;
    bzero(&data, sizeof(data));
    xo_buf_init(&data.xfd_value);
    tree_parse(&data, buf);

    xo_filter_t *xfp = xo_filter_create_with_data(NULL, &data, &data_ops);
    if (xfp == NULL)
	xo_errx(1, "could not create filter");

    char *line, *next;
    for (line = exprs; line && *line; line = next) {
	next = strchr(line, '\n');
	if (next)
	    *next++ = '\0';

	if (*line == '\0' || *line == '#')
	    continue;

	run_expr(xfp, &data, line);
    }

    if (!opt_quiet && data.xfd_sets_live != 0)
	fprintf(stderr, "xpath_03: %u node-sets not freed\n",
		data.xfd_sets_live);

    xo_filter_destroy_standalone(xfp);
    free(buf);

    return 0;
}
