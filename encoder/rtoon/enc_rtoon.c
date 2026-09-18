/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2015-2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * RTOON encoder: an indentation-based, comma-delimited output format
 * meant to be compact and cheap for an LLM to read and produce, loosely
 * inspired by TOON (see encoder/rtoon/rtoon-spec.md for the full wire
 * format and build/rtoon-plan.md for how this file maps libxo's
 * encoder callbacks onto that format).
 *
 * Output is built into our own buffer (rt_data) and only written to
 * fd 1 at XO_OP_FLUSH, exactly like the csv/fullpath encoders. This
 * also gives XO_OP_DEADEND a trivial implementation: every frame we
 * push records the buffer offset and indentation depth in effect right
 * before anything about that frame is written, so a DEADEND just pops
 * the top frame, truncates the buffer back to that offset, and
 * restores the depth -- discarding all tentative output (including
 * anything nested inside it) in one shot.
 *
 * A list's rows can be written sparse (one "- field value" line per
 * field, TOON's usual form) or dense (a tabular "{field,field}" header
 * followed by comma-separated rows). The choice is never inferred from
 * the data -- it's an explicit, upfront signal: either XOF_DENSE on
 * xo_open_list_hf(), or the "dense" field-format modifier (XFF_DENSE)
 * on some field of the list's first instance. If neither ever appears
 * before the first instance closes, the list is sparse.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xo.h"
#include "xo_encoder.h"

/*
 * A field name/key-flag pair, recorded on a dense list's currently
 * governing fields-line.
 */
typedef struct rtoon_field_s {
    char *rf_name;
    unsigned rf_flags;
} rtoon_field_t;
#define RF_KEY (1 << 0)

/*
 * A buffered, not-yet-written field of an in-progress instance. Used
 * both for sparse instances (buffered until the dash line is flushed)
 * and dense instances (buffered until the row is flushed).
 */
typedef struct rtoon_cell_s {
    char *rc_name;
    char *rc_value;
    unsigned rc_flags;
} rtoon_cell_t;
#define RC_KEY		(1 << 0) /* Field is a key */
#define RC_IS_STRING	(1 << 1) /* Came from XO_OP_STRING (typed-literal check) */
#define RC_RAW		(1 << 2) /* Value is pre-formatted; emit verbatim */

typedef enum {
    RTOON_LIST_UNDECIDED = 0,
    RTOON_LIST_SPARSE,
    RTOON_LIST_DENSE,
} rtoon_list_mode_t;

typedef enum {
    RTOON_FRAME_CONTAINER = 1,
    RTOON_FRAME_LIST,
    RTOON_FRAME_INSTANCE,
    RTOON_FRAME_FLATTEN,	/* A nested container flattened into a
				   dense row's columns; see rtoon-spec.md
				   Section 6.6 */
    RTOON_FRAME_ERROR,		/* A rejected nested list under a dense
				   instance; swallow content until close */
} rtoon_frame_type_t;

/*
 * One entry in our stack. We use a single flat struct (rather than a
 * union) for simplicity -- the extra unused fields per frame are
 * cheap and not worth the bookkeeping a union would add.
 */
typedef struct rtoon_frame_s {
    rtoon_frame_type_t rf_type;
    xo_off_t rf_save_off;	/* rt_data offset when this frame opened */
    int rf_save_depth;		/* rt_depth to restore on close/deadend */

    /* RTOON_FRAME_LIST */
    rtoon_list_mode_t rl_mode;
    rtoon_field_t *rl_fields;
    unsigned rl_fields_len;
    unsigned rl_fields_size;
    int rl_fields_dirty;	/* Need a new {...} line before next row */
    int rl_saw_instance;
    int rl_child_depth;	/* Depth of "-"/rows/fields-lines */

    /* RTOON_FRAME_INSTANCE */
    rtoon_cell_t *ri_cells;
    unsigned ri_cells_len;
    unsigned ri_cells_size;
    int ri_wrote_dash;		/* Sparse only: dash line already flushed */
    int ri_dash_depth;		/* == owning list's rl_child_depth */
    int ri_field_depth;	/* Continuation/nested-field depth */

    /* RTOON_FRAME_FLATTEN */
    char *rfl_prefix;		/* e.g. "via/" or "via/nested/" */
} rtoon_frame_t;

typedef struct rtoon_ll_val_s {
    char *val;
    int is_string;
} rtoon_ll_val_t;

typedef struct rtoon_private_s {
    xo_buffer_t rt_data;	/* Output, flushed to fd 1 at XO_OP_FLUSH */
    xo_buffer_t rt_scratch;	/* Scratch space for leaf-list rendering */

    rtoon_frame_t *rt_stack;
    int rt_stack_len;
    int rt_stack_size;

    int rt_depth;		/* Indent level of the next direct write */
    int rt_mid_line;		/* Next write continues the current line */

    rtoon_ll_val_t *rt_ll_vals;	/* Leaf-list values, buffered until close */
    unsigned rt_ll_count;
    unsigned rt_ll_size;

    uint32_t rt_flags;
} rtoon_private_t;

