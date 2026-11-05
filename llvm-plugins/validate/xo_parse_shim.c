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
 * The IR pass hardcodes the member lists of xo_field_info_t and
 * xo_fspec_t in its LLVM StructTypes (FieldTy and FspecTy in
 * xo_precompile.cc).  Offsets and padding aren't hardcoded: the pass
 * gets them from the target's DataLayout.  To check the member lists,
 * xo_shim_layout() reports the C offsets of every member as compiled on
 * the build host, along with the host facts the pass needs to lay out
 * its StructTypes for the host and compare.  So if anyone changes either
 * struct (adds a member, changes a type, reorders) without updating
 * xo_precompile.cc, the pass notices and leaves calls uncached.
 *
 * These widths are the same on every target, and FieldTy relies on them.
 */
_Static_assert(sizeof(xo_xff_flags_t)    == 8,
	       "xo_xff_flags_t must be 8 bytes");
_Static_assert(sizeof(xo_format_offset_t) == 2,
	       "xo_format_offset_t must be 2 bytes");

/*
 * The alignment a struct member really gets, which can be less than
 * _Alignof (see xo_pcv_align64_s).  C23 doesn't allow defining the
 * struct inside offsetof(), so each type gets a named one.
 */
struct xo_shim_align_ptr_s { char xa_pad; void *xa_value; };
struct xo_shim_align_i64_s { char xa_pad; uint64_t xa_value; };
struct xo_shim_align_i32_s { char xa_pad; uint32_t xa_value; };
struct xo_shim_align_i16_s { char xa_pad; uint16_t xa_value; };

#define XO_SHIM_ALIGN(_name) offsetof(struct xo_shim_align_ ## _name ## _s, \
				      xa_value)

void
xo_shim_layout (xo_shim_layout_t *xslp)
{
    static const uint32_t field_offsets[XO_SHIM_FIELD_MEMBERS] = {
	offsetof(xo_field_info_t, xfi_flags),
	offsetof(xo_field_info_t, xfi_ftype),
	offsetof(xo_field_info_t, xfi_start),
	offsetof(xo_field_info_t, xfi_content),
	offsetof(xo_field_info_t, xfi_format),
	offsetof(xo_field_info_t, xfi_encoding),
	offsetof(xo_field_info_t, xfi_next),
	offsetof(xo_field_info_t, xfi_len),
	offsetof(xo_field_info_t, xfi_clen),
	offsetof(xo_field_info_t, xfi_flen),
	offsetof(xo_field_info_t, xfi_elen),
	offsetof(xo_field_info_t, xfi_fnum),
	offsetof(xo_field_info_t, xfi_renum),
	offsetof(xo_field_info_t, xfi_fspecs),
	offsetof(xo_field_info_t, xfi_num_fspecs),
	offsetof(xo_field_info_t, xfi_padding),
    };
    static const uint32_t fspec_offsets[XO_SHIM_FSPEC_MEMBERS] = {
	offsetof(xo_fspec_t, xf_fc),
	offsetof(xo_fspec_t, xf_lflag),
	offsetof(xo_fspec_t, xf_hflag),
	offsetof(xo_fspec_t, xf_jflag),
	offsetof(xo_fspec_t, xf_tflag),
	offsetof(xo_fspec_t, xf_zflag),
	offsetof(xo_fspec_t, xf_qflag),
	offsetof(xo_fspec_t, xf_seen_minus),
	offsetof(xo_fspec_t, xf_leading_zero),
	offsetof(xo_fspec_t, xf_dots),
	offsetof(xo_fspec_t, xf_alt),
	offsetof(xo_fspec_t, xf_stars),
	offsetof(xo_fspec_t, xf_star),
	offsetof(xo_fspec_t, xf_at_stars),
	offsetof(xo_fspec_t, xf_width),
	offsetof(xo_fspec_t, xf_start),
	offsetof(xo_fspec_t, xf_len),
	offsetof(xo_fspec_t, xf_prefix_len),
	offsetof(xo_fspec_t, xf_num_bits),
	offsetof(xo_fspec_t, xf_arg_type),
	offsetof(xo_fspec_t, xf_plus),
	offsetof(xo_fspec_t, xf_space),
	offsetof(xo_fspec_t, xf_extflags),
    };
    const uint16_t one = 1;

    bzero(xslp, sizeof(*xslp));
    xslp->xsl_big_endian = (*(const uint8_t *) &one == 0);
    xslp->xsl_ptr_size = sizeof(void *);
    xslp->xsl_ptr_align = XO_SHIM_ALIGN(ptr);
    xslp->xsl_i64_align = XO_SHIM_ALIGN(i64);
    xslp->xsl_i32_align = XO_SHIM_ALIGN(i32);
    xslp->xsl_i16_align = XO_SHIM_ALIGN(i16);

    xslp->xsl_field_size = sizeof(xo_field_info_t);
    memcpy(xslp->xsl_field_offsets, field_offsets, sizeof(field_offsets));
    xslp->xsl_fspec_size = sizeof(xo_fspec_t);
    memcpy(xslp->xsl_fspec_offsets, fspec_offsets, sizeof(fspec_offsets));
}

