/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2014-2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, 2025
 *
 * xo_format.c: libxo format-string parser, factored out from libxo.c
 * so it can be compiled into tools (e.g. LLVM plugins) independently
 * of the full libxo runtime.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <sys/types.h>

#include "xo_config.h"
#include "xo_format.h"     /* xo.h + field types */
#include "xo_private.h"    /* THREAD_LOCAL (needs xo.h types) */

/*
 * Return a printable version of str, escaping control characters.
 * Uses a rotating set of static buffers — no allocation, no free.
 * Only used for error-message formatting.
 */
const char *
xo_printable2 (const char *str, int len, int bracesp)
{
    static THREAD_LOCAL(char) bufset[XO_NUMBUFS][XO_SMBUFSZ];
    static THREAD_LOCAL(int) bufnum = 0;

    if (str == NULL)
	return "";

    if (++bufnum == XO_NUMBUFS)
	bufnum = 0;

    char *res = bufset[bufnum], *cp, *ep;
    const char *str_end = str + len;

    cp = res;
    if (bracesp)
	*cp++ = '{';

    for (ep = res + XO_SMBUFSZ - 2;
	     str < str_end && *str && cp < ep; cp++, str++) {
	if (*str == '\n') {
	    *cp++ = '\\';
	    *cp = 'n';
	} else if (*str == '\r') {
	    *cp++ = '\\';
	    *cp = 'r';
	} else if (*str == '\"') {
	    *cp++ = '\\';
	    *cp = '"';
	} else
	    *cp = *str;
    }

    if (bracesp && cp < ep)
	*cp++ = '}';

    *cp = '\0';
    return res;
}

const char *
xo_printable (const char *str)
{
    return xo_printable2(str, strlen(str), 0);
}

/* Error reporting */


XO_PRINTFLIKE(2, 3)
static void
xo_parse_error (xo_parse_t *xpp, const char *fmt, ...)
{
    if (xpp == NULL || xpp->xp_error == NULL)
	return;

    char buf[512];
    va_list vap;
    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);
    xpp->xp_error(xpp->xp_error_data, "%s", buf);
}

XO_PRINTFLIKE(2, 3)
static void
xo_parse_warning (xo_parse_t *xpp, const char *fmt, ...)
{
    if (xpp == NULL || xpp->xp_warn == NULL)
	return;

    char buf[512];
    va_list vap;
    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);
    xpp->xp_warn(xpp->xp_warn_data, "%s", buf);
}

/* Allocator helper functions */

static void *
xo_parse_alloc (xo_parse_t *xpp, size_t sz)
{
    xo_realloc_func_t fn = xpp ? xpp->xp_realloc : NULL;
    if (fn == NULL)
	fn = realloc;

    return fn(NULL, sz);
}

static void
xo_parse_free (xo_parse_t *xpp, void *ptr)
{
    xo_free_func_t fn = xpp ? xpp->xp_free : NULL;
    if (fn == NULL)
	fn = free;
    fn(ptr);
}

/*
 * Look up a text name are return a value
 */
xo_xff_flags_t
xo_name_lookup (xo_flag_mapping_t *map, const char *value, ssize_t len)
{
    if (len == 0)
	return 0;

    if (len < 0)
	len = strlen(value);

    while (isspace((int) *value)) {
	value += 1;
	len -= 1;
    }

    while (len > 0 && isspace((int) value[len - 1]))
	len -= 1;

    if (*value == '\0')
	return 0;

    for ( ; map->xm_name; map++) {
	if (len < (ssize_t) strlen(map->xm_name))
	    continue;
	if (strncmp(map->xm_name, value, len) == 0)
	    return map->xm_value;
    }

    return 0;
}

static const char *
xo_value_lookup (xo_flag_mapping_t *map, xo_xff_flags_t value)
{
    if (value == 0)
	return NULL;

    for ( ; map->xm_name; map++)
	if (map->xm_value == value)
	    return map->xm_name;

    return NULL;
}

/*
 * Role and modifier tables
 */
xo_flag_mapping_t xo_role_names[] = {
    { 'C', "color" },
    { 'D', "decoration" },
    { 'E', "error" },
    { 'L', "label" },
    { 'N', "note" },
    { 'P', "padding" },
    { 'T', "title" },
    { 'U', "units" },
    { 'V', "value" },
    { 'W', "warning" },
    { '[', "start-anchor" },
    { ']', "stop-anchor" },
    { 0, NULL }
};