#define RTF_NO_VERSION	(1 << 0) /* User asked us to skip @version */
#define RTF_VERSION_DONE (1 << 1) /* @version already resolved/written */

/*
 * Frame stack
 */

static int
rtoon_push_idx (rtoon_private_t *priv, rtoon_frame_type_t type)
{
    if (priv->rt_stack_len >= priv->rt_stack_size) {
	int new_size = priv->rt_stack_size ? priv->rt_stack_size * 2 : 16;
	rtoon_frame_t *sp = xo_realloc(priv->rt_stack, new_size * sizeof(*sp));
	if (sp == NULL)
	    return priv->rt_stack_len > 0 ? priv->rt_stack_len - 1 : 0;

	priv->rt_stack = sp;
	priv->rt_stack_size = new_size;
    }

    int idx = priv->rt_stack_len++;
    rtoon_frame_t *fp = &priv->rt_stack[idx];

    bzero(fp, sizeof(*fp));
    fp->rf_type = type;
    fp->rf_save_off = xo_buf_offset(&priv->rt_data);
    fp->rf_save_depth = priv->rt_depth;

    return idx;
}

static int
rtoon_top_idx (rtoon_private_t *priv)
{
    return priv->rt_stack_len > 0 ? priv->rt_stack_len - 1 : -1;
}

/*
 * Walk up past any FLATTEN frames to find the innermost accumulating
 * instance. Returns -1 if we're not inside one (top-level, or directly
 * inside an ordinary container).
 */
static int
rtoon_cur_instance_idx (rtoon_private_t *priv)
{
    for (int i = priv->rt_stack_len - 1; i >= 0; i--) {
	rtoon_frame_type_t t = priv->rt_stack[i].rf_type;

	if (t == RTOON_FRAME_FLATTEN)
	    continue;
	if (t == RTOON_FRAME_INSTANCE)
	    return i;
	return -1;
    }

    return -1;
}

/*
 * Concatenate every FLATTEN frame's prefix between an instance and the
 * top of the stack. Returns NULL (not an empty string) if there are
 * none. Caller frees the result.
 */
static char *
rtoon_flatten_prefix (rtoon_private_t *priv, int inst_idx)
{
    size_t len = 0;
    int i;

    for (i = inst_idx + 1; i < priv->rt_stack_len; i++)
	if (priv->rt_stack[i].rf_type == RTOON_FRAME_FLATTEN)
	    len += strlen(priv->rt_stack[i].rfl_prefix);

    if (len == 0)
	return NULL;

    char *buf = xo_realloc(NULL, len + 1);
    buf[0] = '\0';

    for (i = inst_idx + 1; i < priv->rt_stack_len; i++)
	if (priv->rt_stack[i].rf_type == RTOON_FRAME_FLATTEN)
	    strcat(buf, priv->rt_stack[i].rfl_prefix);

    return buf;
}

/*
 * Release memory owned by a frame. Used both by the ordinary close
 * handlers and by XO_OP_DEADEND's universal rollback.
 */
static void
rtoon_free_frame (rtoon_frame_t *fp)
{
    unsigned i;

    switch (fp->rf_type) {
    case RTOON_FRAME_INSTANCE:
	for (i = 0; i < fp->ri_cells_len; i++) {
	    xo_free(fp->ri_cells[i].rc_name);
	    xo_free(fp->ri_cells[i].rc_value);
	}
	if (fp->ri_cells)
	    xo_free(fp->ri_cells);
	break;

    case RTOON_FRAME_LIST:
	for (i = 0; i < fp->rl_fields_len; i++)
	    xo_free(fp->rl_fields[i].rf_name);
	if (fp->rl_fields)
	    xo_free(fp->rl_fields);
	break;

    case RTOON_FRAME_FLATTEN:
	if (fp->rfl_prefix)
	    xo_free(fp->rfl_prefix);
	break;

    default:
	break;
    }
}

/*
 * Quoting and low-level writing
 */

static int
rtoon_is_number (const char *value)
{
    const char *cp = value;

    if (*cp == '-')
	cp++;
    if (!isdigit((unsigned char) *cp))
	return 0;
    if (cp[0] == '0' && isdigit((unsigned char) cp[1]))
	return 0;		/* forbidden leading zero */

    while (isdigit((unsigned char) *cp))
	cp++;

    if (*cp == '.') {
	cp++;
	if (!isdigit((unsigned char) *cp))
	    return 0;
	while (isdigit((unsigned char) *cp))
	    cp++;
    }

    if (*cp == 'e' || *cp == 'E') {
	cp++;
	if (*cp == '+' || *cp == '-')
	    cp++;
	if (!isdigit((unsigned char) *cp))
	    return 0;
	while (isdigit((unsigned char) *cp))
	    cp++;
    }

    return (*cp == '\0');
}

static int
rtoon_is_typed_literal (const char *value)
{
    return xo_streq(value, "true") || xo_streq(value, "false")
	|| xo_streq(value, "null") || rtoon_is_number(value);
}