struct xo_shim_state {
    xo_shim_error_t error;
    void           *data;
    const xo_parse_t *xpp;	/* Source of xp_err_offset */
};

/*
 * Forward a libxo parse error or warning, adding the offset of the
 * field libxo was parsing.  Messages often quote the whole format
 * string, so size the buffer to fit rather than truncating.
 */
static void
shim_error_cb (void *data, const char *fmt, ...)
{
    struct xo_shim_state *ss = data;
    if (ss->error == NULL)
        return;

    va_list vap;
    va_start(vap, fmt);
    int len = vsnprintf(NULL, 0, fmt, vap);
    va_end(vap);

    if (len < 0)
	return;

    char buf[len + 1];
    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);

    ss->error(ss->data, ss->xpp->xp_err_offset, "%s", buf);
}

/*
 * Stands in for a NULL error or warning callback, so the checks in
 * xo_shim_parse_args() can report without testing for one each time.
 */
static void
shim_ignore_cb (void *data, int offset, const char *fmt, ...)
{
    (void) data;
    (void) offset;
    (void) fmt;
}

/*
 * The offset of a field for diagnostics: its '{', when it has one.
 */
static int
shim_field_offset (const char *fmt, const xo_field_info_t *xfip)
{
    int off = xfip->xfi_start;

    if (off > 0 && fmt[off - 1] == '{')
	off -= 1;
    return off;
}

int
xo_shim_parse (const char *fmt, xo_shim_error_t error, void *data)
{
    xo_parse_t xpp = { 0 };
    struct xo_shim_state ss = { error, data, &xpp };
    xpp.xp_error = shim_error_cb;
    xpp.xp_error_data = &ss;
    xpp.xp_warn = shim_error_cb;
    xpp.xp_warn_data = &ss;
    xpp.xp_flags = XPF_STRICT;

    int rc = xo_parse_format(&xpp, fmt);
    xo_parse_release(&xpp);
    return rc;
}

/*
 * An int consumed by a '*' (a width, precision, or "%@...@" prefix) is
 * reported to callers as if it were written "%d".
 */
static const xo_fspec_t xo_shim_star_fspec = {
    .xf_fc = 'd',
    .xf_arg_type = XO_AT_INT,
    .xf_leading_zero = -1,
    .xf_width = { -1, -1, -1 },
    .xf_len = 2,
};

/*
 * Call arg_cb once for each va_arg consumed by the fspecs, in the order
 * xo_do_format_field() consumes them: the "%@...@" prefix's ints, then
 * the '*' widths, then the value itself.  Literal text and "%m" consume
 * nothing, so "{:type/ethernet}" makes no calls and "{:name/%s-%s-%s}"
 * makes three.
 */
static void
xo_shim_visit_args (const xo_fspec_t *xfp, int num,
                    xo_shim_arg_cb_t arg_cb, void *arg_data)
{
    int i, n, s;

    for (i = 0; i < num; i++, xfp++) {
        for (n = 0; n < xfp->xf_at_stars; n++)
            arg_cb(arg_data, &xo_shim_star_fspec);

        if (xfp->xf_arg_type == XO_AT_NONE)
            continue;

        for (s = 0; s < XF_WIDTH_NUM; s++) {
            if (xfp->xf_star[s])
                arg_cb(arg_data, &xo_shim_star_fspec);
        }

        arg_cb(arg_data, xfp);
    }
}

/*
 * Parse the format [ffmt, ffmt + flen) and call arg_cb for each va_arg
 * it consumes.  Parse errors are reported through xpp.  Returns the
 * number of fspecs, or < 0 when the format could not be parsed.
 */
