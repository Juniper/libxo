/*
 * Copyright (c) 2014-2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, 2025
 *
 * xo_field.h: libxo format-string parsing, factored out so it can be
 * linked into tools (e.g. LLVM plugins) that do not carry the full
 * libxo runtime.
 */

#ifndef XO_FIELD_H
#define XO_FIELD_H

#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#include "xo.h"		    /* xo_realloc_func_t, xo_free_func_t, xo_xff_flags_t */
#include "xo_encoder.h"	    /* XFF_* field flags */

/*
 * Maximum number of fields in a format string.  Override at compile
 * time if needed.
 */
#ifndef XO_MAX_FIELDS
#define XO_MAX_FIELDS (8 * 1024)
#endif /* XO_MAX_FIELDS */

/* xfi_ftype values for non-character roles */
#define XO_ROLE_EBRACE	'{'	/* Escaped braces: {{ content }} */
#define XO_ROLE_TEXT	'+'	/* Plain text between fields */
#define XO_ROLE_NEWLINE	'\n'	/* Bare newline */

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
 * This structure represents the parsed field information, suitable for
 * processing by xo_do_emit and anything else that needs to parse fields.
 * Note that all pointers point to the main format string.
 *
 * XXX This is a first step toward compilable or cachable format
 * strings.  We can also cache the results of dgettext when no format
 * is used, assuming the 'p' modifier has _not_ been set.
 */

/*
 * Parsed representation of one field descriptor from a format string.
 * All string pointers are interior pointers into the original fmt string;
 * they are NOT NUL-terminated — use the corresponding length fields.
 */
typedef struct xo_field_info_s {
    xo_xff_flags_t xfi_flags;	/* Modifier flags (XFF_*) */
    unsigned xfi_ftype;		/* Role character ('V','L','G', XO_ROLE_*) */
    const char *xfi_start;	/* Start of field in format string */
    const char *xfi_content;	/* Field content (name) */
    const char *xfi_format;	/* Display format string */
    const char *xfi_encoding;	/* Encoding format string */
    const char *xfi_next;	/* Next position after this field */
    ssize_t xfi_len;		/* Length of whole field descriptor */
    ssize_t xfi_clen;		/* Length of content */
    ssize_t xfi_flen;		/* Length of format */
    ssize_t xfi_elen;		/* Length of encoding */
    unsigned xfi_fnum;		/* Field number (0 = unset) */
    unsigned xfi_renum;		/* Reordered field number (0 = none) */
} xo_field_info_t;

/*
 * Error callback.  Called when xo_parse_format() encounters a problem.
 * The fmt/vap are a printf-style message; the caller owns the va_list.
 */
typedef void (*xo_parse_error_func_t)(void *data, const char *fmt,
				      va_list vap);

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
unsigned xo_count_fields(xo_parse_t *xpp, const char *fmt);

/*
 * Lower-level parser: fill the caller-supplied fields[] array (up to
 * max_fields entries, zero-terminated).  Used internally by libxo.c
 * for the alloca/retain fast path; external callers should prefer
 * xo_parse_format().
 *
 * Returns 0 on success, -1 on error.
 */
int xo_parse_fields(xo_parse_t *xpp, xo_field_info_t *fields,
		    unsigned max_fields, const char *fmt);

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

const char * xo_printable (const char *str);

#endif /* XO_FIELD_H */