static int
rtoon_key_needs_quote (const char *name)
{
    if (name[0] == '\0')
	return 1;
    if (name[0] == '{' || name[0] == '-')
	return 1;

    for (const char *cp = name; *cp; cp++) {
	unsigned char ch = (unsigned char) *cp;
	if (ch == '=' || ch == ',' || ch == '"' || ch == '\\' || isspace(ch))
	    return 1;
    }

    return 0;
}

/*
 * in_delimited: value sits in a comma-delimited context (leaf-list
 *   element or dense row cell) -- changes how an empty value is judged.
 * is_string: value came from XO_OP_STRING, so the typed-literal
 *   collision check applies; XO_OP_CONTENT values are never checked.
 * is_last: this is a leaf-list's final element, before the mandatory
 *   trailing comma -- an empty one there needs quoting even though
 *   other delimited-context empties don't.
 *
 * We quote a comma unconditionally, even outside a delimited context:
 * rtoon-spec.md Section 2.1 reads an unquoted comma anywhere after a
 * field name as the start of a leaf-list, so an ordinary data-value
 * containing one has to be quoted or it will be misread.
 */
static int
rtoon_value_needs_quote (const char *value, int in_delimited, int is_string,
			  int is_last)
{
    if (value[0] == '{')
	return 1;

    for (const char *cp = value; *cp; cp++)
	if (*cp == ',' || *cp == '"' || *cp == '\\')
	    return 1;

    if (!in_delimited && value[0] == '\0')
	return 1;
    if (in_delimited && is_last && value[0] == '\0')
	return 1;
    if (is_string && rtoon_is_typed_literal(value))
	return 1;

    return 0;
}

static void
rtoon_write_escaped (xo_buffer_t *out, const char *value)
{
    for (const char *cp = value; *cp; cp++) {
	if (*cp == '"' || *cp == '\\')
	    xo_buf_append(out, "\\", 1);
	xo_buf_append(out, cp, 1);
    }
}

static void
rtoon_write_key (xo_buffer_t *out, const char *name)
{
    const char *nm = name ? name : "";

    if (rtoon_key_needs_quote(nm)) {
	xo_buf_append(out, "\"", 1);
	rtoon_write_escaped(out, nm);
	xo_buf_append(out, "\"", 1);
    } else {
	xo_buf_append_str(out, nm);
    }
}

/*
 * XO_OP_CONTENT values are libxo's own pre-formatted, non-string
 * content (numbers, booleans, ...); we trust them and emit them bare,
 * exactly as xo_format_value_encoder()'s CONTENT-vs-STRING dispatch
 * already intends. Only XO_OP_STRING values go through quoting.
 */
static void
rtoon_write_value (xo_buffer_t *out, const char *value, int in_delimited,
		    int is_string, int is_last)
{
    const char *v = value ? value : "";

    if (!is_string) {
	xo_buf_append_str(out, v);
	return;
    }

    if (rtoon_value_needs_quote(v, in_delimited, is_string, is_last)) {
	xo_buf_append(out, "\"", 1);
	rtoon_write_escaped(out, v);
	xo_buf_append(out, "\"", 1);
    } else {
	xo_buf_append_str(out, v);
    }
}

static void
rtoon_indent (xo_buffer_t *out, int depth)
{
    for (int i = 0; i < depth; i++)
	xo_buf_append(out, "  ", 2);
}

/*
 * The single choke point for starting a new (or continuing a
 * mid-line) piece of output. Also where we lazily resolve whether to
 * emit "@version 1.0.0" -- deferred to here (rather than XO_OP_CREATE)
 * so that XO_OP_OPTIONS (which arrives after CREATE but before any
 * real content) gets a chance to set RTF_NO_VERSION first.
 */
static void
rtoon_write_line_prefix (rtoon_private_t *priv, int depth)
{
    if (!(priv->rt_flags & RTF_VERSION_DONE)) {
	priv->rt_flags |= RTF_VERSION_DONE;
	if (!(priv->rt_flags & RTF_NO_VERSION))
	    xo_buf_append_str(&priv->rt_data, "@version 1.0.0\n");
    }

    if (priv->rt_mid_line)
	priv->rt_mid_line = 0;
    else
	rtoon_indent(&priv->rt_data, depth);
}

/* ------------------------------------------------------------------ */
/* Dense-list field tracking and instance-cell buffering */

static rtoon_field_t *
rtoon_field_find (rtoon_frame_t *list, const char *name)
{
    for (unsigned i = 0; i < list->rl_fields_len; i++)
	if (xo_streq(list->rl_fields[i].rf_name, name))
	    return &list->rl_fields[i];

    return NULL;
}

static void
rtoon_field_widen (rtoon_frame_t *list, const char *name, int is_key)
{
    rtoon_field_t *fp = rtoon_field_find(list, name);
    if (fp != NULL) {
	if (is_key)
	    fp->rf_flags |= RF_KEY;
	return;
    }

    if (list->rl_fields_len >= list->rl_fields_size) {
	unsigned new_size = list->rl_fields_size ? list->rl_fields_size * 2 : 8;
	rtoon_field_t *np = xo_realloc(list->rl_fields, new_size * sizeof(*np));
	if (np == NULL)
	    return;
	list->rl_fields = np;
	list->rl_fields_size = new_size;
    }

    fp = &list->rl_fields[list->rl_fields_len++];
    fp->rf_name = xo_realloc(NULL, strlen(name) + 1);
    strcpy(fp->rf_name, name);
    fp->rf_flags = is_key ? RF_KEY : 0;

    list->rl_fields_dirty = 1;
}