static xo_flag_mapping_t xo_modifier_names[] = {
    { XFF_ARGUMENT, "argument" },
    { XFF_COLON, "colon" },
    { XFF_COMMA, "comma" },
    { XFF_DISPLAY_ONLY, "display" },
    { XFF_ENCODE_ONLY, "encoding" },
    { XFF_ESC_PRIVATE, "escape-private" },
    { XFF_ESC_SLASH, "escape-slash" },
    { XFF_ESC_SQUARE, "escape-square" },
    { XFF_FIRST_CAP, "first-cap" },
    { XFF_GT_FIELD, "gettext" },
    { XFF_HUMANIZE, "humanize" },
    { XFF_HUMANIZE, "hn" },
    { XFF_HN_SPACE, "hn-space" },
    { XFF_HN_DECIMAL, "hn-decimal" },
    { XFF_HN_1000, "hn-1000" },
    { XFF_KEY, "key" },
    { XFF_LEAF_LIST, "leaf-list" },
    { XFF_LEAF_LIST, "list" },
    { XFF_NO_QUOTE, "no-quotes" },
    { XFF_NO_QUOTE, "no-quote" },
    { XFF_GT_PLURAL, "plural" },
    { XFF_QUOTE, "quotes" },
    { XFF_QUOTE, "quote" },
    { XFF_TRIM_WS, "trim" },
    { XFF_UNITS_ATTR, "units-attr" },
    { XFF_WS, "white" },
    { 0, NULL }
};

const char xo_default_format[] = "%s";

/*
 * Look up the name of a role
 */
const char *
xo_lookup_role_name (uint32_t value)
{
    return xo_value_lookup(xo_role_names, value);
}

int
xo_role_wants_default_format (int ftype)
{
    switch (ftype) {
    case 'C':
    case 'G':
    case '[':
    case ']':
	return 0;
    }
    return 1;
}

/**
 * Bump one of the 'width' values in a format strings (e.g. "%40.50.60s").
 * @param xfp Formatting instructions
 * @param digit Single digit (0-9) of input
 */
static void
xo_bump_width (xo_fspec_t *xfp, int digit)
{
    int16_t *ip = &xfp->xf_width[xfp->xf_dots];

    *ip = ((*ip > 0) ? *ip : 0) * 10 + digit;
}

/*
 * Parse a printf-style format specifier starting at 'cp' (which points
 * at the '%').  Fills in *xfp and returns a pointer to the conversion
 * character, or NULL on error.
 *
 * Note that 'n', 'v', and '$' are not supported.
 */
const char *
xo_parse_format_spec (xo_parse_t *xpp, xo_fspec_t *xfp,
		      const char *cp, const char *ep, const char *fmt)
{
    const char *start = cp;

    for (cp += 1; cp < ep; cp++) {
	if (*cp == 'l')
	    xfp->xf_lflag += 1;
	else if (*cp == 'h')
	    xfp->xf_hflag += 1;
	else if (*cp == 'j')
	    xfp->xf_jflag += 1;
	else if (*cp == 't')
	    xfp->xf_tflag += 1;
	else if (*cp == 'z')
	    xfp->xf_zflag += 1;
	else if (*cp == 'q')
	    xfp->xf_qflag += 1;
	else if (*cp == '.') {
	    if (xfp->xf_dots + 1 >= XF_WIDTH_NUM) {
		xo_parse_error(xpp, "Too many dots in format: '%s'", fmt);
		return NULL;
	    }

	    xfp->xf_dots += 1;	/* Increment it (after check) */

	} else if (*cp == '-')
	    xfp->xf_seen_minus = 1;

	else if (*cp == '#')
	    xfp->xf_alt = 1;

	else if (*cp == '!') {
	    /* "%!NNd" is a NN-bit signed value */
	    const char *sp = cp + 1;
	    const char *np = sp;
	    uint64_t num_bits = 0;

	    for (; np < ep; np++) {
		if (!isdigit((int) *np))
		    break;
		num_bits = num_bits * 10 + (*np - '0');
	    }

	    cp += np - sp;	/* Move pointer along */

	    /* Report errors, which leave xf_num_bits as 0, ignoring the "!" */
	    if (np == sp)
		xo_parse_error(xpp, "missing integer size: '%s'",
			       xo_printable2(start, ep - start, TRUE));
	    else if (num_bits != 8 && num_bits != 16
		     && num_bits != 32 && num_bits != 64)
		xo_parse_error(xpp, "invalid integer size: '%s'",
			       xo_printable2(start, ep - start, TRUE));
	    else
		xfp->xf_num_bits = num_bits;

	} else if (isdigit((int) *cp)) {
	    if (xfp->xf_leading_zero < 0)
		xfp->xf_leading_zero = (*cp == '0');
	    xo_bump_width(xfp, *cp - '0');

	} else if (*cp == '*') {
	    xfp->xf_stars += 1;
	    xfp->xf_star[xfp->xf_dots] = 1;

	} else if (strchr("diouxXDOUeEfFgGaAcCsSpm", *cp) != NULL)
	    break;

	else if (*cp == 'n' || *cp == 'v') {
	    xo_parse_error(xpp, "unsupported format: '%s'", fmt);
	    return NULL;
	}
    }

    if (cp == ep)
	xo_parse_error(xpp, "field format missing format character: %s", fmt);

    xfp->xf_fc = *cp;
    return cp;
}

