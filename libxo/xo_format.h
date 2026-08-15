/*
 * Copyright (c) 2014-2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, 2025
 *
 * xo_format.h: libxo format-string parsing, factored out so it can be
 * linked into tools (e.g. LLVM plugins) that do not carry the full
 * libxo runtime.
 */

#ifndef XO_FORMAT_H
#define XO_FORMAT_H

#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * xo.h provides: xo_realloc_func_t, xo_free_func_t, xo_xff_flags_t, XFF_*,
 * xo_format_offset_t, XO_FOFF_*, XO_FORMAT_MAX, XO_ROLE_*,
 * xo_default_format, xo_foff(), xo_field_info_t.
 */
#include "xo.h"

/*
 * Maximum number of fields in a format string.  Override at compile
 * time if needed.
 */
#ifndef XO_MAX_FIELDS
#define XO_MAX_FIELDS (8 * 1024)
#endif /* XO_MAX_FIELDS */

/*
 * Simple name→value table used for role and modifier lookup.
 * Kept here so callers (e.g. xo_set_options) can define their own tables
 * and reuse xo_name_lookup().
 */
typedef struct xo_flag_mapping_s {
    xo_xof_flags_t xm_value;
    const char *xm_name;
} xo_flag_mapping_t;

/*
 * Parsed representation of one field descriptor from a libxo format string.
 * All string members are xo_format_offset_t values — byte offsets into the
 * "base" format string from which the field was parsed.  Use xo_foff(base, off)
 * to recover a const char *.  XO_FOFF_NONE (-1) means absent; xfi_format may
 * additionally take XO_FOFF_DEFAULT (-2) to indicate the default "%s" format.
 *
 * This struct is exposed publicly so callers can populate pre-built const
 * field tables for xo_emit_cached().  The layout is stable within a given
 * XO_EMIT_CACHE_VERSION; bump the version whenever the layout changes.
 */
typedef struct xo_field_info_s {
    xo_xff_flags_t xfi_flags;		/* Modifier flags (XFF_*) */
    uint32_t xfi_ftype;			/* Role character ('V','L','G', XO_ROLE_*) */
    xo_format_offset_t xfi_start;	/* Offset of field start in base string */
    xo_format_offset_t xfi_content;	/* Offset of content (name) */
    xo_format_offset_t xfi_format;	/* Offset of display format (or XO_FOFF_DEFAULT) */
    xo_format_offset_t xfi_encoding;	/* Offset of encoding format */
    xo_format_offset_t xfi_next;	/* Offset just past this field */
    xo_format_offset_t xfi_len;		/* Length of whole field descriptor */
    xo_format_offset_t xfi_clen;	/* Length of content */
    xo_format_offset_t xfi_flen;	/* Length of format */
    xo_format_offset_t xfi_elen;	/* Length of encoding */
    uint32_t xfi_fnum;			/* Field number (0 = unset) */
    uint32_t xfi_renum;			/* Reordered field number (0 = none) */
} xo_field_info_t;

/*
 * Build-time pre-parsed format string cache.
 *
 * xo_emit_cached() is the target of the LLVM IR pass: it takes a pointer to
 * a pre-built xo_format_cache_t (holding a const xo_field_info_t[] parsed at
 * compile time) plus the original format string (kept for the gettext path
 * and as a version-mismatch fallback).
 *
 * If the cache version does not match XO_EMIT_CACHE_VERSION, or if the cache
 * pointer is NULL, the call silently falls back to parsing fmt at runtime.
 *
 * xo_field_info_t is defined above; callers may populate xfc_fields[] directly
 * (e.g. as a static const array) using the XFF_* flags, XO_FOFF_* sentinels,
 * and XO_ROLE_* constants defined above.
 */
#define XO_EMIT_CACHE_VERSION 1  /* bump on any xo_field_info_t layout change */

struct xo_format_cache_s {
    unsigned xfc_version;		/* == XO_EMIT_CACHE_VERSION */
    unsigned xfc_num_fields;
    const xo_field_info_t *xfc_fields;	/* const xo_field_info_t[] */
};

/*
 * Error callback.  Called when xo_parse_format() encounters a problem.
 * The fmt/... are a printf-style message.
 */
typedef void (*xo_parse_error_func_t)(void *data, const char *fmt, ...);

/*
 * Parse context.  The caller fills in the callback fields; xo_parse_format()
 * fills in xp_fields and xp_num_fields.
 *
 * Callback fields that are NULL fall back to the system defaults (realloc,
 * free, silent error reporting).
 */