static void
rtoon_cell_push (rtoon_frame_t *inst, const char *name, const char *value,
		  unsigned flags)
{
    if (inst->ri_cells_len >= inst->ri_cells_size) {
	unsigned new_size = inst->ri_cells_size ? inst->ri_cells_size * 2 : 8;
	rtoon_cell_t *cp = xo_realloc(inst->ri_cells, new_size * sizeof(*cp));
	if (cp == NULL)
	    return;
	inst->ri_cells = cp;
	inst->ri_cells_size = new_size;
    }

    rtoon_cell_t *cell = &inst->ri_cells[inst->ri_cells_len++];
    const char *v = value ? value : "";

    cell->rc_name = xo_realloc(NULL, strlen(name) + 1);
    strcpy(cell->rc_name, name);
    cell->rc_value = xo_realloc(NULL, strlen(v) + 1);
    strcpy(cell->rc_value, v);
    cell->rc_flags = flags;
}

static void
rtoon_write_cell_value (xo_buffer_t *out, rtoon_cell_t *cell, int in_delimited)
{
    if (cell->rc_flags & RC_RAW)
	xo_buf_append_str(out, cell->rc_value);
    else
	rtoon_write_value(out, cell->rc_value, in_delimited,
			   (cell->rc_flags & RC_IS_STRING) != 0, 0);
}

/*
 * Flush a sparse instance's buffered fields as a "- " dash line
 * followed by continuation lines. Called either when the instance
 * closes normally, or early, when nested content (a container/list
 * field) forces the dash line out before the nested content can be
 * written.
 *
 * If nothing was ever buffered (the very first field is itself the
 * nested content that triggered this flush), we still owe the list a
 * "- " marker -- write it with no trailing newline and leave
 * rt_mid_line set, so the caller's very next write (the nested
 * container/list's own name) continues right after it on the same
 * line, per rtoon-spec.md Section 4.2.
 */
static void
rtoon_flush_sparse_dash (rtoon_private_t *priv, rtoon_frame_t *inst)
{
    xo_buffer_t *out = &priv->rt_data;

    if (inst->ri_cells_len == 0) {
	rtoon_write_line_prefix(priv, inst->ri_dash_depth);
	xo_buf_append_str(out, "- ");
	priv->rt_mid_line = 1;
	inst->ri_wrote_dash = 1;
	priv->rt_depth = inst->ri_field_depth;
	return;
    }

    for (unsigned i = 0; i < inst->ri_cells_len; i++) {
	rtoon_cell_t *cell = &inst->ri_cells[i];
	int depth = (i == 0) ? inst->ri_dash_depth : inst->ri_field_depth;

	rtoon_write_line_prefix(priv, depth);
	if (i == 0)
	    xo_buf_append_str(out, "- ");

	rtoon_write_key(out, cell->rc_name);
	xo_buf_append(out, (cell->rc_flags & RC_KEY) ? "=" : " ", 1);
	rtoon_write_cell_value(out, cell, 0);
	xo_buf_append(out, "\n", 1);

	xo_free(cell->rc_name);
	xo_free(cell->rc_value);
    }

    inst->ri_cells_len = 0;
    inst->ri_wrote_dash = 1;
    priv->rt_depth = inst->ri_field_depth;
}

static void
rtoon_emit_fields_line (rtoon_private_t *priv, rtoon_frame_t *list)
{
    xo_buffer_t *out = &priv->rt_data;

    rtoon_write_line_prefix(priv, list->rl_child_depth);
    xo_buf_append(out, "{", 1);

    for (unsigned i = 0; i < list->rl_fields_len; i++) {
	if (i != 0)
	    xo_buf_append(out, ",", 1);
	rtoon_write_key(out, list->rl_fields[i].rf_name);
	if (list->rl_fields[i].rf_flags & RF_KEY)
	    xo_buf_append(out, "=", 1);
    }

    xo_buf_append(out, "}\n", 2);
    list->rl_fields_dirty = 0;
}

static void
rtoon_flush_dense_row (rtoon_private_t *priv, rtoon_frame_t *list,
			rtoon_frame_t *inst)
{
    if (list->rl_fields_dirty)
	rtoon_emit_fields_line(priv, list);

    xo_buffer_t *out = &priv->rt_data;
    rtoon_write_line_prefix(priv, list->rl_child_depth);

    for (unsigned i = 0; i < list->rl_fields_len; i++) {
	if (i != 0)
	    xo_buf_append(out, ",", 1);

	rtoon_cell_t *cell = NULL;
	for (unsigned j = 0; j < inst->ri_cells_len; j++) {
	    if (xo_streq(inst->ri_cells[j].rc_name,
			 list->rl_fields[i].rf_name)) {
		cell = &inst->ri_cells[j];
		break;
	    }
	}

	if (cell != NULL)
	    rtoon_write_cell_value(out, cell, 1);
	/* else: field not supplied for this row -> empty cell */
    }

    xo_buf_append(out, "\n", 1);

    for (unsigned j = 0; j < inst->ri_cells_len; j++) {
	xo_free(inst->ri_cells[j].rc_name);
	xo_free(inst->ri_cells[j].rc_value);
    }
    inst->ri_cells_len = 0;
}