/*
 * Parse one "%..." conversion starting at 'cp' (which points at the '%').
 * Pure parser: no handle, no va_list, no style/skip decisions — those are
 * per-call and stay in libxo.c's xo_do_format_field().  Fills in *xfp,
 * including xf_start/xf_len (byte offsets relative to 'fmt', covering the
 * '%' through and including the conversion character), and returns a
 * pointer to the conversion character, or NULL on error.
 */
static const char *
xo_parse_one_format (xo_parse_t *xpp, xo_fspec_t *xfp, const char *cp,
		     const char *ep, const char *fmt)
{
    const char *start = cp;		/* Points at the '%' */

    bzero(xfp, sizeof(*xfp));
    xfp->xf_leading_zero = -1;
    xfp->xf_width[0] = xfp->xf_width[1] = xfp->xf_width[2] = -1;

    /*
     * "%@...@" is an XO-specific prefix.  Each '*' inside marks an int
     * arg (from a "%*.*s"-style caller) that must be consumed from
     * va_list and discarded before this field's own value is pulled.
     * We have no va_list here (pure parser), so just count the '*'s
     * into xf_at_stars; the emit path does the actual consuming.
     */
    if (cp[1] == '@') {
	for (cp += 2; cp < ep; cp++) {
	    if (*cp == '@')
		break;
	    if (*cp == '*')
		xfp->xf_at_stars += 1;
	}

	/* 'cp' now sits on the prefix's closing '@', our pseudo-'%' */
	xfp->xf_prefix_len = (uint16_t)(cp - start);
    }

    cp = xo_parse_format_spec(xpp, xfp, cp, ep, fmt);
    if (cp == NULL)
	return NULL;

    /* If no max is given, it defaults to size */
    if (xfp->xf_width[XF_WIDTH_MAX] < 0
		&& xfp->xf_width[XF_WIDTH_SIZE] >= 0)
	xfp->xf_width[XF_WIDTH_MAX] = xfp->xf_width[XF_WIDTH_SIZE];

    if (xfp->xf_fc == 'D' || xfp->xf_fc == 'O' || xfp->xf_fc == 'U')
	xfp->xf_lflag = 1;

    xfp->xf_start = (uint16_t)(start - fmt);
    xfp->xf_len   = (uint16_t)(cp - start + 1);

    return cp;
}

/*
 * Parse a single "%..." format specifier, returning the next
 * character to be processed.  Information about the fspec is recorded
 * in the xpp->xp_fspecs array and xp_cur_fspec is moved along.
 */
int
xo_parse_fspecs (xo_parse_t *xpp, const char *fmt, const char *ep)
{
    xo_fspec_t *xfp = xpp->xp_cur_fspec;
    xo_fspec_t *limit = xpp->xp_fspecs + xpp->xp_num_fspecs;
    const char *cp, *xp = NULL;
    int num = 0;

    for (cp = fmt; cp < ep; cp++) {
	if (*cp != '%') {
	add_one:
	    if (xp == NULL)
		xp = cp;
	    if (*cp == '\\' && cp[1] != '\0')
		cp += 1;
	    continue;

	} else if (cp + 1 < ep && cp[1] == '%') {
	    /* "%%" folds into the preceding literal run as one '%' byte */
	    cp += 1;
	    goto add_one;
	}

	if (xp) {
	    if (xfp + 1 >= limit)
		return -2;	/* Out of room; caller falls back at runtime */

	    /*
	     * xf_fc is the format character (e.g. 'd' in "%10d");
	     * it's not really a "role" but we reuse XO_ROLE_TEXT
	     * here, since it's safe ('+' isn't a formatting
	     * character).  We'll notice it when making output and
	     * emit it as normal text.
	     */
	    xfp->xf_fc = XO_ROLE_TEXT;
	    xfp->xf_start = (uint16_t)(xp - fmt);
	    xfp->xf_len = (uint16_t)(cp - xp);
	    xfp += 1;
	    num += 1;
	    xp = NULL;
	}

	if (xfp + 1 >= limit)
	    return -2;

	cp = xo_parse_one_format(xpp, xfp, cp, ep, fmt);
	if (cp == NULL)
	    return -1;

	xfp += 1;
	num += 1;
    }

    if (xp) {
	if (xfp + 1 >= limit)
	    return -2;

	xfp->xf_fc = XO_ROLE_TEXT;
	xfp->xf_start = (uint16_t)(xp - fmt);
	xfp->xf_len = (uint16_t)(cp - xp);
	xfp += 1;
	num += 1;
    }

    /*
     * xfp points at a zeroed slot (from the initial bzero); that's our
     * terminator.  Advance the cursor past it so the next field's run
     * starts on a fresh zeroed slot.
     */
    xpp->xp_cur_fspec = xfp + 1;

    return num;
}

