/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, 2025
 *
 * xo_parse_shim.c: compiled as C so it can include the C-only libxo headers.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "xo_format.h"
#include "xo_parse_shim.h"

/*
 * Compile-time verification that xo_field_info_t has the layout the IR pass
 * hardcodes in its LLVM StructType.  If anyone changes the struct (adds a
 * member, changes a type, reorders) without updating xo_precompile.cc, one
 * of these assertions will fire at shim build time.
 *
 * Expected layout (LP64, same target as the C compiler and LLVM DataLayout):
 *   offset  0: uint64_t xfi_flags    (i64)
 *   offset  8: uint32_t xfi_ftype    (i32)
 *   offset 12: int16_t  xfi_start    (i16)
 *   offset 14: int16_t  xfi_content  (i16)
 *   offset 16: int16_t  xfi_format   (i16)
 *   offset 18: int16_t  xfi_encoding (i16)
 *   offset 20: int16_t  xfi_next     (i16)
 *   offset 22: int16_t  xfi_len      (i16)
 *   offset 24: int16_t  xfi_clen     (i16)
 *   offset 26: int16_t  xfi_flen     (i16)
 *   offset 28: int16_t  xfi_elen     (i16)
 *   [2-byte pad at offset 30 — inserted by compiler to align xfi_fnum]
 *   offset 32: uint32_t xfi_fnum     (i32)
 *   offset 36: uint32_t xfi_renum    (i32)
 *   offset 40: pointer xfi_cachep    (pointer)
 *   total: 48 bytes (for a 64-bit pointer
 */
_Static_assert(sizeof(xo_xff_flags_t)    == 8,
	       "xo_xff_flags_t must be 8 bytes");
_Static_assert(sizeof(xo_format_offset_t) == 2,
	       "xo_format_offset_t must be 2 bytes");
_Static_assert(sizeof(xo_field_info_t)   == 48 + sizeof(void *),
       "xo_field_info_t size mismatch; update xo_precompile.cc FieldTy");

_Static_assert(offsetof(xo_field_info_t, xfi_flags)    ==  0,
	       "xfi_flags offset");
_Static_assert(offsetof(xo_field_info_t, xfi_ftype)    ==  8,
	       "xfi_ftype offset");
_Static_assert(offsetof(xo_field_info_t, xfi_start)    == 12,
	       "xfi_start offset");
_Static_assert(offsetof(xo_field_info_t, xfi_content)  == 14,
	       "xfi_content offset");
_Static_assert(offsetof(xo_field_info_t, xfi_format)   == 16,
	       "xfi_format offset");
_Static_assert(offsetof(xo_field_info_t, xfi_encoding) == 18,
	       "xfi_encoding offset");
_Static_assert(offsetof(xo_field_info_t, xfi_next)     == 20,
	       "xfi_next offset");
_Static_assert(offsetof(xo_field_info_t, xfi_len)      == 22,
	       "xfi_len offset");
_Static_assert(offsetof(xo_field_info_t, xfi_clen)     == 24,
	       "xfi_clen offset");
_Static_assert(offsetof(xo_field_info_t, xfi_flen)     == 26,
	       "xfi_flen offset");
_Static_assert(offsetof(xo_field_info_t, xfi_elen)     == 28,
	       "xfi_elen offset");
_Static_assert(offsetof(xo_field_info_t, xfi_fnum)     == 32,
	       "xfi_fnum offset");
_Static_assert(offsetof(xo_field_info_t, xfi_renum)    == 36,
	       "xfi_renum offset");
_Static_assert(offsetof(xo_field_info_t, xfi_fspecs)   == 40,
	       "xfi_fspecs offset");
_Static_assert(offsetof(xo_field_info_t, xfi_num_fspecs) == 40 + sizeof(void *),
	       "xfi_num_fspecs offset");

/*
 * Same protection for xo_fspec_t, mirrored in xo_precompile.cc's FspecTy.
 * Expected layout (LP64):
 *   offset  0: uint8_t xf_fc .. xf_stars (8 flag bytes, then leading_zero,
 *              dots, alt, stars — 12 individual uint8_t members)
 *   offset 12: uint8_t xf_star[3]
 *   offset 15: uint8_t xf_at_stars
 *   offset 16: int16_t xf_width[3]
 *   offset 22: uint16_t xf_start
 *   offset 24: uint16_t xf_len
 *   offset 26: uint16_t xf_prefix_len
 *   offset 28: uint16_t xf_num_bits, padding
 *   total: 30 bytes
 */