/*
 * Handle one field (data-value/key-value, or a leaf-list's
 * already-rendered joined text when raw is set). Top-level and
 * plain-container fields are written immediately; fields inside a
 * list instance are routed by the owning list's dense/sparse/
 * undecided mode.
 */
static void
rtoon_emit_field (rtoon_private_t *priv, const char *name, const char *value,
		   xo_xff_flags_t flags, int is_string, int raw)
{
    xo_buffer_t *out = &priv->rt_data;
    int inst_idx = rtoon_cur_instance_idx(priv);

    if (inst_idx < 0) {
	rtoon_write_line_prefix(priv, priv->rt_depth);
	rtoon_write_key(out, name);
	xo_buf_append(out, (flags & XFF_KEY) ? "=" : " ", 1);
	if (raw)
	    xo_buf_append_str(out, value ? value : "");
	else
	    rtoon_write_value(out, value, 0, is_string, 0);
	xo_buf_append(out, "\n", 1);
	return;
    }

    int list_idx = inst_idx - 1;	/* Always the frame directly below */
    rtoon_frame_t *inst = &priv->rt_stack[inst_idx];
    rtoon_frame_t *list = &priv->rt_stack[list_idx];

    char *prefix = rtoon_flatten_prefix(priv, inst_idx);
    char *full_name;
    if (prefix != NULL) {
	size_t plen = strlen(prefix), nlen = strlen(name);
	full_name = xo_realloc(NULL, plen + nlen + 1);
	memcpy(full_name, prefix, plen);
	memcpy(full_name + plen, name, nlen + 1);
	xo_free(prefix);
    } else {
	full_name = xo_realloc(NULL, strlen(name) + 1);
	strcpy(full_name, name);
    }

    unsigned cflags = ((flags & XFF_KEY) ? RC_KEY : 0)
	| (is_string ? RC_IS_STRING : 0)
	| (raw ? RC_RAW : 0);

    if (list->rl_mode == RTOON_LIST_DENSE) {
	rtoon_field_widen(list, full_name, (flags & XFF_KEY) != 0);
	rtoon_cell_push(inst, full_name, value, cflags);

    } else if (list->rl_mode == RTOON_LIST_UNDECIDED) {
	if (flags & XFF_DENSE) {
	    list->rl_mode = RTOON_LIST_DENSE;
	    for (unsigned i = 0; i < inst->ri_cells_len; i++)
		rtoon_field_widen(list, inst->ri_cells[i].rc_name,
				   (inst->ri_cells[i].rc_flags & RC_KEY) != 0);
	    rtoon_field_widen(list, full_name, (flags & XFF_KEY) != 0);
	}
	rtoon_cell_push(inst, full_name, value, cflags);

    } else { /* RTOON_LIST_SPARSE */
	if (inst->ri_wrote_dash) {
	    rtoon_write_line_prefix(priv, priv->rt_depth);
	    rtoon_write_key(out, full_name);
	    xo_buf_append(out, (flags & XFF_KEY) ? "=" : " ", 1);
	    if (raw)
		xo_buf_append_str(out, value ? value : "");
	    else
		rtoon_write_value(out, value, 0, is_string, 0);
	    xo_buf_append(out, "\n", 1);
	} else {
	    rtoon_cell_push(inst, full_name, value, cflags);
	}
    }

    xo_free(full_name);
}

/*
 * Leaf-lists
 */

static void
rtoon_open_leaf_list (rtoon_private_t *priv)
{
    for (unsigned i = 0; i < priv->rt_ll_count; i++)
	xo_free(priv->rt_ll_vals[i].val);
    priv->rt_ll_count = 0;
}

static void
rtoon_ll_add (rtoon_private_t *priv, const char *value, int is_string)
{
    if (priv->rt_ll_count >= priv->rt_ll_size) {
	unsigned new_size = priv->rt_ll_size ? priv->rt_ll_size * 2 : 8;
	rtoon_ll_val_t *np = xo_realloc(priv->rt_ll_vals,
					new_size * sizeof(*np));
	if (np == NULL)
	    return;
	priv->rt_ll_vals = np;
	priv->rt_ll_size = new_size;
    }

    rtoon_ll_val_t *vp = &priv->rt_ll_vals[priv->rt_ll_count++];
    const char *v = value ? value : "";

    vp->val = xo_realloc(NULL, strlen(v) + 1);
    strcpy(vp->val, v);
    vp->is_string = is_string;
}

/*
 * Render the buffered leaf-list values as a single, already-quoted,
 * comma-delimited string (with the mandatory trailing comma of
 * rtoon-spec.md Section 5), then hand it to rtoon_emit_field() as a
 * raw (pre-formatted) field value -- reusing exactly the same
 * top-level/dense/undecided/sparse routing an ordinary field uses.
 */
