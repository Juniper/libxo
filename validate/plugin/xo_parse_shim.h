/*
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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Error callback: receives a fully-formatted message (the va_list has
 * already been consumed by the shim).
 */
typedef void (*xo_shim_error_t)(void *data, const char *msg);

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
 * Parse fmt, calling error_cb for syntax problems and arg_cb once per
 * expected va_arg.  Returns 0 on success, -1 on parse error.
 */
int xo_shim_parse_args(const char *fmt,
                        xo_shim_error_t error_cb, void *error_data,
                        xo_shim_arg_cb_t arg_cb,   void *arg_data);

/*
 * Offset-based field record: mirrors xo_field_info_t but uses only plain C
 * types so this header remains safe for C++ consumers.  Member names, order,
 * and integer widths match the real struct exactly; the C shim copies
 * field-by-field.
 */
typedef struct xo_shim_field_s {
    unsigned long long xsf_flags;   /* xfi_flags (xo_xff_flags_t) */
    unsigned           xsf_ftype;   /* xfi_ftype */
    short xsf_start;                /* xfi_start */
    short xsf_content;              /* xfi_content */
    short xsf_format;               /* xfi_format */
    short xsf_encoding;             /* xfi_encoding */
    short xsf_next;                 /* xfi_next */
    short xsf_len;                  /* xfi_len */
    short xsf_clen;                 /* xfi_clen */
    short xsf_flen;                 /* xfi_flen */
    short xsf_elen;                 /* xfi_elen */
    unsigned xsf_fnum;              /* xfi_fnum */
    unsigned xsf_renum;             /* xfi_renum */
} xo_shim_field_t;

typedef void (*xo_shim_field_cb_t)(void *data, const xo_shim_field_t *f);

/*
 * Parse fmt and call field_cb once per field with the offset-based field
 * descriptor.  Calls error_cb on syntax problems.
 * Returns 0 on success, -1 on parse error.
 */
int xo_shim_parse_fields(const char *fmt,
                          xo_shim_error_t error_cb, void *error_data,
                          xo_shim_field_cb_t field_cb, void *field_data);

#ifdef __cplusplus
}
#endif

#endif /* XO_PARSE_SHIM_H */