_Static_assert(sizeof(xo_fspec_t) == 30,
	       "xo_fspec_t size mismatch; update xo_precompile.cc FspecTy");
_Static_assert(offsetof(xo_fspec_t, xf_fc)           ==  0, "xf_fc offset");
_Static_assert(offsetof(xo_fspec_t, xf_lflag)        ==  1, "xf_lflag offset");
_Static_assert(offsetof(xo_fspec_t, xf_hflag)        ==  2, "xf_hflag offset");
_Static_assert(offsetof(xo_fspec_t, xf_jflag)        ==  3, "xf_jflag offset");
_Static_assert(offsetof(xo_fspec_t, xf_tflag)        ==  4, "xf_tflag offset");
_Static_assert(offsetof(xo_fspec_t, xf_zflag)        ==  5, "xf_zflag offset");
_Static_assert(offsetof(xo_fspec_t, xf_qflag)        ==  6, "xf_qflag offset");
_Static_assert(offsetof(xo_fspec_t, xf_seen_minus)   ==  7, "xf_seen_minus offset");
_Static_assert(offsetof(xo_fspec_t, xf_leading_zero) ==  8, "xf_leading_zero offset");
_Static_assert(offsetof(xo_fspec_t, xf_dots)         ==  9, "xf_dots offset");
_Static_assert(offsetof(xo_fspec_t, xf_alt)          == 10, "xf_alt offset");
_Static_assert(offsetof(xo_fspec_t, xf_stars)        == 11, "xf_stars offset");
_Static_assert(offsetof(xo_fspec_t, xf_star)         == 12, "xf_star offset");
_Static_assert(offsetof(xo_fspec_t, xf_at_stars)     == 15, "xf_at_stars offset");
_Static_assert(offsetof(xo_fspec_t, xf_width)        == 16, "xf_width offset");
_Static_assert(offsetof(xo_fspec_t, xf_start)        == 22, "xf_start offset");
_Static_assert(offsetof(xo_fspec_t, xf_len)          == 24, "xf_len offset");
_Static_assert(offsetof(xo_fspec_t, xf_prefix_len)   == 26, "xf_prefix_len offset");
_Static_assert(offsetof(xo_fspec_t, xf_num_bits  )   == 28, "xf_num_bits offset");

struct xo_shim_state {
    xo_shim_error_t error;
    void           *data;
};

static void
shim_error_cb (void *data, const char *fmt, ...)
{
    struct xo_shim_state *ss = data;
    char buf[512];
    va_list vap;
    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);
    ss->error(ss->data, "%s", buf);
}

static void
shim_warn_cb (void *data, const char *fmt, ...)
{
    struct xo_shim_state *ss = data;
    if (ss->error == NULL)
        return;
    char buf[512];
    va_list vap;
    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);
    ss->error(ss->data, "%s", buf);
}

int
xo_shim_parse (const char *fmt, xo_shim_error_t error, void *data)
{
    struct xo_shim_state ss = { error, data };
    xo_parse_t xpp = { 0 };
    xpp.xp_error = shim_error_cb;
    xpp.xp_error_data = &ss;
    xpp.xp_warn = shim_warn_cb;
    xpp.xp_warn_data = &ss;
    xpp.xp_flags = XPF_STRICT;

    int rc = xo_parse_format(&xpp, fmt);
    xo_parse_release(&xpp);
    return rc;
}

/*
 * Scan a printf-style format string of length flen for %...conv sequences
 * and call arg_cb once per conversion.  %% is skipped (not a va_arg).
 * Fields with no % spec produce zero calls, correctly handling static content
 * like "{:type/ethernet}".  Fields with multiple specs like "%s-%s-%s" produce
 * one call per spec, correctly handling cases like "{:name/%s-%s-%s}".
 */