static int
xo_shim_parse_args_format (xo_parse_t *xpp, const char *ffmt, unsigned flen,
                           xo_shim_arg_cb_t arg_cb, void *arg_data)
{
    /*
     * Every fspec (conversion or literal run) covers at least one
     * byte, so flen entries plus a terminator can never run out.
     * flen can reach 32K, making this about 1 MB, so it comes from
     * the heap, not the stack.
     */
    unsigned max_fspecs = flen + 1;
    size_t sz = max_fspecs * sizeof(xo_fspec_t);
    xo_fspec_t *fspecs = xo_realloc(NULL, sz);

    if (fspecs == NULL)
	return -1;
    bzero(fspecs, sz);

    xo_fspec_t *save_fspecs = xpp->xp_fspecs;
    xo_fspec_t *save_cur = xpp->xp_cur_fspec;
    unsigned save_num = xpp->xp_num_fspecs;

    xpp->xp_fspecs = xpp->xp_cur_fspec = fspecs;
    xpp->xp_num_fspecs = max_fspecs;

    int num = xo_parse_fspecs(xpp, ffmt, ffmt + flen);

    xpp->xp_fspecs = save_fspecs;
    xpp->xp_cur_fspec = save_cur;
    xpp->xp_num_fspecs = save_num;

    if (num > 0)
        xo_shim_visit_args(fspecs, num, arg_cb, arg_data);

    xo_free(fspecs);
    return num;
}

/*
 * Call arg_cb for each va_arg consumed by a field's display format,
 * using the fspecs xo_parse_format() already built when it has them.
 */
static void
xo_shim_field_args (xo_parse_t *xpp, const char *fmt,
                    const xo_field_info_t *xfip,
                    xo_shim_arg_cb_t arg_cb, void *arg_data)
{
    if (xfip->xfi_fspecs)
        xo_shim_visit_args(xfip->xfi_fspecs, xfip->xfi_num_fspecs,
                           arg_cb, arg_data);
    else
        xo_shim_parse_args_format(xpp, xo_foff(fmt, xfip->xfi_format),
                                  (unsigned) xfip->xfi_flen,
                                  arg_cb, arg_data);
}

/*
 * Return non-zero if this field consumes a va_arg for its value.
 *
 * Rule:
 *   V (value) - the content field is the key name; VALUE always from va_arg.
 *   C/D/E/L/N/P/T/U/W - content IS the display text; va_arg only when
 *                        content is absent (xfi_clen == 0) and format present.
 *   G - domain name; static content wins when given, otherwise the domain
 *       comes from va_arg via the format (see xo_set_gettext_domain()).
 *   [ / ] / TEXT / NEWLINE / EBRACE - never consume va_arg.
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
        return 0;

    case 'G':
        return (xfip->xfi_clen == 0);

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

/*
 * The XO_AT_* types of the va_args consumed by one format, used to
 * confirm the display and encoding formats consume the same arguments.
 */
typedef struct xo_shim_arg_types_s {
    uint8_t xsat_types[64];
    unsigned xsat_num;          /* Number of args (may exceed the array) */
} xo_shim_arg_types_t;

static void
xo_shim_record_arg_type (void *data, const xo_fspec_t *xfp)
{
    xo_shim_arg_types_t *xsatp = data;

    if (xsatp->xsat_num < sizeof(xsatp->xsat_types))
        xsatp->xsat_types[xsatp->xsat_num] = xfp->xf_arg_type;
    xsatp->xsat_num += 1;
}

