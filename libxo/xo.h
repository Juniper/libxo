/*
 * Copyright (c) 2014-2018, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

/**
 * libxo provides a means of generating text, XML, JSON, and HTML output
 * using a single set of function calls, maximizing the value of output
 * while minimizing the cost/impact on the code.
 *
 * Full documentation is available in ./doc/libxo.txt or online at:
 *   http://juniper.github.io/libxo/libxo-manual.html
 */

#ifndef INCLUDE_XO_H
#define INCLUDE_XO_H

#include <stdio.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>

#ifdef __dead2
#define XO_NORETURN __dead2
#else
#define XO_NORETURN
#endif /* __dead2 */

/*
 * Normally we'd use the HAVE_PRINTFLIKE define triggered by the
 * --enable-printflike option to configure, but we don't install
 * our internal "xoconfig.h", and I'd rather not.  Taking the
 * coward's path, we'll turn it on inside a #if that allows
 * others to turn it off where needed.  Not ideal, but functional.
 */
#if !defined(NO_PRINTFLIKE)
#if defined(__linux) && !defined(__printflike)
#define __printflike(_x, _y) __attribute__((__format__ (__printf__, _x, _y)))
#endif
#define XO_PRINTFLIKE(_x, _y) __printflike(_x, _y)
#else
#define XO_PRINTFLIKE(_x, _y)
#endif /* NO_PRINTFLIKE */

/** Formatting types */
typedef unsigned short xo_style_t;
#define XO_STYLE_TEXT	0	/** Generate text output */
#define XO_STYLE_XML	1	/** Generate XML output */
#define XO_STYLE_JSON	2	/** Generate JSON output */
#define XO_STYLE_HTML	3	/** Generate HTML output */
#define XO_STYLE_SDPARAMS 4	/* Generate syslog structured data params */
#define XO_STYLE_ENCODER 5	/* Generate calls to external encoder */

/** Flags for libxo */
typedef unsigned long long xo_xof_flags_t;
#define XOF_BIT(_n) ((xo_xof_flags_t) 1 << (_n))
#define XOF_CLOSE_FP	XOF_BIT(0) /** Close file pointer on xo_close() */
#define XOF_PRETTY	XOF_BIT(1) /** Make 'pretty printed' output */
#define XOF_LOG_SYSLOG	XOF_BIT(2) /** Log (on stderr) our syslog content */
#define XOF_RESV3	XOF_BIT(3) /* Unused */

#define XOF_WARN	XOF_BIT(4) /** Generate warnings for broken calls */
#define XOF_XPATH	XOF_BIT(5) /** Emit XPath attributes in HTML  */
#define XOF_INFO	XOF_BIT(6) /** Emit additional info fields (HTML) */
#define XOF_WARN_XML	XOF_BIT(7) /** Emit warnings in XML (on stdout) */

#define XOF_NO_ENV	XOF_BIT(8) /** Don't look at LIBXO_OPTIONS env var */
#define XOF_NO_VA_ARG	XOF_BIT(9) /** Don't advance va_list w/ va_arg() */
#define XOF_DTRT	XOF_BIT(10) /** Enable "do the right thing" mode */
#define XOF_KEYS	XOF_BIT(11) /** Flag 'key' fields for xml and json */

#define XOF_IGNORE_CLOSE XOF_BIT(12) /** Ignore errors on close tags */
#define XOF_NOT_FIRST	XOF_BIT(13) /* Not the first item (JSON)  */
#define XOF_NO_LOCALE	XOF_BIT(14) /** Don't bother with locale */
#define XOF_RESV15	XOF_BIT(15) /* Unused */

#define XOF_NO_TOP	XOF_BIT(16) /** Don't emit the top braces in JSON */
#define XOF_RESV17	XOF_BIT(17) /* Unused  */
#define XOF_UNITS	XOF_BIT(18) /** Encode units in XML */
#define XOF_RESV19	XOF_BIT(19) /* Unused */