static void
scan_format_args (const char *field_fmt, unsigned flen,
                  xo_shim_arg_cb_t arg_cb, void *arg_data)
{
    const char *p = field_fmt, *end = field_fmt + flen;

    while (p < end) {
        if (*p != '%') {
	    p += 1;
	    continue;
	}

        const char *spec = p++;
        if (p >= end)
	    break;

        if (*p == '%') {  /* literal: "%%" */
	    p += 1;
	    continue;
	}

        /*
         * "%@...@" is an XO-specific prefix: each '*' between the two
         * '@'s marks an int arg that must be consumed and discarded
         * before the real conversion's own args are pulled (see
         * xo_parse_one_format() in xo_format.c).  Record one int arg
         * per '*', then treat the closing '@' as the pseudo '%' and
         * keep parsing the rest of the spec from there.
         */
        if (*p == '@') {
            for (p += 1; p < end && *p != '@'; p++) {
                if (*p == '*')
                    arg_cb(arg_data, "%d", 2);
            }
            if (p < end)
                p += 1;  /* skip the closing '@' (pseudo '%') */
        }

        /* flags */
        while (p < end && (*p == '-' || *p == '+' || *p == ' '
			   || *p == '0' || *p == '#' || *p == '\''))
            p += 1;

        /* width: digits or '*' (the '*' consumes one int va_arg) */
        if (p < end && *p == '*') {
            arg_cb(arg_data, "%d", 2);
            p += 1;

        } else {
            while (p < end && isdigit((int) (unsigned char) *p))
                p += 1;
        }

        /*
         * Groups 2 and 3 of libxo's three width groups are both '.'-prefixed:
         *   %*.*.*s → width(*), columns(.*), bytes(.*), value
         * The loop handles any number of '.' groups, each with optional '*'.
         */
        while (p < end && *p == '.') {
            p += 1;

            if (p < end && *p == '*') {
                arg_cb(arg_data, "%d", 2);
                p += 1;

            } else {
                while (p < end && isdigit((unsigned char)*p))
                    p += 1;
            }
        }
        /* length modifiers */
        while (p < end && (*p == 'l' || *p == 'h' || *p == 'L' ||
                            *p == 'z' || *p == 't' || *p == 'j' || *p == 'q'))
            p += 1;

        /* conversion character */
        if (p >= end)
	    break;

        if (*p == 'm') {  /* %m uses errno, no va_arg */
	    p += 1;
	    continue;
	}
        p += 1;

        arg_cb(arg_data, spec, (unsigned)(p - spec));
    }
}

/*
 * Return non-zero if this field consumes a va_arg for its value.
 *
 * Rule:
 *   V (value) — the content field is the key name; VALUE always from va_arg.
 *   C/D/E/L/N/P/T/U/W — content IS the display text; va_arg only when
 *                        content is absent (xfi_clen == 0) and format present.
 *   G / [ / ] / TEXT / NEWLINE / EBRACE — never consume va_arg (G is
 *                        forbidden from having a format by XO_LINT_ROLES_NO_FORMAT).
 */
static int
field_consumes_varg (const xo_field_info_t *xfip)
{
    if (xfip->xfi_format == XO_FOFF_NONE)
        return 0;

    switch ((int) xfip->xfi_ftype) {
    case XO_ROLE_TEXT:
    case XO_ROLE_NEWLINE:
    case XO_ROLE_EBRACE:
    case 'G':
        return 0;

    case '[':
    case ']':
        /* an explicit format (must be %d) means the width comes from va_arg */
        return 1;

    case 'V':
        return 1;

    case 'C':
        return (xfip->xfi_flen > 0);

    default:
        return (xfip->xfi_clen == 0);
    }
}

typedef struct arg_record_s {
    char ar_data[64];		/* Record */
    int ar_cur;
} arg_record_t;

static void
arg_record_cb (void *data, const char *fmt, unsigned fmtlen)
{
    arg_record_t *arp = data;

    if (fmtlen > 1 && arp->ar_cur < (int) sizeof(arp->ar_data) - 1)
	arp->ar_data[arp->ar_cur++] = fmt[fmtlen - 1];
}

static unsigned
count_format_args (const char *field_fmt, unsigned flen, arg_record_t *arp)
{
    bzero(arp, sizeof(*arp));

    scan_format_args(field_fmt, flen, arg_record_cb, arp);

    return arp->ar_cur;
}

