/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

/**
 * libxo provides a means of generating text, XML, and JSON output
 * using a single set of function calls, maximizing the value of output
 * while minimizing the cost/impact on the code.
 */

#ifndef INCLUDE_XO_H
#define INCLUDE_XO_H

/** Formatting types */
#define XO_STYLE_TEXT	0	/** Generate text output */
#define XO_STYLE_XML	1	/** Generate XML output */
#define XO_STYLE_JSON	2	/** Generate JSON output */
#define XO_STYLE_HTML	3	/** Generate HTML output */

/** Flags for libxo */
#define XOF_CLOSE_FP	(1<<0)	/** Close file pointer on xo_close() */
#define XOF_PRETTY	(1<<1)	/** Make 'pretty printed' output */
#define XOF_DIV_OPEN	(1<<2)	/** Internal use only: a <div> is open */
#define XOF_LINE_OPEN	(1<<3)	/** Internal use only: a <div class="line"> */
#define XOF_WARN	(1<<4)	/** Generate warnings for broken calls */
#define XOF_XPATH	(1<<5)	/** Emit XPath attributes in HTML  */
#define XOF_INFO	(1<<6)	/** Emit additional info fields (HTML) */
#define XOF_WARN_XML	(1<<7)	/** Emit warnings in XML (on stdout) */
#define XOF_NO_ENV	(1<<8)	/** Don't look at the LIBXO_OPTIONS env var */

/*
 * The xo_info_t structure provides a mapping between names and
 * additional data emitted via HTML.
 */
typedef struct xo_info_s {
    const char *xi_name;	/* Name of the element */
    const char *xi_type;	/* Type of field */
    const char *xi_help;	/* Description of field */
} xo_info_t;

struct xo_handle_s;		/* Opaque structure forward */
typedef struct xo_handle_s xo_handle_t; /* Handle for XO output */

typedef int (*xo_write_func_t)(void *, const char *);
typedef void (*xo_close_func_t)(void *);
typedef void *(*xo_realloc_func_t)(void *, size_t size);
typedef void (*xo_free_func_t)(void *);

typedef char *(*xo_formatter_t)(xo_handle_t *, const char *);

xo_handle_t *
xo_create (unsigned type, unsigned flags);

xo_handle_t *
xo_create_to_file (FILE *fp, unsigned type, unsigned flags);

void
xo_destroy (xo_handle_t *xop);

void
xo_set_writer (xo_handle_t *xop, void *opaque, xo_write_func_t write_func,
	       xo_close_func_t close_func);

void
xo_set_allocator (xo_realloc_func_t realloc_func, xo_free_func_t free_func);

void
xo_set_style (xo_handle_t *xop, unsigned style);

void
xo_set_flags (xo_handle_t *xop, unsigned flags);

void
xo_clear_flags (xo_handle_t *xop, unsigned flags);

void
xo_set_info (xo_handle_t *xop, xo_info_t *infop, int count);

void
xo_set_formatter (xo_handle_t *xop, xo_formatter_t func);

int
xo_emit_hv (xo_handle_t *xop, const char *fmt, va_list vap);

int
xo_emit_h (xo_handle_t *xop, const char *fmt, ...);

int
xo_emit (const char *fmt, ...);

int
xo_open_container_h (xo_handle_t *xop, const char *name);

int
xo_open_container (const char *name);

int
xo_close_container_h (xo_handle_t *xop, const char *name);

int
xo_close_container (const char *name);

int
xo_open_list_h (xo_handle_t *xop, const char *name);

int
xo_open_list (const char *name);

int
xo_close_list_h (xo_handle_t *xop, const char *name);

int
xo_close_list (const char *name);

int
xo_open_instance_h (xo_handle_t *xop, const char *name);

int
xo_open_instance (const char *name);

int
xo_close_instance_h (xo_handle_t *xop, const char *name);

int
xo_close_instance (const char *name);

int
xo_attr_h (xo_handle_t *xop, const char *name, const char *fmt, ...);

int
xo_attr_hv (xo_handle_t *xop, const char *name, const char *fmt, va_list vap);

int
xo_attr (const char *name, const char *fmt, ...);

#endif /* INCLUDE_XO_H */