#define XOF_UNDERSCORES	XOF_BIT(20) /** Replace dashes with underscores (JSON)*/
#define XOF_COLUMNS	XOF_BIT(21) /** xo_emit should return a column count */
#define XOF_FLUSH	XOF_BIT(22) /** Flush after each xo_emit call */
#define XOF_FLUSH_LINE	XOF_BIT(23) /** Flush after each newline */

#define XOF_NO_CLOSE	XOF_BIT(24) /** xo_finish won't close open elements */
#define XOF_COLOR_ALLOWED XOF_BIT(25) /** Allow color/effects to be enabled */
#define XOF_COLOR	XOF_BIT(26) /** Enable color and effects */
#define XOF_NO_HUMANIZE	XOF_BIT(27) /** Block the {h:} modifier */

#define XOF_LOG_GETTEXT	XOF_BIT(28) /** Log (stderr) gettext lookup strings */
#define XOF_UTF8	XOF_BIT(29) /** Force text output to be UTF8 */
/* XOF_BIT(30) is unused (was XOF_RETAIN_ALL; removed as unsafe) */
#define XOF_RETAIN_NONE	XOF_BIT(31) /** Prevent use of XOEF_RETAIN */

#define XOF_COLOR_MAP	XOF_BIT(32) /** Color map has been initialized */
#define XOF_CONTINUATION XOF_BIT(33) /** Continuation of previous line */
#define XOF_DEBUG	XOF_BIT(34) /** Internal debug flag */
#ifdef XO_WANT_FILTER_FLAG	    /* Only visible internally */
#define XOF_FILTER      XOF_BIT(35) /* Filters are active; uses whiteboard */
#endif /* XO_WANT_FILTER_FLAG */

#define XOF_NO_TOP_LEVEL XOF_BIT(36) /** Don't make a fake top-level tag */
#define XOF_FILTER_WARN	XOF_BIT(37)  /** Warn about runtime errors w/ filters */

typedef unsigned xo_emit_flags_t; /* Flags to xo_emit() and friends */
#define XOEF_RETAIN	(1<<0)	  /* Retain parsed formatting information */
#define XOEF_NO_RETAIN	(1<<1)	  /* Format must not be retained (dynamic) */

/*
 * Field modifier flags (xfi_flags in xo_field_info_t).
 * These are shared between the public field-cache API and the internal
 * encoder plugin API (xo_encoder.h).
 */
typedef unsigned long long xo_xff_flags_t;
#define XFF_COLON	(1<<0)	/* Append a ":" */
#define XFF_COMMA	(1<<1)	/* Append a "," iff there's more output */
#define XFF_WS		(1<<2)	/* Append a blank */
#define XFF_ENCODE_ONLY	(1<<3)	/* Only emit for encoding styles (XML, JSON) */

#define XFF_QUOTE	(1<<4)	/* Force quotes */
#define XFF_NOQUOTE	(1<<5)	/* Force no quotes */
#define XFF_DISPLAY_ONLY (1<<6)	/* Only emit for display styles (text, html) */
#define XFF_KEY		(1<<7)	/* Field is a key (for XPath) */

#define XFF_XML		(1<<8)	/* Force XML encoding style (for XPath) */
#define XFF_ATTR	(1<<9)	/* Escape value using attribute rules (XML) */
#define XFF_BLANK_LINE	(1<<10)	/* Emit a blank line */
#define XFF_NO_OUTPUT	(1<<11)	/* Do not make any output */

#define XFF_TRIM_WS	(1<<12)	/* Trim whitespace off encoded values */
#define XFF_LEAF_LIST	(1<<13)	/* A leaf-list (list of values) */
#define XFF_UNESCAPE	(1<<14)	/* Need to printf-style unescape the value */
#define XFF_HUMANIZE	(1<<15)	/* Humanize the value (for display styles) */

#define XFF_HN_SPACE	(1<<16)	/* Humanize: put space before suffix */
#define XFF_HN_DECIMAL	(1<<17)	/* Humanize: add one decimal place if <10 */
#define XFF_HN_1000	(1<<18)	/* Humanize: use 1000, not 1024 */
#define XFF_GT_FIELD	(1<<19) /* Call gettext() on a field */