/* Flags for xo_parse_t.xp_flags */
#define XPF_STRICT	(1<<0)	/* Enable lint-style semantic checks */

typedef struct xo_parse_s {
    xo_realloc_func_t xp_realloc;	/* Allocator (NULL → realloc) */
    xo_free_func_t xp_free;		/* Free (NULL → free) */
    xo_parse_error_func_t xp_error;	/* Error reporter (NULL → silent) */
    void *xp_error_data;		/* Opaque data passed to xp_error */
    xo_parse_error_func_t xp_warn;	/* Warning reporter (NULL → silent) */
    void *xp_warn_data;			/* Opaque data passed to xp_warn */
    unsigned xp_flags;			/* XPF_* flags */

    /* Output — filled in by xo_parse_format() */
    xo_field_info_t *xp_fields;	/* Allocated, zero-terminated field array */
    unsigned xp_num_fields;		/* Number of valid entries */
} xo_parse_t;

/* The default printf-style format used when a field has no explicit format. */
extern const char xo_default_format[];

/*
 * Parse the libxo format string fmt.  Allocates xpp->xp_fields via
 * xpp->xp_realloc and sets xpp->xp_num_fields.  Any previous xp_fields
 * value is released first.
 *
 * Returns 0 on success, -1 on error (error already reported via xp_error).
 */
int xo_parse_format(xo_parse_t *xpp, const char *fmt);

/*
 * Free xpp->xp_fields (using xpp->xp_free) and zero the output fields.
 */
void xo_parse_release(xo_parse_t *xpp);

/*
 * Parse role and modifier characters from the prefix of a single field
 * descriptor (the part before the ':'). basep points just past the opening
 * '{'. On success returns a pointer to the ':' or '/' or '}'; on error
 * returns NULL (error already reported via xpp->xp_error). fmt is the full
 * original format string, used only for error messages.
 *
 * Exposed so libxo.c can call it for xo_emit_field_hvf().
 */
const char *xo_parse_roles(xo_parse_t *xpp, const char *fmt,
			    const char *basep, xo_field_info_t *xfip);

/*
 * Look up value by name in a flag-mapping table.
 * len < 0 means use strlen(value).  Matches on a prefix of the table name
 * (e.g. "enc" matches "encoder").
 */
xo_xff_flags_t xo_name_lookup(xo_flag_mapping_t *map, const char *value,
			       ssize_t len);

/*
 * Return the pessimistic maximum number of field slots needed for fmt.
 * Used internally and by callers that supply their own field buffer.
 */
unsigned xo_count_fields(xo_parse_t *xpp, const char *fmt, size_t *fmt_lenp);

/*
 * Lower-level parser: fill the caller-supplied fields[] array (up to
 * max_fields entries, zero-terminated).  Used internally by libxo.c
 * for the alloca/retain fast path; external callers should prefer
 * xo_parse_format().
 *
 * Returns 0 on success, -1 on error.
 */
int xo_parse_fields(xo_parse_t *xpp, xo_field_info_t *fields,
		    unsigned max_fields, const char *fmt, size_t fmt_len);

/*
 * Return non-zero if this field role should receive a default "%s" format
 * when no explicit format is given.
 */
int xo_role_wants_default_format(int ftype);

/*
 * Use a rotating stock of buffers to make a printable string
 */
#define XO_NUMBUFS 8
#define XO_SMBUFSZ 128

const char *
xo_printable (const char *str);
const char *
xo_printable2 (const char *str, int len, int bracesp);

/*
 * Some roles need a name, some don't
 */
#define XO_LINT_ROLES_NEEDING_NAME "V" /* Can't have empty name (:XX)*/
#define XO_LINT_ROLES_NEEDING_NAME_OR_FORMAT "DELNPTUW" /* One or the other */
#define XO_LINT_ROLES_NO_DEFAULT_FORMAT "DELNPTUW[]" /* No '%s' default */
#define XO_LINT_ROLES_OPTIONAL_NAME "CG"      /* Might have empty name */
#define XO_LINT_ROLES_DEC_NAME_OR_FORMAT "[]"	       /* ":XX" or "/%d"" */
#define XO_LINT_ROLES_NO_FORMAT "G"	       /* Can't have a format (/XX) */

#define XO_LINT_MIN_NAME 3	/* Lint: no names less than this length  */

#endif /* XO_FORMAT_H */
