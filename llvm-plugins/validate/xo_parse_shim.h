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
 * xo_parse_shim.h: C++-safe bridge to xo_parse_format().
 *
 * xo_format.h and its transitive includes are not C++-safe (they use
 * 'private' as a parameter name and rely on void* implicit casts).
 * This header exposes only what the C++ plugin needs.
 */

#ifndef XO_PARSE_SHIM_H
#define XO_PARSE_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "xo_format.h"

/*
 * Error callback: receives a fully-formatted message (the va_list has
 * already been consumed by the shim).
 */
typedef void (*xo_shim_error_t)(void *data, const char *fmt, ...);

/*
 * Parse fmt for syntax errors only.  Calls error(data, msg) for each
 * problem found.  Returns 0 on success, -1 if the format is malformed.
 */
int xo_shim_parse(const char *fmt, xo_shim_error_t error, void *data);

/*
 * Argument descriptor: called once per va_arg the format string consumes.
 *
 * fmt / fmtlen  — the printf format spec ("%s", "%ld", etc.),
 *                 NOT NUL-terminated.  fmtlen > 0 in all cases.
 *
 * Special case: when a field uses the 'a' (XFF_ARGUMENT) modifier the
 * field name itself comes from va_arg as a const char *.  That arg is
 * reported with fmt == NULL / fmtlen == 0 BEFORE any value arg for the
 * same field.
 */
typedef void (*xo_shim_arg_cb_t)(void *data, const char *fmt, unsigned fmtlen);

/*
 * Parse fmt, calling error_cb for hard errors, warn_cb for style warnings
 * (e.g. "use lower case"), and arg_cb once per expected va_arg.
 * warn_cb / warn_data may be NULL, in which case warnings are silently dropped.
 * lint is non-zero to enable the minor, non-fatal lint-style warnings
 * (XPF_LINT); those are reported via warn_cb like any other warning.
 * Returns 0 on success, -1 on parse error.
 */
int xo_shim_parse_args(const char *fmt,
                        xo_shim_error_t error_cb, void *error_data,
                        xo_shim_error_t warn_cb,  void *warn_data,
                        xo_shim_arg_cb_t arg_cb,  void *arg_data,
                        xo_parse_flags_t flags);

/*
 * Offset-based field record: mirrors xo_field_info_t but uses only plain C
 * types so this header remains safe for C++ consumers.  Member names, order,
 * and integer widths match the real struct exactly; the C shim copies
 * field-by-field.
 */
typedef struct xo_shim_field_s {
    uint64_t xsf_flags;    /* xfi_flags (xo_xff_flags_t) */
    uint32_t xsf_ftype;    /* xfi_ftype */
    int16_t  xsf_start;    /* xfi_start */
    int16_t  xsf_content;  /* xfi_content */
    int16_t  xsf_format;   /* xfi_format */
    int16_t  xsf_encoding; /* xfi_encoding */
    int16_t  xsf_next;     /* xfi_next */
    int16_t  xsf_len;      /* xfi_len */
    int16_t  xsf_clen;     /* xfi_clen */
    int16_t  xsf_flen;     /* xfi_flen */
    int16_t  xsf_elen;     /* xfi_elen */
    uint32_t xsf_fnum;     /* xfi_fnum */
    uint32_t xsf_renum;    /* xfi_renum */
    uint16_t xsf_num_fspecs; /* xfi_num_fspecs: fspec_cb calls for this field */
} xo_shim_field_t;

typedef void (*xo_shim_field_cb_t)(void *data, const xo_shim_field_t *f);

/*
 * Offset-based fspec record: mirrors xo_fspec_t (the pre-parsed "%..."
 * conversion spec cached at xfi_fspecs).  Field names, order, and widths
 * match the real struct exactly; the C shim copies field-by-field.
 */
typedef struct xo_shim_fspec_s {
    uint8_t  xsp_fc;            /* xf_fc */
    uint8_t  xsp_lflag;         /* xf_lflag */
    uint8_t  xsp_hflag;         /* xf_hflag */
    uint8_t  xsp_jflag;         /* xf_jflag */
    uint8_t  xsp_tflag;         /* xf_tflag */
    uint8_t  xsp_zflag;         /* xf_zflag */
    uint8_t  xsp_qflag;         /* xf_qflag */
    uint8_t  xsp_seen_minus;    /* xf_seen_minus */
    int8_t   xsp_leading_zero;  /* xf_leading_zero */
    uint8_t  xsp_dots;          /* xf_dots */
    uint8_t  xsp_alt;           /* xf_alt */
    uint8_t  xsp_stars;         /* xf_stars */
    uint8_t  xsp_star[3];       /* xf_star[XF_WIDTH_NUM] */
    uint8_t  xsp_at_stars;      /* xf_at_stars */
    int16_t  xsp_width[3];      /* xf_width[XF_WIDTH_NUM] */
    uint16_t xsp_start;         /* xf_start */
    uint16_t xsp_len;           /* xf_len */
    uint16_t xsp_prefix_len;    /* xf_prefix_len */
    uint8_t  xsp_num_bits;	/* xf_num_bits */
    uint8_t  xsp_padding;	/* xf_padding */
} xo_shim_fspec_t;

typedef void (*xo_shim_fspec_cb_t)(void *data, const xo_shim_fspec_t *f);

/*
 * Parse fmt and call field_cb once per field with the offset-based field
 * descriptor.  Immediately after each field_cb call, fspec_cb is called
 * f->xsf_num_fspecs times, once per pre-parsed display-format element for
 * that field, before the next field_cb call.  fspec_cb/fspec_data may be
 * NULL to skip fspecs entirely.  Calls error_cb on syntax problems.
 * Returns 0 on success, -1 on parse error.
 */
int xo_shim_parse_fields(const char *fmt,
                          xo_shim_error_t error_cb, void *error_data,
                          xo_shim_field_cb_t field_cb, void *field_data,
                          xo_shim_fspec_cb_t fspec_cb, void *fspec_data);

#ifdef __cplusplus
}
#endif

#endif /* XO_PARSE_SHIM_H */