#define XFF_GT_PLURAL	(1<<20)	/* Call dngettext to find plural form */
#define XFF_ARGUMENT	(1<<21)	/* Content provided via argument */
#define XFF_ESC_SLASH	(1<<22)	/* Escape a forward slash in a JSON string */
#define XFF_ESC_SQUARE	(1<<23)	/* Escape XML control chars to UTF8 square */

#define XFF_ESC_PRIVATE (1<<24)	/* Escape XML ctrl chars as private (0xe000) */

/* Flags to turn off when we don't want i18n processing */
#define XFF_GT_FLAGS (XFF_GT_FIELD | XFF_GT_PLURAL)

/*
 * xo_format_offset_t: signed byte offset into a format string.
 * Negative values are sentinels:
 *   XO_FOFF_NONE    (-1)  field absent (replaces NULL pointer)
 *   XO_FOFF_DEFAULT (-2)  use the default "%s" format
 * Any value >= 0 is a real byte offset into the base string.
 * Format strings longer than XO_FORMAT_MAX are rejected by the parser.
 */
typedef int16_t xo_format_offset_t;
#define XO_FOFF_NONE	((xo_format_offset_t) -1)
#define XO_FOFF_DEFAULT	((xo_format_offset_t) -2)
#define XO_FORMAT_MAX	32000	/* leaves room below INT16_MAX for one-past-end */

/* xfi_ftype values for non-character field roles */
#define XO_ROLE_EBRACE	'{'	/* Escaped brace: {{ content }} */
#define XO_ROLE_TEXT	'+'	/* Plain text between fields */
#define XO_ROLE_NEWLINE	'\n'	/* Bare newline */

/* The default printf-style format used when a field has no explicit format. */
extern const char xo_default_format[];

/*
 * Resolve an xo_format_offset_t to a const char *.
 * Returns NULL for XO_FOFF_NONE, the global default "%s" for XO_FOFF_DEFAULT,
 * and base+off for any non-negative offset.
 */
static inline const char *
xo_foff (const char *base, xo_format_offset_t off)
{
    if (off >= 0)               return base + off;
    if (off == XO_FOFF_DEFAULT) return xo_default_format;
    return NULL;                /* XO_FOFF_NONE */
}

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
    unsigned xfi_ftype;			/* Role character ('V','L','G', XO_ROLE_*) */
    xo_format_offset_t xfi_start;	/* Offset of field start in base string */
    xo_format_offset_t xfi_content;	/* Offset of content (name) */
    xo_format_offset_t xfi_format;	/* Offset of display format (or XO_FOFF_DEFAULT) */
    xo_format_offset_t xfi_encoding;	/* Offset of encoding format */
    xo_format_offset_t xfi_next;	/* Offset just past this field */
    xo_format_offset_t xfi_len;		/* Length of whole field descriptor */
    xo_format_offset_t xfi_clen;	/* Length of content */
    xo_format_offset_t xfi_flen;	/* Length of format */
    xo_format_offset_t xfi_elen;	/* Length of encoding */
    unsigned xfi_fnum;			/* Field number (0 = unset) */
    unsigned xfi_renum;			/* Reordered field number (0 = none) */
} xo_field_info_t;

/*
 * The xo_info_t structure provides a mapping between names and
 * additional data emitted via HTML.
 */
typedef struct xo_info_s {
    const char *xi_name;	/* Name of the element */
    const char *xi_type;	/* Type of field */
    const char *xi_help;	/* Description of field */
} xo_info_t;

#define XO_INFO_NULL NULL, NULL, NULL /* Use '{ XO_INFO_NULL }' to end lists */

struct xo_handle_s;		/* Opaque structure forward */
typedef struct xo_handle_s xo_handle_t; /* Handle for XO output */

/*
 * Early versions of the API used "int" instead of "size_t" for buffer
 * sizes.  We want to fix this but allow for backwards compatibility
 * where needed.
 */
