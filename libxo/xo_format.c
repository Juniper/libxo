/*
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
xo_printable (const char *str)
{
    static THREAD_LOCAL(char) bufset[XO_NUMBUFS][XO_SMBUFSZ];
    static THREAD_LOCAL(int) bufnum = 0;

    if (str == NULL)
	return "";

    if (++bufnum == XO_NUMBUFS)
	bufnum = 0;

    char *res = bufset[bufnum], *cp, *ep;

    for (cp = res, ep = res + XO_SMBUFSZ - 1; *str && cp < ep; cp++, str++) {
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

    *cp = '\0';
    return res;
}

/* Error reporting */

static void
xo_parse_error (xo_parse_t *xpp, const char *fmt, ...)
{
    if (xpp == NULL || xpp->xp_error == NULL)
	return;

    va_list vap;
    va_start(vap, fmt);
    xpp->xp_error(xpp->xp_error_data, fmt, vap);
    va_end(vap);
}

static void
xo_parse_warning (xo_parse_t *xpp, const char *fmt, ...)
{
    if (xpp == NULL || xpp->xp_error == NULL)
	return;

    va_list vap;
    va_start(vap, fmt);
    xpp->xp_error(xpp->xp_error_data, fmt, vap);
    va_end(vap);
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

#ifdef NOT_NEEDED_YET
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
#endif /* NOT_NEEDED_YET */

/*
 * Role and modifier tables
 */
static xo_flag_mapping_t xo_role_names[] = {
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
    { XFF_GT_FIELD, "gettext" },
    { XFF_HUMANIZE, "humanize" },
    { XFF_HUMANIZE, "hn" },
    { XFF_HN_SPACE, "hn-space" },
    { XFF_HN_DECIMAL, "hn-decimal" },
    { XFF_HN_1000, "hn-1000" },
    { XFF_KEY, "key" },
    { XFF_LEAF_LIST, "leaf-list" },
    { XFF_LEAF_LIST, "list" },
    { XFF_NOQUOTE, "no-quotes" },
    { XFF_NOQUOTE, "no-quote" },
    { XFF_GT_PLURAL, "plural" },
    { XFF_QUOTE, "quotes" },
    { XFF_QUOTE, "quote" },
    { XFF_TRIM_WS, "trim" },
    { XFF_WS, "white" },
    { 0, NULL }
};

const char xo_default_format[] = "%s";

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

unsigned
xo_count_fields (xo_parse_t *xpp __attribute__((unused)), const char *fmt)
{
    unsigned rc = 1;
    const char *cp;

    for (cp = fmt; *cp; cp++)
	if (*cp == '{' || *cp == '\n')
	    rc += 1;

    if (rc > XO_MAX_FIELDS)
	rc = XO_MAX_FIELDS;

    return rc * 2 + 1;
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
	case 'g': flags |= XFF_GT_FIELD;    break;
	case 'h': flags |= XFF_HUMANIZE;    break;
	case 'k': flags |= XFF_KEY;         break;
	case 'l': flags |= XFF_LEAF_LIST;   break;
	case 'n': flags |= XFF_NOQUOTE;     break;
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
xo_parse_fields (xo_parse_t *xpp, xo_field_info_t *fields,
		 unsigned num_fields, const char *fmt)
{
    const char *cp, *sp, *ep, *basep;
    unsigned field = 0;
    xo_field_info_t *xfip = fields;
    unsigned seen_fnum = 0;

    /* Reject oversized format strings (int16_t offset range) */
    if (strlen(fmt) > XO_FORMAT_MAX) {
	xo_parse_error(xpp, "format string too long (max %d bytes, len %d)",
		       XO_FORMAT_MAX, strlen(fmt));
	return -1;
    }

    for (cp = fmt; *cp && field < num_fields; field++, xfip++) {
	/* Initialize offset members to XO_FOFF_NONE; 0 is a valid offset */
	xfip->xfi_start    = XO_FOFF_NONE;
	xfip->xfi_content  = XO_FOFF_NONE;
	xfip->xfi_format   = XO_FOFF_NONE;
	xfip->xfi_encoding = XO_FOFF_NONE;
	xfip->xfi_next     = XO_FOFF_NONE;

	xfip->xfi_start = (xo_format_offset_t)(cp - fmt);

	if (*cp == '\n') {
	    xfip->xfi_ftype = XO_ROLE_NEWLINE;
	    xfip->xfi_len = 1;
	    cp += 1;
	    continue;
	}

	if (*cp != '{') {
	    for (sp = cp; *sp; sp++)
		if (*sp == '{' || *sp == '\n')
		    break;

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

	    xfip->xfi_encoding = (xo_format_offset_t)(ep - fmt);
	    xfip->xfi_elen     = (xo_format_offset_t)(sp - ep);
	}

	if (*sp != '}') {
	    xo_parse_error(xpp, "missing closing '}': %s",
			   xo_printable(fmt));
	    return -1;
	}

	xfip->xfi_len  = (xo_format_offset_t)(sp - basep);
	xfip->xfi_next = (xo_format_offset_t)(sp + 1 - fmt);
	sp += 1;

	if (xfip->xfi_clen || format || (xfip->xfi_flags & XFF_ARGUMENT)) {
	    if (format) {
		xfip->xfi_format = (xo_format_offset_t)(format - fmt);
		xfip->xfi_flen   = (xo_format_offset_t)flen;
	    } else if (xo_role_wants_default_format(xfip->xfi_ftype)) {
		xfip->xfi_format = XO_FOFF_DEFAULT;
		xfip->xfi_flen   = 2;
	    }
	}

	/* Semantic validation (mirrors xolint checks) — strict mode only */
	if (!(xpp->xp_flags & XPF_STRICT))
	    goto next_field;

	if (xfip->xfi_ftype == 'V') {
	    const char *np = xo_foff(fmt, xfip->xfi_content);
	    unsigned nlen = (unsigned)xfip->xfi_clen;
	    unsigned ni;

	    if (nlen == 0 && !(xfip->xfi_flags & XFF_ARGUMENT)) {
		xo_parse_error(xpp, "field must have a name: '%s'",
			       xo_printable(fmt));
		return -1;
	    }
	    if (np && nlen) {
		if (isdigit((unsigned char) np[0])) {
		    xo_parse_warning(xpp,
			   "field name cannot start with a digit: '%.*s'",
			   (int) nlen, np);
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
			       "field name contains percent sign: '%.*s'",
			       (int) nlen, np);
			re.re_percent = 1;
			continue;
		    }

		    if (!re.re_under && nc == '_') {
			xo_parse_warning(xpp,
			       "use hyphens, not underscores, "
				       "in field name: '%.*s'",
			       (int) nlen, np);
			re.re_under = 1;
		    }

		    if (!re.re_upper && isupper(nc)) {
			xo_parse_warning(xpp,
			       "field name should be lower case: '%.*s'",
			       (int) nlen, np);
			re.re_upper = 1;
		    }

		    /*
		     * Underscores and upper case are reported above,
		     * so skip them here
		     */
		    if (!re.re_invalid && !isdigit(nc)
			    && !(islower(nc) || isupper(nc))
 			    && nc != '-' && nc != '_') {
			xo_parse_warning(xpp,
			       "field name contains invalid character: '%.*s'",
			       (int) nlen, np);
			re.re_invalid = 1;
		    }
		}

		if (!re.re_percent && nlen <= 2) {
		    xo_parse_warning(xpp,
			   "field name should be longer than "
				   "two characters: '%.*s'",
			   (int) nlen, np);
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
			       "anchor content must be a decimal width: '%.*s'",
			       (int) nlen, np);
			break;
		    }
		}
		if (format && flen > 0) {
		    xo_parse_error(xpp,
			   "anchor cannot have both static "
				   "width and format: '%s'",
			   xo_printable(fmt));
		}
	    }
	    if (format && flen > 0) {
		/* Anchor width must be "%d" or numeric */
		if (flen != 2 || format[0] != '%' || format[1] != 'd') {
		    char *aep = NULL;
		    (void) strtol(format, &aep, 10);
		    if (aep != format + flen)
			xo_parse_error(xpp,
				       "anchor format must be '%%d': '%.*s'",
				       (int) flen, format);
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
}

int
xo_parse_format (xo_parse_t *xpp, const char *fmt)
{
    if (xpp == NULL || fmt == NULL)
	return -1;

    xo_parse_release(xpp);

    unsigned max_fields = xo_count_fields(xpp, fmt);
    size_t sz = max_fields * sizeof(xo_field_info_t);

    xo_field_info_t *fields = xo_parse_alloc(xpp, sz);
    if (fields == NULL) {
	xo_parse_error(xpp, "xo_parse_format: out of memory");
	return -1;
    }
    memset(fields, 0, sz);

    if (xo_parse_fields(xpp, fields, max_fields, fmt) < 0) {
	xo_parse_free(xpp, fields);
	return -1;
    }

    /* Count actual entries (array is zero-terminated via xfi_ftype) */
    unsigned n = 0;
    for (xo_field_info_t *xfip = fields; xfip->xfi_ftype; xfip++)
	n++;

    xpp->xp_fields = fields;
    xpp->xp_num_fields = n;
    return 0;
}