int
xo_shim_parse_args (const char *fmt,
                     xo_shim_error_t error_cb, void *error_data,
                     xo_shim_error_t warn_cb,  void *warn_data,
                     xo_shim_arg_cb_t arg_cb,  void *arg_data,
                     xo_parse_flags_t flags)
{
    struct xo_shim_state ss_err  = { error_cb, error_data };
    struct xo_shim_state ss_warn = { warn_cb, warn_data };
    xo_parse_t xpp = { 0 };
    xpp.xp_error      = shim_error_cb;
    xpp.xp_error_data = &ss_err;
    xpp.xp_warn       = shim_warn_cb;
    xpp.xp_warn_data  = &ss_warn;
    xpp.xp_flags      = flags;

    if (xo_parse_format(&xpp, fmt) < 0) {
        xo_parse_release(&xpp);
        return -1;
    }

    for (unsigned i = 0; i < xpp.xp_num_fields; i++) {
        const xo_field_info_t *xfip = &xpp.xp_fields[i];
        int ftype = (int) xfip->xfi_ftype;
	int slen = (xfip->xfi_len > 0) ? xfip->xfi_len : 0;
	const char *str = xo_foff(fmt, xfip->xfi_start);

        /* XFF_ARGUMENT: field name/content comes from va_arg as const char * */
        if (xfip->xfi_flags & XFF_ARGUMENT)
            arg_cb(arg_data, NULL, 0);

	else {
	    int no_name = (xfip->xfi_flags & XFF_DISPLAY_ONLY) != 0;
	    const char use_instead[] = "use 'F'/format role instead";

	    /* Enforce name/format restrictions */
	    if ((flags & XPF_LINT) && strchr(XO_LINT_ROLES_NEEDING_NAME, ftype)
			&& xfip->xfi_clen == 0) {
		const char *role_name = xo_lookup_role_name(ftype);
		if (no_name)
		    ss_err.error(ss_err.data,
				 "value field ('%c'%s%s) has empty name, but "
				 "has the 'display' flag set; %s: '%s'",
				 ftype, role_name ? "/" : "", role_name ?: "",
				 use_instead, xo_printable2(str, slen, 1));
		else 
		    ss_err.error(ss_err.data,
				 "field role ('%c'%s%s) requires a non-empty "
				 "name: '%s'",
				 ftype, role_name ? "/" : "", role_name ?: "",
				 xo_printable2(str, slen, 1));
	    }

	    /*
	     * xfi_format >= 0 means an explicit format was written in the
	     * format string.  XO_FOFF_DEFAULT (-2) is an implicit "%s"
	     * added by the parser; XO_FOFF_NONE (-1) means no format at all.
	     * Only error when the user wrote neither content nor format.
	     */
	    if (strchr(XO_LINT_ROLES_NEEDING_NAME_OR_FORMAT, ftype)
		    && xfip->xfi_clen == 0 && xfip->xfi_format < 0) {
		const char *role_name = xo_lookup_role_name(ftype);
		ss_err.error(ss_err.data,
			     "field role ('%c'%s%s) requires a name or format: "
			     "'%s'",
			     ftype, role_name ? "/" : "", role_name ?: "",
			     xo_printable2(str, slen, 1));
	    }

	    if (strchr(XO_LINT_ROLES_NO_FORMAT, ftype)
			&& xfip->xfi_format != XO_FOFF_NONE)
		ss_err.error(ss_err.data,
			     "field role ('%c') cannot have a "
			     "format specifier: '%s'",
			     ftype, xo_printable2(str, slen, 1));
	}

        /*
         * For non-V roles with XFF_ARGUMENT (e.g. {La:}), the content IS
         * the va_arg already consumed above; no additional value arg.
         * For V role with XFF_ARGUMENT (e.g. {a:}), the name came from the
         * NULL callback above; the value still comes from the format spec.
         */
        int skip_value = (xfip->xfi_flags & XFF_ARGUMENT) && (ftype != 'V');

        /* Check that display and encoding formats consume the same arg count */
        if (!skip_value && xfip->xfi_encoding != XO_FOFF_NONE) {
            const char *dfmt, *efmt;
            unsigned dlen, elen;

            if (xfip->xfi_format >= 0) {
                dfmt = xo_foff(fmt, xfip->xfi_format);
                dlen = (unsigned) xfip->xfi_flen;
            } else if (xfip->xfi_format == XO_FOFF_DEFAULT) {
                dfmt = xo_default_format;
                dlen = (unsigned) strlen(xo_default_format);
            } else {
                dfmt = "";
                dlen = 0;
            }

            if (xfip->xfi_encoding >= 0) {
                efmt = xo_foff(fmt, xfip->xfi_encoding);
                elen = (unsigned) xfip->xfi_elen;
            } else {
                efmt = xo_default_format;
                elen = (unsigned) strlen(xo_default_format);
            }

	    arg_record_t dargs, eargs;
            unsigned dc = count_format_args(dfmt, dlen, &dargs);
            unsigned ec = count_format_args(efmt, elen, &eargs);
            if (dc != ec)
                ss_err.error(ss_err.data,
                    "display and encoding formats consume "
			     "%u vs %u argument(s): '%s'",
			     dc, ec, xo_printable2(str, slen, 1));
            else if (strcmp(dargs.ar_data, eargs.ar_data) != 0)
                ss_err.error(ss_err.data,
                    "display and encoding formats consume different "
			     "argument(s): '%s'",
			     xo_printable2(str, slen, 1));
        }

        if (!skip_value && field_consumes_varg(xfip))
            scan_format_args(xo_foff(fmt, xfip->xfi_format),
                             (unsigned) xfip->xfi_flen,
                             arg_cb, arg_data);
    }

    xo_parse_release(&xpp);
    return 0;
}

