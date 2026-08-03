/*
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
 *   total: 40 bytes
 */
_Static_assert(sizeof(xo_xff_flags_t)    == 8,  "xo_xff_flags_t must be 8 bytes");
_Static_assert(sizeof(xo_format_offset_t) == 2, "xo_format_offset_t must be 2 bytes");
_Static_assert(sizeof(xo_field_info_t)   == 40, "xo_field_info_t size mismatch; update xo_precompile.cc FieldTy");
_Static_assert(offsetof(xo_field_info_t, xfi_flags)    ==  0, "xfi_flags offset");
_Static_assert(offsetof(xo_field_info_t, xfi_ftype)    ==  8, "xfi_ftype offset");
_Static_assert(offsetof(xo_field_info_t, xfi_start)    == 12, "xfi_start offset");
_Static_assert(offsetof(xo_field_info_t, xfi_content)  == 14, "xfi_content offset");
_Static_assert(offsetof(xo_field_info_t, xfi_format)   == 16, "xfi_format offset");
_Static_assert(offsetof(xo_field_info_t, xfi_encoding) == 18, "xfi_encoding offset");
_Static_assert(offsetof(xo_field_info_t, xfi_next)     == 20, "xfi_next offset");
_Static_assert(offsetof(xo_field_info_t, xfi_len)      == 22, "xfi_len offset");
_Static_assert(offsetof(xo_field_info_t, xfi_clen)     == 24, "xfi_clen offset");
_Static_assert(offsetof(xo_field_info_t, xfi_flen)     == 26, "xfi_flen offset");
_Static_assert(offsetof(xo_field_info_t, xfi_elen)     == 28, "xfi_elen offset");
_Static_assert(offsetof(xo_field_info_t, xfi_fnum)     == 32, "xfi_fnum offset");
_Static_assert(offsetof(xo_field_info_t, xfi_renum)    == 36, "xfi_renum offset");

struct xo_shim_state {
    xo_shim_error_t error;
    void           *data;
};

static void
shim_error_cb (void *data, const char *fmt, va_list vap)
{
    struct xo_shim_state *ss = data;
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, vap);
    ss->error(ss->data, buf);
}

static void
shim_warn_cb (void *data, const char *fmt, va_list vap)
{
    struct xo_shim_state *ss = data;
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, vap);
    ss->error(ss->data, buf);
}

int
xo_shim_parse (const char *fmt, xo_shim_error_t error, void *data)
{
    struct xo_shim_state ss = { error, data };
    xo_parse_t xpp = { 0 };
    xpp.xp_error      = shim_error_cb;
    xpp.xp_error_data = &ss;
    xpp.xp_warn       = shim_warn_cb;
    xpp.xp_warn_data = &ss;
    xpp.xp_flags      = XPF_STRICT;

    int rc = xo_parse_format(&xpp, fmt);
    xo_parse_release(&xpp);
    return rc;
}

/*
 * Return non-zero if this field consumes a va_arg for its value.
 *
 * Rule:
 *   V (value) — the content field is the key name; VALUE always from va_arg.
 *   L/D/N/P/T/U/W/E — the content IS the display text; va_arg only when
 *                      content is absent (xfi_clen == 0).
 *   G / C / [ / ] / TEXT / NEWLINE / EBRACE — never consume va_arg.
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
    case 'C':
        return 0;

    case '[':
    case ']':
        /* an explicit format (must be %d) means the width comes from va_arg */
        return 1;

    case 'V':
        return 1;

    default:
        return (xfip->xfi_clen == 0);
    }
}

int
xo_shim_parse_args (const char *fmt,
                     xo_shim_error_t error_cb, void *error_data,
                     xo_shim_arg_cb_t arg_cb,   void *arg_data)
{
    struct xo_shim_state ss = { error_cb, error_data };
    xo_parse_t xpp = { 0 };
    xpp.xp_error      = shim_error_cb;
    xpp.xp_error_data = &ss;
    xpp.xp_flags      = XPF_STRICT;

    if (xo_parse_format(&xpp, fmt) < 0) {
        xo_parse_release(&xpp);
        return -1;
    }

    for (unsigned i = 0; i < xpp.xp_num_fields; i++) {
        const xo_field_info_t *xfip = &xpp.xp_fields[i];

        /* XFF_ARGUMENT: field name itself comes from va_arg as const char * */
        if (xfip->xfi_flags & XFF_ARGUMENT)
            arg_cb(arg_data, NULL, 0);

        if (field_consumes_varg(xfip))
            arg_cb(arg_data, xo_foff(fmt, xfip->xfi_format),
		   (unsigned) xfip->xfi_flen);
    }

    xo_parse_release(&xpp);
    return 0;
}

int
xo_shim_parse_fields (const char *fmt,
                       xo_shim_error_t error_cb, void *error_data,
                       xo_shim_field_cb_t field_cb, void *field_data)
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
        f.xsf_flags    = xfip->xfi_flags;
        f.xsf_ftype    = xfip->xfi_ftype;
        f.xsf_start    = xfip->xfi_start;
        f.xsf_content  = xfip->xfi_content;
        f.xsf_format   = xfip->xfi_format;
        f.xsf_encoding = xfip->xfi_encoding;
        f.xsf_next     = xfip->xfi_next;
        f.xsf_len      = xfip->xfi_len;
        f.xsf_clen     = xfip->xfi_clen;
        f.xsf_flen     = xfip->xfi_flen;
        f.xsf_elen     = xfip->xfi_elen;
        f.xsf_fnum     = xfip->xfi_fnum;
        f.xsf_renum    = xfip->xfi_renum;
        field_cb(field_data, &f);
    }

    xo_parse_release(&xpp);
    return 0;
}