int
xo_shim_parse_args (const char *fmt,
                     xo_shim_error_t error_cb, void *error_data,
                     xo_shim_error_t warn_cb,  void *warn_data,
                     xo_shim_arg_cb_t arg_cb,  void *arg_data,
                     xo_parse_flags_t flags)
{
    xo_parse_t xpp = { 0 };
    struct xo_shim_state ss_err  = { error_cb ?: shim_ignore_cb,
				     error_data, &xpp };
    struct xo_shim_state ss_warn = { warn_cb ?: shim_ignore_cb,
				     warn_data, &xpp };
    xpp.xp_error      = shim_error_cb;
    xpp.xp_error_data = &ss_err;
    xpp.xp_warn       = shim_error_cb;
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
	int off = shim_field_offset(fmt, xfip);

        /* XFF_ARGUMENT: field name/content comes from va_arg as const char * */
        if (xfip->xfi_flags & XFF_ARGUMENT)
            arg_cb(arg_data, NULL);

	else {
	    int no_name = (xfip->xfi_flags & XFF_DISPLAY_ONLY) != 0;
	    const char use_instead[] = "use 'F'/format role instead";

	    /*
	     * Enforce name/format restrictions.  A value with no name
	     * has nothing to be encoded under, so that is an error.
	     * With the 'display' flag the value is never encoded, so
	     * the empty name is harmless; it is common practice, but
	     * 'F' is the proper role, so it is only a lint warning.
	     */
	    if (strchr(XO_LINT_ROLES_NEEDING_NAME, ftype)
			&& xfip->xfi_clen == 0) {
		const char *role_name = xo_lookup_role_name(ftype);
		if (!no_name)
		    ss_err.error(ss_err.data, off,
				 "field role ('%c'%s%s) requires a non-empty "
				 "name: '%s'",
				 ftype, role_name ? "/" : "", role_name ?: "",
				 xo_parse_printable(&xpp, str, slen, 1));
		else if (flags & XPF_LINT)
		    ss_warn.error(ss_warn.data, off,
				 "value field ('%c'%s%s) has empty name, but "
				 "has the 'display' flag set; %s: '%s'",
				 ftype, role_name ? "/" : "", role_name ?: "",
				 use_instead,
				 xo_parse_printable(&xpp, str, slen, 1));
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
		ss_err.error(ss_err.data, off,
			     "field role ('%c'%s%s) requires a name or format: "
			     "'%s'",
			     ftype, role_name ? "/" : "", role_name ?: "",
			     xo_parse_printable(&xpp, str, slen, 1));
	    }

#ifdef XO_LINT_ROLES_NO_FORMAT
	    if (strchr(XO_LINT_ROLES_NO_FORMAT, ftype)
			&& xfip->xfi_format != XO_FOFF_NONE)
		ss_err.error(ss_err.data, off,
			     "field role ('%c') cannot have a "
			     "format specifier: '%s'",
			     ftype, xo_parse_printable(&xpp, str, slen, 1));
#endif /* XO_LINT_ROLES_NO_FORMAT */

            /*
             * A format on these roles is only meaningful when no static
             * content is given; content always wins at emit time (see
             * xo_set_gettext_domain()), so a format alongside content is
             * dead code, not a real error.
             */
            if (strchr(XO_LINT_ROLES_FORMAT_IGNORED_WITH_CONTENT, ftype)
                        && xfip->xfi_clen != 0
                        && xfip->xfi_format != XO_FOFF_NONE)
                ss_warn.error(ss_warn.data, off,
                             "field role ('%c') has both content and a "
                             "format; the format is ignored: '%s'",
                             ftype, xo_parse_printable(&xpp, str, slen, 1));
	}

        /*
         * For non-V roles with XFF_ARGUMENT (e.g. {La:}), the content IS
         * the va_arg already consumed above; no additional value arg.
         * For V role with XFF_ARGUMENT (e.g. {a:}), the name came from the
         * NULL callback above; the value still comes from the format spec.
         */
        int skip_value = (xfip->xfi_flags & XFF_ARGUMENT) && (ftype != 'V');

        /* Check that display and encoding formats consume the same args */
        if (!skip_value && xfip->xfi_encoding != XO_FOFF_NONE) {
            xo_shim_arg_types_t dargs = { 0 }, eargs = { 0 };
	    int erc;

            if (xfip->xfi_format != XO_FOFF_NONE)
                xo_shim_field_args(&xpp, fmt, xfip,
                                   xo_shim_record_arg_type, &dargs);

	    /*
	     * xo_parse_format() does not parse encoding formats, so
	     * their errors come from here; point them at this field.
	     * A bad one has been reported, and its count means nothing.
	     */
	    xpp.xp_err_offset = off;
            if (xfip->xfi_encoding >= 0)
                erc = xo_shim_parse_args_format(&xpp,
                                          xo_foff(fmt, xfip->xfi_encoding),
                                          (unsigned) xfip->xfi_elen,
                                          xo_shim_record_arg_type, &eargs);
            else
                erc = xo_shim_parse_args_format(&xpp, xo_default_format,
                                          (unsigned) strlen(xo_default_format),
                                          xo_shim_record_arg_type, &eargs);
	    xpp.xp_err_offset = -1;

	    if (erc < 0) {
		/* Already reported */
	    } else if (dargs.xsat_num != eargs.xsat_num)
                ss_err.error(ss_err.data, off,
                             "display and encoding formats consume "
                             "%u vs %u argument(s): '%s'",
                             dargs.xsat_num, eargs.xsat_num,
                             xo_parse_printable(&xpp, str, slen, 1));
            else if (memcmp(dargs.xsat_types, eargs.xsat_types,
                            sizeof(dargs.xsat_types)) != 0)
                ss_err.error(ss_err.data, off,
                             "display and encoding formats consume "
                             "different argument(s): '%s'",
                             xo_parse_printable(&xpp, str, slen, 1));
        }

        if (!skip_value && field_consumes_varg(xfip))
            xo_shim_field_args(&xpp, fmt, xfip, arg_cb, arg_data);
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
    xo_parse_t xpp = { 0 };
    struct xo_shim_state ss = { error_cb, error_data, &xpp };
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
            sf.xsp_arg_type     = xfp->xf_arg_type;
            sf.xsp_plus         = xfp->xf_plus;
            sf.xsp_space        = xfp->xf_space;
            sf.xsp_extflags     = xfp->xf_extflags;
            fspec_cb(fspec_data, &sf);
        }
    }

    xo_parse_release(&xpp);
    return 0;
}
