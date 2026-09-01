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
 * xo_format.h: libxo format-string parsing, factored out so it can be
 * linked into tools (e.g. LLVM plugins) that do not carry the full
 * libxo runtime.
 */

#ifndef XO_FORMAT_H
#define XO_FORMAT_H

#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#include "xo.h"

/*
 * Maximum number of fields in a format string.  Override at compile
 * time if needed.
 */
#ifndef XO_MAX_FIELDS
#define XO_MAX_FIELDS (8 * 1024)
#endif /* XO_MAX_FIELDS */

#ifndef XO_MAX_SPECS
#define XO_MAX_SPECS (8 * 1024)
#endif /* XO_MAX_SPECS */

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
 * Normal printf has width and precision, which for strings operate as
 * min and max number of columns.  But this depends on the idea that
 * one byte means one column, which UTF-8 and multi-byte characters
 * pitches on its ear.  It may take 40 bytes of data to populate 14
 * columns, but we can't go off looking at 40 bytes of data without the
 * caller's permission for fear/knowledge that we'll generate core files.
 * 
 * So we make three values, distinguishing between "max column" and
 * "number of bytes that we will inspect inspect safely" We call the
 * later "size", and make the format "%[[<min>].[[<size>].<max>]]s".
 *
 * Under the "first do no harm" theory, we default "max" to "size".
 * This is a reasonable assumption for folks that don't grok the
 * MBS/WCS/UTF-8 world, and while it will be annoying, it will never
 * be evil.
 *
 * For example, xo_emit("{:tag/%-14.14s}", buf) will make 14
 * columns of output, but will never look at more than 14 bytes of the
 * input buffer.  This is mostly compatible with printf and caller's
 * expectations.
 *
 * In contrast xo_emit("{:tag/%-14..14s}", buf) will look at however
 * many bytes (or until a NUL is seen) are needed to fill 14 columns
 * of output.  xo_emit("{:tag/%-14.*.14s}", xx, buf) will look at up
 * to xx bytes (or until a NUL is seen) in order to fill 14 columns
 * of output.
 *
 * It's fairly amazing how a good idea (handle all languages of the
 * world) blows such a big hole in the bottom of the fairly weak boat
 * that is C string handling.  The simplicity and completenesss are
 * sunk in ways we haven't even begun to understand.
 */
#define XF_WIDTH_MIN	0	/* Minimal width */
#define XF_WIDTH_SIZE	1	/* Maximum number of bytes to examine */
#define XF_WIDTH_MAX	2	/* Maximum width */
#define XF_WIDTH_NUM	3	/* Numeric fields in printf (min.size.max) */

/* Input and output string encodings */
#define XF_ENC_WIDE	1	/* Wide characters (wchar_t) */
#define XF_ENC_UTF8	2	/* UTF-8 */
#define XF_ENC_LOCALE	3	/* Current locale */

/*
 * A place to parse printf-style format flags for each "field specifier"
 */
typedef struct xo_fspec_s {
    uint8_t xf_fc;	/* Format character */
    uint8_t xf_lflag;	/* 'l' (long) */
    uint8_t xf_hflag;	/* 'h' (half) */
    uint8_t xf_jflag;	/* 'j' (intmax_t) */

    uint8_t xf_tflag;	/* 't' (ptrdiff_t) */
    uint8_t xf_zflag;	/* 'z' (size_t) */
    uint8_t xf_qflag;	/* 'q' (quad_t) */
    uint8_t xf_seen_minus; /* Seen a minus */

    int8_t xf_leading_zero;	/* Seen a leading zero (zero fill)  */
    uint8_t xf_dots;		/* Seen one or more '.'s */
    uint8_t xf_alt;	/* "alternate form" ('#') flag */
    uint8_t xf_stars;		/* Seen one or more '*'s */

    uint8_t xf_star[XF_WIDTH_NUM]; /* Seen one or more '*'s */

    /*
     * Number of '*'s seen in a "%@...@" prefix.  This many int args
     * must be consumed (and discarded) from va_list before this
     * field's own value is pulled.
     */
    uint8_t xf_at_stars;
    int16_t xf_width[XF_WIDTH_NUM]; /* Width/precision/size numeric fields */
    uint16_t xf_start;		    /* Start off in format string */
    uint16_t xf_len;		    /* Length in format string */

    /*
     * Offset from xf_start to the position that should be treated as
     * the '%' for reconstructing a real printf spec: 0 if there's no
     * "%@...@" prefix, or the offset of the prefix's closing '@'
     * (itself treated as a pseudo-'%') if there is.
     */
    uint16_t xf_prefix_len;
    uint8_t xf_num_bits;  /* '!' Number of bits in a number (signed/unsigned) */
    uint8_t xf_padding;
} xo_fspec_t;

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
    xo_fspec_t *xfi_fspecs;		/* Cached format elements */
    uint16_t xfi_num_fspecs;		/* Number of fspecs at xfi_fspec */
    uint16_t xfi_padding[3];		/* Padding (unused) */
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
#define XPF_STRICT	(1<<0)  /* Enable semantic checks */
#define XPF_LINT	(1<<1)	/* Enable lint warnings (minor, non-fatal) */

