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
#include <stdio.h>

#include "xo_format.h"
#include "xo_parse_shim.h"

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

int
xo_shim_parse (const char *fmt, xo_shim_error_t error, void *data)
{
    struct xo_shim_state ss = { error, data };
    xo_parse_t xpp = { 0 };
    xpp.xp_error      = shim_error_cb;
    xpp.xp_error_data = &ss;
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
    if (xfip->xfi_format == NULL)
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
            arg_cb(arg_data, xfip->xfi_format, (unsigned) xfip->xfi_flen);
    }

    xo_parse_release(&xpp);
    return 0;
}