void
xo_count_fields (xo_parse_t *xpp XO_UNUSED, const char *fmt,
		 size_t *fmt_lenp, unsigned *max_fields, unsigned *max_specs)
{
    unsigned fields = 1;
    unsigned percents = 0;
    const char *cp;

    for (cp = fmt; *cp; cp++) {
	if (*cp == '{' || *cp == '\n')
	    fields += 1;
	if (*cp == '%')
	    percents += 1;
    }

    fields = fields * 2 + 1;	/* Maximally pessimistic */

    /*
     * Pessimistic fspec bound: one text entry plus one spec entry per
     * '%', plus one terminator per field.
     */
    unsigned specs = 2 * (percents + 1) + fields;

    if (fields > XO_MAX_FIELDS)
	fields = XO_MAX_FIELDS;

    if (specs > XO_MAX_SPECS)
	specs = XO_MAX_SPECS;

    if (fmt_lenp)
	*fmt_lenp = cp - fmt;

    *max_fields = fields;
    *max_specs = specs;
}

/*
 * Parse the role/modifier prefix of a field descriptor: the part
 * before the ':'.  Returns a pointer to the ':' (or '/' or '}') on
 * success, NULL on error.
 */
const char *
xo_parse_roles (xo_parse_t *xpp, const char *fmt,
		const char *basep, xo_field_info_t *xfip)
{
    const char *sp;
    unsigned ftype = 0;
    xo_xff_flags_t flags = 0;
    uint8_t fnum = 0;

    for (sp = basep; sp && *sp; sp++) {
	if (*sp == ':' || *sp == '/' || *sp == '}')
	    break;

	if (*sp == '\\') {
	    if (sp[1] == '\0') {
		xo_parse_error(xpp, "backslash at the end of string, ignored");
		return NULL;
	    }
	    sp += 1;		/* Anything backslashed is ignored */
	    continue;
	}

	if (*sp == ',') {
	    const char *np;
	    for (np = ++sp; *np; np++)
		if (*np == ':' || *np == '/' || *np == '}' || *np == ',')
		    break;

	    ssize_t slen = np - sp;
	    if (slen > 0) {
		xo_xff_flags_t value;

		value = xo_name_lookup(xo_role_names, sp, slen);
		if (value)
		    ftype = value;
		else {
		    value = xo_name_lookup(xo_modifier_names, sp, slen);
		    if (value)
			flags |= value;
		    else
			xo_parse_error(xpp, "unknown keyword ignored: '%.*s'",
				       (int) slen, sp);
		}
	    }

	    sp = np - 1;
	    continue;
	}

	switch (*sp) {
	case 'C':
	case 'D':
	case 'E':
	case 'G':
	case 'L':
	case 'N':
	case 'P':
	case 'T':
	case 'U':
	case 'V':
	case 'W':
	case '[':
	case ']':
	    if (ftype != 0) {
		xo_parse_error(xpp,
			       "field descriptor uses multiple types: '%s'",
			       xo_printable(fmt));
		return NULL;
	    }
	    ftype = *sp;
	    break;

	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
	    fnum = (fnum * 10) + (*sp - '0');
	    break;

	case 'a': flags |= XFF_ARGUMENT;    break;
	case 'c': flags |= XFF_COLON;       break;
	case 'd': flags |= XFF_DISPLAY_ONLY; break;
	case 'e': flags |= XFF_ENCODE_ONLY; break;
	case 'f': flags |= XFF_FIRST_CAP;   break;
	case 'g': flags |= XFF_GT_FIELD;    break;
	case 'h': flags |= XFF_HUMANIZE;    break;
	case 'k': flags |= XFF_KEY;         break;
	case 'l': flags |= XFF_LEAF_LIST;   break;
	case 'n': flags |= XFF_NO_QUOTE;    break;
	case 'p': flags |= XFF_GT_PLURAL;   break;
	case 'q': flags |= XFF_QUOTE;       break;
	case 't': flags |= XFF_TRIM_WS;     break;
	case 'w': flags |= XFF_WS;          break;

	default:
	    xo_parse_error(xpp,
			   "field descriptor uses unknown modifier: '%s'",
			   xo_printable(fmt));
	}

	if (ftype == 'N' || ftype == 'U') {
	    if (flags & XFF_COLON) {
		xo_parse_error(xpp,
		       "colon modifier on 'N' or 'U' field ignored: '%s'",
		       xo_printable(fmt));
		flags &= ~XFF_COLON;
	    }
	}
    }

    xfip->xfi_flags = flags;
    xfip->xfi_ftype = ftype ?: 'V';
    xfip->xfi_fnum  = fnum;

    return sp;
}

/*
 * Validate and assign field numbers when at least one {N:...} is used
 */