static void
rtoon_close_leaf_list (rtoon_private_t *priv, const char *name,
			xo_xff_flags_t flags)
{
    xo_buffer_t *scratch = &priv->rt_scratch;
    xo_buf_reset(scratch);

    for (unsigned i = 0; i < priv->rt_ll_count; i++) {
	if (i != 0)
	    xo_buf_append(scratch, ",", 1);
	rtoon_write_value(scratch, priv->rt_ll_vals[i].val, 1,
			   priv->rt_ll_vals[i].is_string,
			   (i + 1 == priv->rt_ll_count));
    }
    xo_buf_append(scratch, ",", 1);
    xo_buf_force_nul(scratch);

    rtoon_emit_field(priv, name, xo_buf_data(scratch, 0), flags, 0, 1);

    for (unsigned i = 0; i < priv->rt_ll_count; i++)
	xo_free(priv->rt_ll_vals[i].val);
    priv->rt_ll_count = 0;
}

/*
 * Structural operations: containers, lists, instances
 */

static void
rtoon_open_container (rtoon_private_t *priv, const char *name)
{
    int inst_idx = rtoon_cur_instance_idx(priv);

    if (inst_idx >= 0) {
	int list_idx = inst_idx - 1;

	if (priv->rt_stack[list_idx].rl_mode == RTOON_LIST_UNDECIDED)
	    priv->rt_stack[list_idx].rl_mode = RTOON_LIST_SPARSE;

	if (priv->rt_stack[list_idx].rl_mode == RTOON_LIST_DENSE) {
	    /*
	     * Flatten: fold this nested container's leaves into the
	     * dense row as prefixed columns (rtoon-spec.md Section 6.6)
	     */
	    int fidx = rtoon_push_idx(priv, RTOON_FRAME_FLATTEN);
	    rtoon_frame_t *fp = &priv->rt_stack[fidx];
	    size_t len = strlen(name);

	    fp->rfl_prefix = xo_realloc(NULL, len + 2);
	    memcpy(fp->rfl_prefix, name, len);
	    fp->rfl_prefix[len] = '/';
	    fp->rfl_prefix[len + 1] = '\0';
	    return;
	}

	if (!priv->rt_stack[inst_idx].ri_wrote_dash)
	    rtoon_flush_sparse_dash(priv, &priv->rt_stack[inst_idx]);
    }

    rtoon_push_idx(priv, RTOON_FRAME_CONTAINER);

    rtoon_write_line_prefix(priv, priv->rt_depth);
    rtoon_write_key(&priv->rt_data, name);
    xo_buf_append(&priv->rt_data, "\n", 1);

    priv->rt_depth += 1;
}

static void
rtoon_close_container (rtoon_private_t *priv)
{
    int idx = rtoon_top_idx(priv);
    if (idx < 0)
	return;

    rtoon_frame_t *fp = &priv->rt_stack[idx];

    if (fp->rf_type == RTOON_FRAME_FLATTEN
		|| fp->rf_type == RTOON_FRAME_ERROR) {
	rtoon_free_frame(fp);
	priv->rt_stack_len -= 1;
	return;
    }

    rtoon_free_frame(fp);
    priv->rt_depth = fp->rf_save_depth;
    priv->rt_stack_len -= 1;
}

static void
rtoon_open_list (xo_handle_t *xop, rtoon_private_t *priv, const char *name,
		  xo_xff_flags_t flags)
{
    int inst_idx = rtoon_cur_instance_idx(priv);

    if (inst_idx >= 0) {
	int list_idx = inst_idx - 1;

	if (priv->rt_stack[list_idx].rl_mode == RTOON_LIST_UNDECIDED)
	    priv->rt_stack[list_idx].rl_mode = RTOON_LIST_SPARSE;

	if (priv->rt_stack[list_idx].rl_mode == RTOON_LIST_DENSE) {
	    /*
	     * A dense row's cells are plain scalars; a nested list
	     * can't be flattened (its cardinality varies row to row)
	     * and we don't attempt a mid-stream dense->sparse
	     * downgrade -- see rtoon-spec.md Section 6.5.
	     */
	    xo_failure(xop, "rtoon: nested list not permitted inside a "
		       "dense list instance ('%s')", name ?: "");
	    rtoon_push_idx(priv, RTOON_FRAME_ERROR);
	    return;
	}

	if (!priv->rt_stack[inst_idx].ri_wrote_dash)
	    rtoon_flush_sparse_dash(priv, &priv->rt_stack[inst_idx]);
    }

    int lidx = rtoon_push_idx(priv, RTOON_FRAME_LIST);

    rtoon_write_line_prefix(priv, priv->rt_depth);
    rtoon_write_key(&priv->rt_data, name);
    xo_buf_append(&priv->rt_data, "\n", 1);

    priv->rt_depth += 1;

    rtoon_frame_t *fp = &priv->rt_stack[lidx];
    fp->rl_child_depth = priv->rt_depth;
    fp->rl_mode = (flags & XOF_DENSE) ? RTOON_LIST_DENSE : RTOON_LIST_UNDECIDED;
}