#ifdef XO_USE_INT_RETURN_CODES
typedef int xo_ssize_t;		/* Buffer size */
#else /* XO_USE_INT_RETURN_CODES */
typedef ssize_t xo_ssize_t;	/* Buffer size */
#endif /* XO_USE_INT_RETURN_CODES */

typedef xo_ssize_t (*xo_write_func_t)(void *, const char *);
typedef void (*xo_close_func_t)(void *);
typedef int (*xo_flush_func_t)(void *);
typedef void *(*xo_realloc_func_t)(void *, size_t);
typedef void (*xo_free_func_t)(void *);

/*
 * The formatter function mirrors "vsnprintf", with an additional argument
 * of the xo handle.  The caller should return the number of bytes _needed_
 * to fit the data, even if this exceeds 'len'.
 */
typedef xo_ssize_t (*xo_formatter_t)(xo_handle_t *, char *, xo_ssize_t,
				const char *, va_list);
typedef void (*xo_checkpointer_t)(xo_handle_t *, va_list, int);

xo_handle_t *
xo_create (xo_style_t style, xo_xof_flags_t flags);

xo_handle_t *
xo_create_to_file (FILE *fp, xo_style_t style, xo_xof_flags_t flags);

void
xo_destroy (xo_handle_t *xop);

void
xo_set_writer (xo_handle_t *xop, void *opaque, xo_write_func_t write_func,
	       xo_close_func_t close_func, xo_flush_func_t flush_func);

void
xo_set_allocator (xo_realloc_func_t realloc_func, xo_free_func_t free_func);

void
xo_set_style (xo_handle_t *xop, xo_style_t style);

xo_style_t
xo_get_style (xo_handle_t *xop);

int
xo_set_style_name (xo_handle_t *xop, const char *style);

int
xo_set_options (xo_handle_t *xop, const char *input);

xo_xof_flags_t
xo_get_flags (xo_handle_t *xop);

void
xo_set_flags (xo_handle_t *xop, xo_xof_flags_t flags);

int
xo_isset_flags (xo_handle_t *xop, xo_xof_flags_t flags);

void
xo_clear_flags (xo_handle_t *xop, xo_xof_flags_t flags);

int
xo_set_file_h (xo_handle_t *xop, FILE *fp);

int
xo_set_file (FILE *fp);

void
xo_set_info (xo_handle_t *xop, xo_info_t *infop, int count);

void
xo_set_formatter (xo_handle_t *xop, xo_formatter_t func, xo_checkpointer_t);

void
xo_set_depth (xo_handle_t *xop, int depth);

xo_ssize_t
xo_emit_hv (xo_handle_t *xop, const char *fmt, va_list vap);

xo_ssize_t
xo_emit_h (xo_handle_t *xop, const char *fmt, ...);

xo_ssize_t
xo_emit (const char *fmt, ...);

xo_ssize_t
xo_emitr (const char *fmt, ...);

xo_ssize_t
xo_emit_hvf (xo_handle_t *xop, xo_emit_flags_t flags,
	     const char *fmt, va_list vap);

xo_ssize_t
xo_emit_hf (xo_handle_t *xop, xo_emit_flags_t flags, const char *fmt, ...);

xo_ssize_t
xo_emit_f (xo_emit_flags_t flags, const char *fmt, ...);

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

typedef struct xo_format_cache_s {
    unsigned xfc_version;		/* == XO_EMIT_CACHE_VERSION */
    unsigned xfc_num_fields;
    const xo_field_info_t *xfc_fields;	/* const xo_field_info_t[] */
} xo_format_cache_t;

xo_ssize_t
xo_emit_cached_h (xo_handle_t *xop, const xo_format_cache_t *fcp,
		  const char *fmt, ...);

xo_ssize_t
xo_emit_cached (const xo_format_cache_t *fcp, const char *fmt, ...);

xo_ssize_t
xo_emit_cachedr (const xo_format_cache_t *fcp, const char *fmt, ...);