static int
xo_parse_field_numbers (xo_parse_t *xpp, const char *fmt,
			xo_field_info_t *fields, unsigned num_fields)
{
    xo_field_info_t *xfip;
    unsigned field, fnum;
    uint64_t bits = 0;
    const uint64_t one = 1;

    for (xfip = fields, field = 0; field < num_fields; xfip++, field++) {
	if (xfip->xfi_fnum == 0)
	    xfip->xfi_fnum = field + 1;
	else if (xfip->xfi_fnum > num_fields) {
	    xo_parse_error(xpp,
			   "field number exceeds number of fields: '%s'",
			   xo_printable(fmt));
	    return -1;
	}

	fnum = xfip->xfi_fnum - 1;
	if (fnum < 64) {
	    if (bits & (one << fnum)) {
		xo_parse_error(xpp, "field number reused: #%u in '%s'",
			       xfip->xfi_fnum, xo_printable(fmt));
		return -1;
	    }
	    bits |= one << fnum;
	}
    }

    return 0;
}

/*
 * The field format is:
 *  '{' modifiers ':' content [ '/' print-fmt [ '/' encode-fmt ]] '}'
 * Roles are optional and include the following field types:
 *   'D': decoration; something non-text and non-data (colons, commmas)
 *   'E': error message
 *   'G': gettext() the entire string; optional domainname as content
 *   'L': label; text preceding data
 *   'N': note; text following data
 *   'P': padding; whitespace
 *   'T': Title, where 'content' is a column title
 *   'U': Units, where 'content' is the unit label
 *   'V': value, where 'content' is the name of the field (the default)
 *   'W': warning message
 *   '[': start a section of anchored text
 *   ']': end a section of anchored text
 * The following modifiers are also supported:
 *   'a': content is provided via argument (const char *), not descriptor
 *   'c': flag: emit a colon after the label
 *   'd': field is only emitted for display styles (text and html)
 *   'e': field is only emitted for encoding styles (xml and json)
 *   'g': gettext() the field
 *   'h': humanize a numeric value (only for display styles)
 *   'k': this field is a key, suitable for XPath predicates
 *   'l': a leaf-list, a simple list of values
 *   'n': no quotes around this field
 *   'p': the field has plural gettext semantics (ngettext)
 *   'q': add quotes around this field
 *   't': trim whitespace around the value
 *   'w': emit a blank after the label
 * xo_parse_roles() is now in xo_field.c.
 */
