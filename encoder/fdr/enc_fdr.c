/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2015-2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, August 2015
 */

#include <ctype.h>

#include "xo.h"
#include "xo_encoder.h"

/*
 * Make a copy of a value for a debug comment, which must stay on
 * one line
 */
static void
fdr_cleanup (char *buf, const char *value)
{
    char *bp;
    const char *cp;

    for (bp = buf, cp = value; cp && *cp; cp++, bp++) {
	unsigned char ch = *cp;
	if (ch >= 0x20 && ch != 0x7f)
	    *bp = *cp;
	else
	    *bp = ' ';
    }

    *bp = '\0';
}

/*
 * Does a value need quoting to survive tests/xpath/xpath_01.c's
 * reader?  That reader trims spaces from both ends of a value, skips
 * a line with no value, and decodes a value in double quotes.
 */
static int
fdr_needs_quotes (const char *value)
{
    size_t len = strlen(value);

    if (len == 0 || value[0] == '"' || isspace((unsigned char) value[0])
	    || isspace((unsigned char) value[len - 1]))
	return 1;

    for (const char *cp = value; *cp; cp++) {
	unsigned char ch = *cp;
	if (ch < 0x20 || ch == 0x7f)
	    return 1;
    }

    return 0;
}

/*
 * Print a value, in double quotes, with escapes, when it needs them
 */
static void
fdr_print_value (const char *value)
{
    if (!fdr_needs_quotes(value)) {
	fputs(value, stdout);
	return;
    }

    putchar('"');

    for (const char *cp = value; *cp; cp++) {
	unsigned char ch = *cp;

	switch (ch) {
	case '"':
	case '\\':
	    printf("\\%c", ch);
	    break;
	case '\n':
	    fputs("\\n", stdout);
	    break;
	case '\r':
	    fputs("\\r", stdout);
	    break;
	case '\t':
	    fputs("\\t", stdout);
	    break;
	default:
	    if (ch < 0x20 || ch == 0x7f)
		printf("\\x%02x", ch);
	    else
		putchar(ch);
	}
    }

    putchar('"');
}

static int
fdr_handler (XO_ENCODER_HANDLER_ARGS)
{
    flags &= ~XOF_UTF8; /* Skip this flag, since it depends on terminal */

    int len = value ? strlen(value) + 1 : 1;
    char *clean = alloca(len);
    fdr_cleanup(clean, value);

    if (xo_get_flags(xop) & XOF_DEBUG)
	printf("# op %s: [%s] [%s] [%#llx]\n", xo_encoder_op_name(op),
	       name ?: "", clean, (unsigned long long) flags);

    const char *leader = NULL;

    switch (op) {

    case XO_OP_CREATE: /* Called when the handle is init'd */
	printf("r\n");
	break;

    case XO_OP_OPEN_CONTAINER: /* XXX */
	leader = "+";
	break;

    case XO_OP_OPEN_LIST: /* XXX */
	leader = "{";
	break;

    case XO_OP_OPEN_INSTANCE: /* XXX */
	leader = "<";
	break;

    case XO_OP_CLOSE_CONTAINER: /* XXX */
	leader = "-";
	break;

    case XO_OP_CLOSE_LIST: /* XXX */
	leader = "}";
	break;

    case XO_OP_CLOSE_INSTANCE: /* XXX */
	leader = ">";
	break;

    case XO_OP_OPEN_LEAF_LIST: /* XXX */
	leader = "[";
	break;

    case XO_OP_CLOSE_LEAF_LIST: /* XXX */
	leader = "]";
	break;

    case XO_OP_STRING: /* Quoted UTF-8 string */
    case XO_OP_CONTENT: /* Other content */
	printf("%s %s ", (flags & XFF_KEY) ? "$" : "=", name);
	fdr_print_value(value ?: "");
	putchar('\n');
	break;

    case XO_OP_ATTRIBUTE: /* Attribute name/value */
	leader = "@";
	break;

    case XO_OP_FILTER: /* Filter text, which we record */
	if (value)
	    printf("?%s\n", value);
	break;
    }

    if (leader && name) {
	printf("%s %s", leader, name);
	if (value) {
	    putchar(' ');
	    fdr_print_value(value);
	}
	putchar('\n');
    }

    return 0;
}

int
xo_encoder_library_init (XO_ENCODER_INIT_ARGS)
{
    /* Caller's version is older than we need; report ours and fail */
    if (arg->xei_version < XO_ENCODER_VERSION) {
	arg->xei_version = XO_ENCODER_VERSION;
	return -1;
    }

    arg->xei_version = XO_ENCODER_VERSION;
    arg->xei_handler = fdr_handler;
    arg->xei_flags |= XEIF_FILTER_RECORD; /* Record filters, unapplied */

    return 0;
}