xo_ssize_t
xo_emit_cached_hv (xo_handle_t *xop, const xo_format_cache_t *fcp,
		   const char *fmt, va_list vap);

xo_ssize_t
xo_emit_cached_hvf (xo_handle_t *xop, xo_emit_flags_t flags,
		    const xo_format_cache_t *fcp, const char *fmt, va_list vap);

xo_ssize_t
xo_emit_cached_hf (xo_handle_t *xop, xo_emit_flags_t flags,
		   const xo_format_cache_t *fcp, const char *fmt, ...);

xo_ssize_t
xo_emit_cached_f (xo_emit_flags_t flags, const xo_format_cache_t *fcp,
		  const char *fmt, ...);

XO_PRINTFLIKE(2, 0)
static inline xo_ssize_t
xo_emit_hvp (xo_handle_t *xop, const char *fmt, va_list vap)
{
    return xo_emit_hv(xop, fmt, vap);
}

XO_PRINTFLIKE(2, 3)
static inline xo_ssize_t
xo_emit_hp (xo_handle_t *xop, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_ssize_t rc = xo_emit_hv(xop, fmt, vap);
    va_end(vap);
    return rc;
}

XO_PRINTFLIKE(1, 2)
static inline xo_ssize_t
xo_emit_p (const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_ssize_t rc = xo_emit_hv(NULL, fmt, vap);
    va_end(vap);
    return rc;
}

XO_PRINTFLIKE(3, 0)
static inline xo_ssize_t
xo_emit_hvfp (xo_handle_t *xop, xo_emit_flags_t flags,
	      const char *fmt, va_list vap)
{
    return xo_emit_hvf(xop, flags, fmt, vap);
}

XO_PRINTFLIKE(3, 4)
static inline xo_ssize_t
xo_emit_hfp (xo_handle_t *xop, xo_emit_flags_t flags, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_ssize_t rc = xo_emit_hvf(xop, flags, fmt, vap);
    va_end(vap);
    return rc;
}

XO_PRINTFLIKE(2, 3)
static inline xo_ssize_t
xo_emit_fp (xo_emit_flags_t flags, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_ssize_t rc = xo_emit_hvf(NULL, flags, fmt, vap);
    va_end(vap);
    return rc;
}

/* Cached _p variants: mirror the _p wrappers above, targeting xo_emit_cached_hv
 * and xo_emit_cached_hvf so the IR pass can rewrite _p calls uniformly. */

XO_PRINTFLIKE(3, 0)
static inline xo_ssize_t
xo_emit_cached_hvp (xo_handle_t *xop, const xo_format_cache_t *fcp,
		    const char *fmt, va_list vap)
{
    return xo_emit_cached_hv(xop, fcp, fmt, vap);
}

XO_PRINTFLIKE(3, 4)
static inline xo_ssize_t
xo_emit_cached_hp (xo_handle_t *xop, const xo_format_cache_t *fcp,
		   const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_ssize_t rc = xo_emit_cached_hv(xop, fcp, fmt, vap);
    va_end(vap);
    return rc;
}

XO_PRINTFLIKE(2, 3)
static inline xo_ssize_t
xo_emit_cached_p (const xo_format_cache_t *fcp, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_ssize_t rc = xo_emit_cached_hv(NULL, fcp, fmt, vap);
    va_end(vap);
    return rc;
}

XO_PRINTFLIKE(4, 0)
static inline xo_ssize_t
xo_emit_cached_hvfp (xo_handle_t *xop, xo_emit_flags_t flags,
		     const xo_format_cache_t *fcp, const char *fmt, va_list vap)
{
    return xo_emit_cached_hvf(xop, flags, fcp, fmt, vap);
}

XO_PRINTFLIKE(4, 5)
static inline xo_ssize_t
xo_emit_cached_hfp (xo_handle_t *xop, xo_emit_flags_t flags,
		    const xo_format_cache_t *fcp, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_ssize_t rc = xo_emit_cached_hvf(xop, flags, fcp, fmt, vap);
    va_end(vap);
    return rc;
}