int
xo_parse_fields (xo_parse_t *xpp, const char *fmt, size_t fmt_len)
{
    const char *cp, *sp, *ep, *basep;
    unsigned field = 0;
    xo_field_info_t *fields = xpp->xp_fields;
    unsigned num_fields = xpp->xp_num_fields; /* Capacity, set by our caller */
    xo_field_info_t *xfip = xpp->xp_cur_field;
    unsigned seen_fnum = 0;

    /* Reject oversized format strings (int16_t offset range) */
    if (fmt_len > XO_FORMAT_MAX) {
	xo_parse_error(xpp, "format string too long (max %zu bytes, len %zu)",
		       (size_t) XO_FORMAT_MAX, fmt_len);
	return -1;
    }

    for (cp = fmt; *cp && field < num_fields; field++, xfip++) {
	/* Initialize offset members to XO_FOFF_NONE; 0 is a valid offset */
	xfip->xfi_start    = XO_FOFF_NONE;
	xfip->xfi_content  = XO_FOFF_NONE;
	xfip->xfi_format   = XO_FOFF_NONE;
	xfip->xfi_encoding = XO_FOFF_NONE;
	xfip->xfi_next     = XO_FOFF_NONE;
	xfip->xfi_padding[0] = xfip->xfi_padding[1] = xfip->xfi_padding[2] = 0;

	xfip->xfi_start = (xo_format_offset_t)(cp - fmt);

	if (*cp == '\n') {
	    /*
	     * xfi_flags/xfi_fnum/xfi_renum are each read unconditionally
	     * (no ftype check) by some consumer downstream -- xfi_flags by
	     * xo_do_emit_fields()'s hot loop (before the ftype switch),
	     * xfi_fnum/xfi_renum by the gettext helpers.  NEWLINE/TEXT/
	     * EBRACE never run xo_parse_roles(), which is what normally
	     * sets these, so they must be zeroed here explicitly: a stray
	     * XFF_ARGUMENT bit in leftover stack data would steal a
	     * va_arg() on this field, and a stray field number would
	     * confuse gettext field lookup/renumbering.
	     */
	    xfip->xfi_flags = 0;
	    xfip->xfi_fnum = 0;
	    xfip->xfi_renum = 0;
	    xfip->xfi_ftype = XO_ROLE_NEWLINE;
	    xfip->xfi_len = 1;
	    cp += 1;
	    continue;
	}

	if (*cp != '{') {
	    for (sp = cp; *sp; sp++)
		if (*sp == '{' || *sp == '\n')
		    break;

	    xfip->xfi_flags   = 0;	/* See NEWLINE case above */
	    xfip->xfi_fnum    = 0;
	    xfip->xfi_renum   = 0;
	    xfip->xfi_ftype   = XO_ROLE_TEXT;
	    xfip->xfi_content = (xo_format_offset_t)(cp - fmt);
	    xfip->xfi_clen    = (xo_format_offset_t)(sp - cp);
	    xfip->xfi_next    = (xo_format_offset_t)(sp - fmt);
	    cp = sp;
	    continue;
	}

	if (cp[1] == '{') {		/* {{ escaped brace */
	    const char *start_ptr = cp + 1;
	    xfip->xfi_start = (xo_format_offset_t)(start_ptr - fmt);
	    xfip->xfi_flags = 0;	/* See NEWLINE case above */
	    xfip->xfi_fnum = 0;
	    xfip->xfi_renum = 0;
	    xfip->xfi_ftype = XO_ROLE_EBRACE;

	    cp += 2;
	    for (sp = cp; *sp; sp++)
		if (*sp == '}' && sp[1] == '}')
		    break;

	    if (*sp == '\0') {
		xo_parse_error(xpp, "missing closing '}}': '%s'",
			       xo_printable(fmt));
		return -1;
	    }

	    xfip->xfi_len = (xo_format_offset_t)(sp - start_ptr + 1);

	    if (*sp == '}' && sp[1] == '}')
		sp += 2;

	    cp = sp;
	    xfip->xfi_next = (xo_format_offset_t)(cp - fmt);
	    continue;
	}

	basep = cp + 1;
	xfip->xfi_start = (xo_format_offset_t)(basep - fmt);

	const char *format = NULL;
	ssize_t flen = 0;

	sp = xo_parse_roles(xpp, fmt, basep, xfip);
	if (sp == NULL)
	    return -1;

	if (xfip->xfi_fnum)
	    seen_fnum = 1;

	/* Content (name), between ':' and '/' or '}' */
	if (*sp == ':') {
	    for (ep = ++sp; *sp; sp++) {
		if (*sp == '}' || *sp == '/')
		    break;
		if (*sp == '\\') {
		    if (sp[1] == '\0') {
			xo_parse_error(xpp, "backslash at the end of string");
			return -1;
		    }
		    sp += 1;
		    continue;
		}
	    }
	    if (ep != sp) {
		xfip->xfi_clen    = (xo_format_offset_t)(sp - ep);
		xfip->xfi_content = (xo_format_offset_t)(ep - fmt);
	    } else {
		/*
		 * Empty content (e.g. "{[:/%d}"): xfi_content is already
		 * XO_FOFF_NONE from the top-of-loop reset, but xfi_clen
		 * isn't one of those offset members and consumers like
		 * xo_find_width() trust a nonzero length even when the
		 * resolved pointer is NULL, so it must be zeroed here.
		 */
		xfip->xfi_clen = 0;
	    }
	} else {
	    xo_parse_error(xpp, "missing content (':'): '%s'",
			   xo_printable(fmt));
	    return -1;
	}

	/* Display format, between first '/' and second '/' or '}' */
	if (*sp == '/') {
	    for (ep = ++sp; *sp; sp++) {
		if (*sp == '}' || *sp == '/')
		    break;
		if (*sp == '\\') {
		    if (sp[1] == '\0') {
			xo_parse_error(xpp, "backslash at the end of string");
			return -1;
		    }
		    sp += 1;
		    continue;
		}
	    }
	    flen   = sp - ep;
	    format = ep;
	}

	/* Encoding format, between second '/' and '}' */
	if (*sp == '/') {
	    for (ep = ++sp; *sp; sp++)
		if (*sp == '}')
		    break;

	    /* A probable typo, since the encoding field is empty: {:foo/%s/} */
	    if (ep == sp) {
		xo_parse_error(xpp, "zero length encoding format, ignored");
		xfip->xfi_elen = (xo_format_offset_t)(sp - ep);
	    } else {
		xfip->xfi_encoding = (xo_format_offset_t)(ep - fmt);
		xfip->xfi_elen = (xo_format_offset_t)(sp - ep);
	    }

	} else {
	    /*
	     * No encoding format: xfi_encoding is already XO_FOFF_NONE
	     * from the top-of-loop reset, but consumers (e.g. xo_format_
	     * title()'s "flen == 0 means use the default" check) trust
	     * xfi_elen/xfi_flen directly, so it must be zeroed here too.
	     */
	    xfip->xfi_elen = 0;
	}

	if (*sp != '}') {
	    xo_parse_error(xpp, "missing closing '}': %s",
			   xo_printable(fmt));
	    return -1;
	}

	xfip->xfi_len  = (xo_format_offset_t)(sp - basep);
	xfip->xfi_next = (xo_format_offset_t)(sp + 1 - fmt);
	sp += 1;

	if (format) {
	    xfip->xfi_format = (xo_format_offset_t)(format - fmt);
	    xfip->xfi_flen   = (xo_format_offset_t)flen;
	} else if ((xfip->xfi_clen || (xfip->xfi_flags & XFF_ARGUMENT))
		&& xo_role_wants_default_format(xfip->xfi_ftype)) {
	    xfip->xfi_format = XO_FOFF_DEFAULT;
	    xfip->xfi_flen   = 2;
	} else {
	    /*
	     * No display format at all (e.g. an anchor's "{[:...}"):
	     * xfi_format is already XO_FOFF_NONE from the top-of-loop
	     * reset, but consumers trust xfi_flen directly (e.g.
	     * xo_format_title()'s "flen == 0 means use the default"
	     * check), so it must be zeroed here too.
	     */
	    xfip->xfi_flen = 0;
	}

	/*
	 * xfi_format stayed XO_FOFF_NONE: the eager fspecs pass below
	 * won't run, so xfi_fspecs must be NULL (never left as leftover
	 * stack data) -- NULL is xo_do_format_field()'s "re-parse the
	 * format on the fly" signal, and a stray non-NULL pointer here
	 * paired with a stray xfi_num_fspecs would walk unrelated memory.
	 */
	if (xfip->xfi_format == XO_FOFF_NONE) {
	    xfip->xfi_fspecs = NULL;
	    xfip->xfi_num_fspecs = 0;
	}

	/*
	 * Populate: pre-parse the display format into fspec entries so
	 * the emit path can walk them instead of rescanning.  Encoding
	 * formats are out of scope for now (Phase 1 decision); that path
	 * still re-parses at call time.
	 */
	if (xfip->xfi_format != XO_FOFF_NONE) {
	    const char *ffmt = xo_foff(fmt, xfip->xfi_format);
	    xo_fspec_t *fstart = xpp->xp_cur_fspec;
	    int nspecs = xo_parse_fspecs(xpp, ffmt, ffmt + xfip->xfi_flen);

	    if (nspecs >= 0) {
		xfip->xfi_fspecs = fstart;
		xfip->xfi_num_fspecs = (uint16_t)nspecs;
	    } else if (nspecs == -2) {
		/* Out of room; NULL is always a safe "re-parse" signal */
		xfip->xfi_fspecs = NULL;
		xfip->xfi_num_fspecs = 0;
	    } else {
		return -1;	/* Genuine error; already reported */
	    }
	}

	/* Semantic validation in strict mode only; not during xo_emit() */
	if (!(xpp->xp_flags & XPF_STRICT))
	    goto next_field;

	const char *str = xo_foff(fmt, xfip->xfi_start);
	int slen = xfip->xfi_len;

	if (xfip->xfi_ftype == 'V') {
	    const char *np = xo_foff(fmt, xfip->xfi_content);
	    unsigned nlen = (unsigned)xfip->xfi_clen;
	    unsigned ni;

	    if (nlen == 0 && !(xfip->xfi_flags & XFF_ARGUMENT)) {
		xo_parse_error(xpp, "field must have a name: '%s'",
			       xo_printable2(str, slen, TRUE));
		return -1;
	    }
	    if (np && nlen) {
		if (isdigit((unsigned char) np[0])) {
		    xo_parse_warning(xpp,
			   "field name cannot start with a digit: '%s'",
				     xo_printable2(str, slen, TRUE));
		}

		struct reported_errors {
		    int re_percent; /* Percent sign */
		    int re_under; /* Underscores */
		    int re_upper; /* Upper case */
		    int re_invalid; /* Invalid character */
		} re = { 0, 0, 0, 0 };

		for (ni = 0; ni < nlen; ni++) {
		    unsigned char nc = (unsigned char) np[ni];

		    if (!re.re_percent && nc == '%') {
			xo_parse_warning(xpp,
			       "field name contains percent sign: '%s'",
					 xo_printable2(str, slen, TRUE));
			re.re_percent = 1;
			continue;
		    }

		    if (XO_IS_LINT(xpp) && !re.re_under && nc == '_') {
			xo_parse_warning(xpp,
			       "use hyphens, not underscores, "
				       "in field name: '%s'",
					 xo_printable2(str, slen, TRUE));
			re.re_under = 1;
		    }

		    if (XO_IS_LINT(xpp) && !re.re_upper && isupper(nc)) {
			xo_parse_warning(xpp,
			       "field name should be lower case: '%s'",
					 xo_printable2(str, slen, TRUE));
			re.re_upper = 1;
		    }
		}

		if (XO_IS_LINT(xpp) && !re.re_percent
			    && nlen < XO_LINT_MIN_NAME) {
		    xo_parse_warning(xpp,
				     "field name should not be less than "
				     "%d characters long: '%s'",
				     XO_LINT_MIN_NAME,
				     xo_printable2(str, slen, TRUE));
		}
	    }
	}

	if (xfip->xfi_ftype == '[' || xfip->xfi_ftype == ']') {
	    const char *np = xo_foff(fmt, xfip->xfi_content);
	    unsigned nlen = (unsigned)xfip->xfi_clen;
	    if (np && nlen) {
		unsigned ni = (np[0] == '-') ? 1 : 0;
		for (; ni < nlen; ni++) {
		    if (!isdigit((unsigned char) np[ni])) {
			xo_parse_error(xpp,
			       "anchor content must be a decimal width: '%s'",
				       xo_printable2(str, slen, TRUE));
			break;
		    }
		}
		if (format && flen > 0) {
		    xo_parse_error(xpp,
			   "anchor cannot have both static "
				   "width and format: '%s'",
				   xo_printable2(str, slen, TRUE));
		}
	    }
	    if (format && flen > 0) {
		/* Anchor width must be "%d" or numeric */
		if (flen != 2 || format[0] != '%'
		        || !(format[1] != 'd' || format[1] != 'u')) {
		    char *aep = NULL;
		    (void) strtol(format, &aep, 10);
		    if (aep != format + flen)
			xo_parse_error(xpp,
			       "anchor format must be '%%d' or '%%u: '%s'",
				       xo_printable2(str, slen, TRUE));
		}
	    }
	}

	if ((xfip->xfi_flags & XFF_HUMANIZE) && !format) {
	    xo_parse_error(xpp,
		   "humanize modifier ('h') requires a format string: '%s'",
		   xo_printable(fmt));
	    return -1;
	}

next_field:
	cp = sp;
    }

    /*
     * Leave the cursor just past the last field written.  Sentinel
     * scans elsewhere walk fields[] until xfi_ftype == 0, so this one
     * slot (guaranteed to exist by xo_count_fields()'s pessimistic
     * sizing) must be zeroed explicitly now that we no longer bzero
     * the whole array.  Our caller also uses the cursor delta to
     * learn the real entry count.
     */
    if (field < num_fields)
	bzero(xfip, sizeof(*xfip));
    xpp->xp_cur_field = xfip;

    if (seen_fnum)
	return xo_parse_field_numbers(xpp, fmt, fields, field);

    return 0;
}