static void
rtoon_close_list (rtoon_private_t *priv)
{
    int idx = rtoon_top_idx(priv);
    if (idx < 0)
	return;

    rtoon_frame_t *fp = &priv->rt_stack[idx];

    if (fp->rf_type == RTOON_FRAME_ERROR) {
	priv->rt_stack_len -= 1;
	return;
    }

    if (!fp->rl_saw_instance) {
	rtoon_write_line_prefix(priv, fp->rl_child_depth);
	xo_buf_append_str(&priv->rt_data, "-\n");
    }

    rtoon_free_frame(fp);
    priv->rt_depth = fp->rf_save_depth;
    priv->rt_stack_len -= 1;
}

static void
rtoon_open_instance (rtoon_private_t *priv)
{
    int list_idx = rtoon_top_idx(priv);
    if (list_idx < 0)
	return;

    int depth = priv->rt_stack[list_idx].rl_child_depth;
    int inst_idx = rtoon_push_idx(priv, RTOON_FRAME_INSTANCE);

    rtoon_frame_t *fp = &priv->rt_stack[inst_idx];
    fp->ri_dash_depth = depth;
    fp->ri_field_depth = depth + 1;

    priv->rt_stack[list_idx].rl_saw_instance = 1;
}

static void
rtoon_close_instance (rtoon_private_t *priv)
{
    int inst_idx = rtoon_top_idx(priv);
    if (inst_idx < 0)
	return;

    int list_idx = inst_idx - 1;
    rtoon_frame_t *inst = &priv->rt_stack[inst_idx];
    rtoon_frame_t *list = &priv->rt_stack[list_idx];

    if (list->rl_mode == RTOON_LIST_UNDECIDED)
	list->rl_mode = RTOON_LIST_SPARSE;

    if (list->rl_mode == RTOON_LIST_DENSE) {
	rtoon_flush_dense_row(priv, list, inst);

    } else if (!inst->ri_wrote_dash) {
	if (inst->ri_cells_len == 0) {
	    /*
	     * Pathological: an instance that never received a single
	     * field. libxo's own emit calls don't produce this (every
	     * instance carries at least a key), but keep this well
	     * formed. We encode it as "- \"\"" -- an instance holding
	     * a single empty string -- rather than a bare "-": a bare
	     * dash reads as a truncated/malformed line, while "- \"\""
	     * is an unambiguous, self-describing empty element.
	     * See rtoon-spec.md Section 4.2.
	     */
	    rtoon_write_line_prefix(priv, inst->ri_dash_depth);
	    xo_buf_append_str(&priv->rt_data, "- ");
	    rtoon_write_value(&priv->rt_data, "", 0, 1, 0);
	    xo_buf_append(&priv->rt_data, "\n", 1);

	    priv->rt_depth = inst->ri_field_depth;
	} else {
	    rtoon_flush_sparse_dash(priv, inst);
	}
    }

    rtoon_free_frame(inst);
    priv->rt_depth = list->rl_child_depth;
    priv->rt_stack_len -= 1;
}

/*
 * Universal rollback: pop the top frame, discard any output written
 * since it opened (everything at or after its saved buffer offset,
 * nested content included), and restore our indentation depth.
 */
static void
rtoon_deadend (rtoon_private_t *priv)
{
    int idx = rtoon_top_idx(priv);
    if (idx < 0)
	return;

    rtoon_frame_t *fp = &priv->rt_stack[idx];

    xo_buf_set_len(&priv->rt_data, fp->rf_save_off);
    priv->rt_depth = fp->rf_save_depth;
    priv->rt_mid_line = 0;

    rtoon_free_frame(fp);
    priv->rt_stack_len -= 1;
}

/*
 * Setup, options, teardown
 */

static int
rtoon_create (xo_handle_t *xop)
{
    rtoon_private_t *priv = xo_realloc(NULL, sizeof(*priv));
    if (priv == NULL)
	return -1;

    bzero(priv, sizeof(*priv));
    xo_buf_init(&priv->rt_data);
    xo_buf_init(&priv->rt_scratch);

    xo_set_private(xop, priv);

    return 0;
}

static void
rtoon_destroy (rtoon_private_t *priv)
{
    for (int i = 0; i < priv->rt_stack_len; i++)
	rtoon_free_frame(&priv->rt_stack[i]);
    if (priv->rt_stack)
	xo_free(priv->rt_stack);

    for (unsigned i = 0; i < priv->rt_ll_count; i++)
	xo_free(priv->rt_ll_vals[i].val);
    if (priv->rt_ll_vals)
	xo_free(priv->rt_ll_vals);

    xo_buf_cleanup(&priv->rt_data);
    xo_buf_cleanup(&priv->rt_scratch);

    xo_free(priv);
}

/*
 * Extract option values. The format is:
 *    -libxo encoder=rtoon:kw=val:kw=val
 *    -libxo encoder=rtoon+kw=val+kw=val
 */