typedef uint32_t xo_parse_flags_t; /* XPF_* */

typedef struct xo_parse_s {
    xo_realloc_func_t xp_realloc;	/* Allocator (NULL → realloc) */
    xo_free_func_t xp_free;		/* Free (NULL → free) */
    xo_parse_error_func_t xp_error;	/* Error reporter (NULL → silent) */
    void *xp_error_data;		/* Opaque data passed to xp_error */
    xo_parse_error_func_t xp_warn;	/* Warning reporter (NULL → silent) */
    void *xp_warn_data;			/* Opaque data passed to xp_warn */
    xo_parse_flags_t xp_flags;		/* XPF_* flags */

    /* Output — filled in by xo_parse_format() */
    xo_field_info_t *xp_fields;	/* Allocated, zero-terminated field array */
    unsigned xp_num_fields;	/* Number of valid entries */
    xo_fspec_t *xp_fspecs;	/* Allocated, zero-terminated fspec array */
    unsigned xp_num_fspecs;	/* Number of valid entries */

    /* Working: these fields hold the current values during parsing */
    xo_field_info_t *xp_cur_field; /* Current item in xp_fields */
    xo_fspec_t *xp_cur_fspec;	   /* Current item in xp_fspecs */

} xo_parse_t;

#define XO_IS_LINT(_p) (((_p)->xp_flags & XPF_LINT) ? TRUE : FALSE)

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
 * Look up the name of a role
 */
const char *xo_lookup_role_name(uint32_t value);

/*
 * Table mapping field role characters (e.g. 'C', 'L', 'N') to their
 * human-readable names, for use in diagnostics.
 */
extern xo_flag_mapping_t xo_role_names[];

/*
 * Return the pessimistic maximum number of field slots needed for fmt.
 * Used internally and by callers that supply their own field buffer.
 */
void
xo_count_fields(xo_parse_t *xpp, const char *fmt, size_t *fmt_lenp,
		unsigned *max_fields, unsigned *max_specs);


/*
 * Lower-level parser: fill the caller-supplied fields[] array (up to
 * max_fields entries, zero-terminated).  Used internally by libxo.c
 * for the alloca/retain fast path; external callers should prefer
 * xo_parse_format().
 *
 * Returns 0 on success, -1 on error.
 */
int xo_parse_fields(xo_parse_t *xpp, const char *fmt, size_t fmt_len);

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
#define XO_LINT_ROLES_NEEDING_NAME_OR_FORMAT "DEFLNPTUW" /* One or the other */
#define XO_LINT_ROLES_OPTIONAL_NAME "CG"      /* Might have empty name */
#define XO_LINT_ROLES_DEC_NAME_OR_FORMAT "[]"	       /* ":XX" or "/%d"" */
#define XO_LINT_ROLES_NO_FORMAT "G"	       /* Can't have a format (/XX) */

#define XO_LINT_MIN_NAME 3	/* Lint: no names less than this length  */

/*
 * Parse a single printf-style format specifier starting at 'cp' (which
 * points at the '%').  Fills in *xfp and returns a pointer to the
 * conversion character, or NULL on error.
 */
const char *
xo_parse_format_spec (xo_parse_t *xpp, xo_fspec_t *xfp,
		      const char *cp, const char *ep, const char *fmt);

/*
 * Parse a field's format substring [fmt, ep) into fspec entries, writing
 * them starting at xpp->xp_cur_fspec (bounded by xpp->xp_fspecs +
 * xpp->xp_num_fspecs) and advancing xpp->xp_cur_fspec past the entries
 * written and their terminator.  Each entry's xf_start/xf_len are byte
 * offsets relative to 'fmt' (the field's format substring passed here,
 * NOT the whole xo_emit format string the field was found in).
 *
 * Returns the number of entries written (not counting the terminator) on
 * success; -1 on a genuine format-spec error (already reported via
 * xpp->xp_error); or -2 if there was not enough room left in
 * xpp->xp_fspecs.  -2 is never an error the caller need report: a field
 * with xfi_fspecs left NULL is always a safe "re-parse at runtime" signal.
 */
int
xo_parse_fspecs (xo_parse_t *xpp, const char *fmt, const char *ep);

static inline int
xo_is_format_char (char ch, int numeric_only)
{
    const char *cp = numeric_only ? "DEFGOUdefgiou" : "ACDEFGOSUXacdefgimopsux";
    return strchr(cp, ch) != NULL;
}



#endif /* XO_FORMAT_H */