int
xo_shim_parse_fields (const char *fmt,
                       xo_shim_error_t error_cb, void *error_data,
                       xo_shim_field_cb_t field_cb, void *field_data,
                       xo_shim_fspec_cb_t fspec_cb, void *fspec_data)
{
    struct xo_shim_state ss = { error_cb, error_data };
    xo_parse_t xpp = { 0 };
    xpp.xp_error      = shim_error_cb;
    xpp.xp_error_data = &ss;

    if (xo_parse_format(&xpp, fmt) < 0) {
        xo_parse_release(&xpp);
        return -1;
    }

    for (unsigned i = 0; i < xpp.xp_num_fields; i++) {
        const xo_field_info_t *xfip = &xpp.xp_fields[i];
        xo_shim_field_t f;
        f.xsf_flags       = xfip->xfi_flags;
        f.xsf_ftype       = xfip->xfi_ftype;
        f.xsf_start       = xfip->xfi_start;
        f.xsf_content     = xfip->xfi_content;
        f.xsf_format      = xfip->xfi_format;
        f.xsf_encoding    = xfip->xfi_encoding;
        f.xsf_next        = xfip->xfi_next;
        f.xsf_len         = xfip->xfi_len;
        f.xsf_clen        = xfip->xfi_clen;
        f.xsf_flen        = xfip->xfi_flen;
        f.xsf_elen        = xfip->xfi_elen;
        f.xsf_fnum        = xfip->xfi_fnum;
        f.xsf_renum       = xfip->xfi_renum;
        f.xsf_num_fspecs  = xfip->xfi_fspecs ? xfip->xfi_num_fspecs : 0;
        field_cb(field_data, &f);

        if (fspec_cb == NULL)
            continue;

        for (unsigned j = 0; j < f.xsf_num_fspecs; j++) {
            const xo_fspec_t *xfp = &xfip->xfi_fspecs[j];
            xo_shim_fspec_t sf;
            sf.xsp_fc           = xfp->xf_fc;
            sf.xsp_lflag        = xfp->xf_lflag;
            sf.xsp_hflag        = xfp->xf_hflag;
            sf.xsp_jflag        = xfp->xf_jflag;
            sf.xsp_tflag        = xfp->xf_tflag;
            sf.xsp_zflag        = xfp->xf_zflag;
            sf.xsp_qflag        = xfp->xf_qflag;
            sf.xsp_seen_minus   = xfp->xf_seen_minus;
            sf.xsp_leading_zero = xfp->xf_leading_zero;
            sf.xsp_dots         = xfp->xf_dots;
            sf.xsp_alt          = xfp->xf_alt;
            sf.xsp_stars        = xfp->xf_stars;
            sf.xsp_star[0]      = xfp->xf_star[0];
            sf.xsp_star[1]      = xfp->xf_star[1];
            sf.xsp_star[2]      = xfp->xf_star[2];
            sf.xsp_at_stars     = xfp->xf_at_stars;
            sf.xsp_width[0]     = xfp->xf_width[0];
            sf.xsp_width[1]     = xfp->xf_width[1];
            sf.xsp_width[2]     = xfp->xf_width[2];
            sf.xsp_start        = xfp->xf_start;
            sf.xsp_len          = xfp->xf_len;
            sf.xsp_prefix_len   = xfp->xf_prefix_len;
            sf.xsp_num_bits     = xfp->xf_num_bits;
            sf.xsp_padding      = xfp->xf_padding;
            fspec_cb(fspec_data, &sf);
        }
    }

    xo_parse_release(&xpp);
    return 0;
}