static int
rtoon_options (xo_handle_t *xop, rtoon_private_t *priv,
	       const char *raw_opts, char opts_char)
{
    ssize_t len = strlen(raw_opts);
    char *options = alloca(len + 1);
    memcpy(options, raw_opts, len);
    options[len] = '\0';

    char *cp, *ep, *np, *vp;
    for (cp = options, ep = options + len + 1; cp && cp < ep; cp = np) {
	np = strchr(cp, opts_char);
	if (np)
	    *np++ = '\0';

	vp = strchr(cp, '=');
	if (vp)
	    *vp++ = '\0';

	if (*cp == '\0') {
	    continue;
	} else if (xo_streq(cp, "no-version")) {
	    priv->rt_flags |= RTF_NO_VERSION;
	} else {
	    xo_warn_hc(xop, -1, "unknown encoder option value: '%s'", cp);
	    return -1;
	}
    }

    return 0;
}

/*
 * Dispatch
 */

static int
rtoon_handler (XO_ENCODER_HANDLER_ARGS)
{
    rtoon_private_t *priv = private;

    if (getenv("RTOON_DEBUG"))
	fprintf(stderr, "rtoon: op=%d name=%s value=%s flags=%llx\n",
		op, name ?: "-", value ?: "-", (unsigned long long) flags);

    if (priv == NULL && op != XO_OP_CREATE)
	return -1;

    if (priv != NULL) {
	int top_idx = rtoon_top_idx(priv);
	if (top_idx >= 0
	    	&& priv->rt_stack[top_idx].rf_type == RTOON_FRAME_ERROR) {
	    /*
	     * We're inside a rejected (dense-nested-list) scope; swallow
	     * everything except the structural balance needed to find
	     * our way back out.
	     */
	    switch (op) {
	    case XO_OP_OPEN_CONTAINER:
	    case XO_OP_OPEN_LIST:
	    case XO_OP_OPEN_INSTANCE:
		rtoon_push_idx(priv, RTOON_FRAME_ERROR);
		break;

	    case XO_OP_CLOSE_CONTAINER:
	    case XO_OP_CLOSE_LIST:
	    case XO_OP_CLOSE_INSTANCE:
	    case XO_OP_DEADEND:
		priv->rt_stack_len -= 1;
		break;

	    default:
		break;
	    }
	    return 0;
	}
    }

    switch (op) {
    case XO_OP_CREATE:
	return rtoon_create(xop);

    case XO_OP_OPTIONS:
	return rtoon_options(xop, priv, value, ':');

    case XO_OP_OPTIONS_PLUS:
	return rtoon_options(xop, priv, value, '+');

    case XO_OP_OPEN_CONTAINER:
	rtoon_open_container(priv, name);
	break;

    case XO_OP_CLOSE_CONTAINER:
	rtoon_close_container(priv);
	break;

    case XO_OP_OPEN_LIST:
	rtoon_open_list(xop, priv, name, flags);
	break;

    case XO_OP_CLOSE_LIST:
	rtoon_close_list(priv);
	break;

    case XO_OP_OPEN_INSTANCE:
	rtoon_open_instance(priv);
	break;

    case XO_OP_CLOSE_INSTANCE:
	rtoon_close_instance(priv);
	break;

    case XO_OP_OPEN_LEAF_LIST:
	rtoon_open_leaf_list(priv);
	break;

    case XO_OP_CLOSE_LEAF_LIST:
	rtoon_close_leaf_list(priv, name, flags);
	break;

    case XO_OP_STRING:
    case XO_OP_CONTENT:
	if (flags & XFF_LEAF_LIST)
	    rtoon_ll_add(priv, value, op == XO_OP_STRING);
	else
	    rtoon_emit_field(priv, name, value, flags, op == XO_OP_STRING, 0);
	break;

    case XO_OP_DEADEND:
	rtoon_deadend(priv);
	break;

    case XO_OP_ATTRIBUTE:
	break;			/* Attributes are dropped */

    case XO_OP_VERSION:
	break;

    case XO_OP_FINISH:
	break; /* xo_finish_h() always calls xo_flush_h() next */

    case XO_OP_FLUSH: {
	xo_buffer_t *out = &priv->rt_data;
	ssize_t left = out->xb_curp - out->xb_bufp;
	ssize_t rc = (left > 0) ? write(1, out->xb_bufp, left) : 0;

	xo_buf_reset(out);
	if (rc < 0)
	    return -1;
	break;
    }

    case XO_OP_DESTROY:
	rtoon_destroy(priv);
	break;

    default:
	break;
    }

    return 0;
}

/*
 * Callback when our encoder is loaded.
 */
int
xo_encoder_library_init (XO_ENCODER_INIT_ARGS)
{
    /* Caller's version is older than we need; report ours and fail */
    if (arg->xei_version < XO_ENCODER_VERSION) {
	arg->xei_version = XO_ENCODER_VERSION;
	return -1;
    }

    arg->xei_handler = rtoon_handler;
    arg->xei_version = XO_ENCODER_VERSION;
    arg->xei_flags |= XEIF_FILTER_AWARE | XEIF_FILTER_NOTIFY_DEADEND;

    return 0;
}