XO_PRINTFLIKE(3, 4)
static inline xo_ssize_t
xo_emit_cached_fp (xo_emit_flags_t flags, const xo_format_cache_t *fcp,
		   const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_ssize_t rc = xo_emit_cached_hvf(NULL, flags, fcp, fmt, vap);
    va_end(vap);
    return rc;
}

xo_ssize_t
xo_open_container_hf (xo_handle_t *xop, xo_xof_flags_t flags, const char *name);

xo_ssize_t
xo_open_container_h (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_open_container (const char *name);

xo_ssize_t
xo_open_container_hd (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_open_container_d (const char *name);

xo_ssize_t
xo_close_container_h (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_close_container (const char *name);

xo_ssize_t
xo_close_container_hd (xo_handle_t *xop);

xo_ssize_t
xo_close_container_d (void);

xo_ssize_t
xo_open_list_hf (xo_handle_t *xop, xo_xof_flags_t flags, const char *name);

xo_ssize_t
xo_open_list_h (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_open_list (const char *name);

xo_ssize_t
xo_open_list_hd (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_open_list_d (const char *name);

xo_ssize_t
xo_close_list_h (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_close_list (const char *name);

xo_ssize_t
xo_close_list_hd (xo_handle_t *xop);

xo_ssize_t
xo_close_list_d (void);

xo_ssize_t
xo_open_instance_hf (xo_handle_t *xop, xo_xof_flags_t flags, const char *name);

xo_ssize_t
xo_open_instance_h (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_open_instance (const char *name);

xo_ssize_t
xo_open_instance_hd (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_open_instance_d (const char *name);

xo_ssize_t
xo_close_instance_h (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_close_instance (const char *name);

xo_ssize_t
xo_close_instance_hd (xo_handle_t *xop);

xo_ssize_t
xo_close_instance_d (void);

xo_ssize_t
xo_open_marker_h (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_open_marker (const char *name);

xo_ssize_t
xo_close_marker_h (xo_handle_t *xop, const char *name);

xo_ssize_t
xo_close_marker (const char *name);

xo_ssize_t
xo_attr_h (xo_handle_t *xop, const char *name, const char *fmt, ...);

xo_ssize_t
xo_attr_hv (xo_handle_t *xop, const char *name, const char *fmt, va_list vap);

xo_ssize_t
xo_attr (const char *name, const char *fmt, ...);

void
xo_error_hv (xo_handle_t *xop, const char *fmt, va_list vap);

void
xo_error_h (xo_handle_t *xop, const char *fmt, ...);

void
xo_error (const char *fmt, ...);

void
xo_errorn_hv (xo_handle_t *xop, int need_newline, const char *fmt, va_list vap);

void
xo_errorn_h (xo_handle_t *xop, const char *fmt, ...);

void
xo_errorn (const char *fmt, ...);

xo_ssize_t
xo_flush_h (xo_handle_t *xop);

xo_ssize_t
xo_flush (void);

xo_ssize_t
xo_finish_h (xo_handle_t *xop);

xo_ssize_t
xo_finish (void);

void
xo_finish_atexit (void);

void
xo_set_leading_xpath (xo_handle_t *xop, const char *path);

void
xo_warn_hcv (xo_handle_t *xop, int code, int check_warn,
	     const char *fmt, va_list vap);

void
xo_warn_hc (xo_handle_t *xop, int code, const char *fmt, ...) XO_PRINTFLIKE(3, 4);

void
xo_warn_c (int code, const char *fmt, ...) XO_PRINTFLIKE(2, 3);

void
xo_warn (const char *fmt, ...) XO_PRINTFLIKE(1, 2);

void
xo_warnx (const char *fmt, ...) XO_PRINTFLIKE(1, 2);

void
xo_err (int eval, const char *fmt, ...) XO_NORETURN XO_PRINTFLIKE(2, 3);

void
xo_errx (int eval, const char *fmt, ...) XO_NORETURN XO_PRINTFLIKE(2, 3);

void
xo_errc (int eval, int code, const char *fmt, ...) XO_NORETURN XO_PRINTFLIKE(3, 4);

void
xo_message_hcv (xo_handle_t *xop, int code, const char *fmt, va_list vap) XO_PRINTFLIKE(3, 0);

void
xo_message_hc (xo_handle_t *xop, int code, const char *fmt, ...) XO_PRINTFLIKE(3, 4);

void
xo_message_c (int code, const char *fmt, ...) XO_PRINTFLIKE(2, 3);

void
xo_message_e (const char *fmt, ...) XO_PRINTFLIKE(1, 2);

void
xo_message (const char *fmt, ...) XO_PRINTFLIKE(1, 2);

void
xo_emit_warn_hcv (xo_handle_t *xop, int as_warning, int code,
		  const char *fmt, va_list vap);

void
xo_emit_warn_hc (xo_handle_t *xop, int code, const char *fmt, ...);

void
xo_emit_warn_c (int code, const char *fmt, ...);

void
xo_emit_warn (const char *fmt, ...);

void
xo_emit_warnx (const char *fmt, ...);

void
xo_emit_err (int eval, const char *fmt, ...) XO_NORETURN;

void
xo_emit_errx (int eval, const char *fmt, ...) XO_NORETURN;

void
xo_emit_errc (int eval, int code, const char *fmt, ...) XO_NORETURN;

XO_PRINTFLIKE(4, 0)
static inline void
xo_emit_warn_hcvp (xo_handle_t *xop, int as_warning, int code,
		  const char *fmt, va_list vap)
{
    xo_emit_warn_hcv(xop, as_warning, code, fmt, vap);
}

XO_PRINTFLIKE(3, 4)
static inline void
xo_emit_warn_hcp (xo_handle_t *xop, int code, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_emit_warn_hcv(xop, 1, code, fmt, vap);
    va_end(vap);
}

XO_PRINTFLIKE(2, 3)
static inline void
xo_emit_warn_cp (int code, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 1, code, fmt, vap);
    va_end(vap);
}

XO_PRINTFLIKE(1, 2)
static inline void
xo_emit_warn_p (const char *fmt, ...)
{
    int code = errno;
    va_list vap;
    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 1, code, fmt, vap);
    va_end(vap);
}

XO_PRINTFLIKE(1, 2)
static inline void
xo_emit_warnx_p (const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 1, -1, fmt, vap);
    va_end(vap);
}

XO_NORETURN XO_PRINTFLIKE(2, 3)
static inline void
xo_emit_err_p (int eval, const char *fmt, ...)
{
    int code = errno;
    va_list vap;
    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 0, code, fmt, vap);
    va_end(vap);

    exit(eval);
}

XO_PRINTFLIKE(2, 3)
static inline void
xo_emit_errx_p (int eval, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 0, -1, fmt, vap);
    va_end(vap);
    exit(eval);
}

XO_PRINTFLIKE(3, 4)
static inline void
xo_emit_errc_p (int eval, int code, const char *fmt, ...)
{
    va_list vap;
    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 0, code, fmt, vap);
    va_end(vap);
    exit(eval);
}

void
xo_emit_err_v (int eval, int code, const char *fmt, va_list vap) XO_NORETURN XO_PRINTFLIKE(3, 0);

void
xo_no_setlocale (void);

/**
 * @brief Lift libxo-specific arguments from a set of arguments
 *
 * libxo-enable programs typically use command line options to enable
 * all the nifty-cool libxo features.  xo_parse_args() makes this simple
 * by pre-processing the command line arguments given to main(), handling
 * and removing the libxo-specific ones, meaning anything starting with
 * "--libxo".  A full description of these arguments is in the base
 * documentation.
 * @param[in] argc Number of arguments (ala #main())
 * @param[in] argc Array of argument strings (ala #main())
 * @return New number of arguments, or -1 for failure.
 */
int
xo_parse_args (int argc, char **argv);

/**
 * This is the "magic" number returned by libxo-supporting commands
 * when passed the equally magic "--libxo-check" option.  If you
 * return this, we can (unsafely) assume that since you know the magic
 * handshake, you'll happily handle future --libxo options and not do
 * something violent like reboot the box or create another hole in the
 * ozone layer.
 */
#define XO_HAS_LIBXO	121

/**
 * externs for libxo's version number strings
 */
extern const char xo_version[];	      /** Base version triple string */
extern const char xo_version_extra[]; /** Extra version magic content */

/**
 * @brief Recode the name of the program, suitable for error output.
 *
 * libxo will record the given name for use while generating error
 * messages.  The contents are not copied, so the value must continue
 * to point to a valid memory location.  This allows the caller to change
 * the value, but requires the caller to manage the memory.  Typically
 * this is called with argv[0] from main().
 * @param[in] name The name of the current application program
 */
void
xo_set_program (const char *name);

/**
 * @brief Add a version string to the output, where possible.
 *
 * Adds a version number to the output, suitable for tracking
 * changes in the content.  This is only important for the "encoding"
 * format styles (XML and JSON) and allows a user of the data to
 * discern which version of the data model is in use.
 * @param[in] version The version number, encoded as a string
 */
void
xo_set_version (const char *version);

/**
 * #xo_set_version with a handle.
 * @param[in] xop A valid libxo handle, or NULL for the default handle
 * @param[in] version The version number, encoded as a string
 */
void
xo_set_version_h (xo_handle_t *xop, const char *version);

void
xo_open_log (const char *ident, int logopt, int facility);

void
xo_close_log (void);

int
xo_set_logmask (int maskpri);

void
xo_set_unit_test_mode (int value);

void
xo_syslog (int priority, const char *name, const char *message, ...);

void
xo_vsyslog (int priority, const char *name, const char *message, va_list args);

typedef void (*xo_syslog_open_t)(void);
typedef void (*xo_syslog_send_t)(const char *full_msg,
				 const char *v0_hdr, const char *text_only);
typedef void (*xo_syslog_close_t)(void);

void
xo_set_syslog_handler (xo_syslog_open_t open_func, xo_syslog_send_t send_func,
		       xo_syslog_close_t close_func);

void
xo_set_syslog_enterprise_id (unsigned short eid);

typedef void (*xo_simplify_field_func_t)(const char *, unsigned, int);

char *
xo_simplify_format (xo_handle_t *xop, const char *fmt, int with_numbers,
		    xo_simplify_field_func_t field_cb);

xo_ssize_t
xo_emit_field_hvf (xo_handle_t *xop, xo_emit_flags_t flags,
		   const char *rolmod, const char *contents,
		   const char *fmt, const char *efmt,
		   va_list vap);

xo_ssize_t
xo_emit_field_hv (xo_handle_t *xop, const char *rolmod, const char *contents,
		  const char *fmt, const char *efmt,
		  va_list vap);

xo_ssize_t
xo_emit_field_h (xo_handle_t *xop, const char *rolmod, const char *contents,
		 const char *fmt, const char *efmt, ...);

xo_ssize_t
xo_emit_field_f (xo_emit_flags_t flags, const char *rolmod,
		 const char *contents,
		 const char *fmt, const char *efmt, ...);

xo_ssize_t
xo_emit_field (const char *rolmod, const char *contents,
	       const char *fmt, const char *efmt, ...);

void
xo_retain_clear_all (void);

void
xo_retain_clear (const char *fmt);

unsigned long
xo_retain_get_hits (void);

int
xo_map_add (xo_handle_t *xop, const char *from, size_t flen,
	    const char *to, size_t tlen);

int
xo_map_add_file (xo_handle_t *xop, const char *fname);

int
xo_add_filter (xo_handle_t *xop, const char *vp);

int
xo_discarding_output_h (xo_handle_t *xop);

int
xo_discarding_output (void);

#endif /* INCLUDE_XO_H */