/*
 * We need an external (but not "public") API that is callable from
 * our llvm plugin.
 */
void
xo_parse_release (xo_parse_t *xpp)
{
    if (xpp == NULL)
	return;

    if (xpp->xp_fields) {
	xo_parse_free(xpp, xpp->xp_fields);
	xpp->xp_fields = NULL;
	xpp->xp_num_fields = 0;
    }

    if (xpp->xp_fspecs) {
	xo_parse_free(xpp, xpp->xp_fspecs);
	xpp->xp_fspecs = NULL;
	xpp->xp_num_fspecs = 0;
    }

    xpp->xp_cur_field = NULL;
    xpp->xp_cur_fspec = NULL;
}

int
xo_parse_format (xo_parse_t *xpp, const char *fmt)
{
    if (xpp == NULL || fmt == NULL)
	return -1;

    xo_parse_release(xpp);

    size_t fmt_len;
    unsigned max_fields, max_fspecs;
    xo_count_fields(xpp, fmt, &fmt_len, &max_fields, &max_fspecs);

    size_t sz = max_fields * sizeof(xo_field_info_t);

    xo_field_info_t *fields = xo_parse_alloc(xpp, sz);
    if (fields == NULL) {
	xo_parse_error(xpp, "xo_parse_format: out of memory");
	return -1;
    }
    bzero(fields, sz);

    sz = max_fspecs * sizeof(xo_fspec_t);

    xo_fspec_t *fspecs = xo_parse_alloc(xpp, sz);
    if (fspecs == NULL) {
	xo_parse_error(xpp, "xo_parse_format: out of memory");
	xo_parse_free(xpp, fields);
	return -1;
    }
    memset(fspecs, 0, sz);

    xpp->xp_fields = xpp->xp_cur_field = fields;
    xpp->xp_fspecs = xpp->xp_cur_fspec = fspecs;
    xpp->xp_num_fields = max_fields;
    xpp->xp_num_fspecs = max_fspecs;

    if (xo_parse_fields(xpp, fmt, fmt_len) < 0) {
	xo_parse_free(xpp, fields);
	xo_parse_free(xpp, fspecs);
	xpp->xp_fields = xpp->xp_cur_field = NULL;
	xpp->xp_fspecs = xpp->xp_cur_fspec = NULL;
	xpp->xp_num_fields = xpp->xp_num_fspecs = 0;
	return -1;
    }

    /*
     * xo_parse_fields() leaves xp_cur_field/xp_cur_fspec pointing just
     * past the last entry written (a zeroed terminator slot); the delta
     * from the base is exactly the real entry count.
     */
    xpp->xp_fields = fields;
    xpp->xp_num_fields = (unsigned)(xpp->xp_cur_field - fields);
    xpp->xp_fspecs = fspecs;
    xpp->xp_num_fspecs = (unsigned)(xpp->xp_cur_fspec - fspecs);

    return 0;
}
