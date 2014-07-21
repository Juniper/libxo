/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include <sys/types.h>
#include <stdarg.h>
#include <string.h>

#include "libxo.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif /* UNUSED */

#define XO_INDENT_BY 2	/* Amount to indent when pretty printing */
#define XO_BUFSIZ	(8*1024) /* Initial buffer size */
#define XO_DEPTH	512	 /* Default stack depth */

#ifdef LIBXO_WIDE
typedef wchar_t xchar_t;
#define W L

#define fprintf fwprintf
#define vfprintf vfwprintf
#define vsnprintf vswnprintf
#define snprintf swnprintf
#define strchr wcschr

#else /* LIBXO_WIDE */
typedef char xchar_t;
#define W /* nothing */
#endif /* LIBXO_WIDE */

/*
 * xo_buffer_t: a memory buffer that can be grown as needed.  We
 * use them for building format strings and output data.
 */
typedef struct xo_buffer_s {
    xchar_t *xb_bufp;		/* Buffer memory */
    xchar_t *xb_curp;		/* Current insertion point */
    int xb_size;		/* Size of buffer */
} xo_buffer_t;

/*
 * xo_stack_t: As we open and close containers and levels, we
 * create a stack of frames to track them.  This is needed for
 * XOF_WARN and XOF_XPATH.
 */
typedef struct xo_stack_s {
    unsigned xs_flags;		/* Flags for this frame */
    xchar_t *xs_name;		/* Name (for XPath value) */
} xo_stack_t;

/* Flags for xs_flags: */
#define XSF_NOT_FIRST	(1<<0)	/* Not the first element */
#define XSF_LIST	(1<<1)	/* Frame is a list */
#define XSF_INSTANCE	(1<<2)	/* Frame is an instance */

/*
 * xo_handle_t: this is the principle data structure for libxo.
 * It's used as a store for state, options, and content.
 */
struct xo_handle_s {
    unsigned short xo_style;	/* XO_STYLE_* value */
    unsigned short xo_flags;	/* Flags */
    unsigned short xo_indent;	/* Indent level (if pretty) */
    unsigned short xo_indent_by; /* Indent amount (tab stop) */
    xo_write_func_t xo_write;	/* Write callback */
    xo_close_func_t xo_close;	/* Clo;se callback */
    xo_formatter_t xo_formatter; /* Custom formating function */
    void *xo_opaque;		/* Opaque data for write function */
    FILE *xo_fp;		/* XXX File pointer */
    xo_buffer_t xo_data;	/* Output data */
    xo_buffer_t xo_fmt;	   	/* Work area for building format strings */
    xo_buffer_t xo_attrs;	/* Work area for building XML attributes */
    xo_stack_t *xo_stack;	/* Stack pointer */
    int xo_depth;		/* Depth of stack */
    int xo_stack_size;		/* Size of the stack */
    xo_info_t *xo_info;		/* Info fields for all elements */
    int xo_info_count;		/* Number of info entries */
    va_list xo_vap;		/* Variable arguments (stdargs) */
};

/* Flags for formatting functions */
#define XFF_COLON	(1<<0)	/* Append a ":" */
#define XFF_COMMA	(1<<1)	/* Append a "," iff there's more output */
#define XFF_WS		(1<<2)	/* Append a blank */
#define XFF_HIDE_TEXT	(1<<3)	/* Hide this from text output (text, html) */
#define XFF_QUOTE	(1<<4)	/* Force quotes */
#define XFF_NOQUOTE	(1<<5)	/* Force no quotes */
#define XFF_HIDE_DATA	(1<<6)	/* Hide this from data output (xml, json) */

/*
 * We keep a default handle to allow callers to avoid having to
 * allocate one.  Passing NULL to any of our functions will use
 * this default handle.
 */
static xo_handle_t xo_default_handle;
static int xo_default_inited;

/*
 * To allow libxo to be used in diverse environment, we allow the
 * caller to give callbacks for memory allocation.
 */
static xo_realloc_func_t xo_realloc = realloc;
static xo_free_func_t xo_free = free;

/*
 * Callback to write data to a FILE pointer
 */
static int
xo_write_to_file (void *opaque, const xchar_t *data)
{
    FILE *fp = (FILE *) opaque;
    return fprintf(fp, "%s", data);
}

/*
 * Callback to close a file
 */
static void
xo_close_file (void *opaque)
{
    FILE *fp = (FILE *) opaque;
    fclose(fp);
}

/*
 * Initialize the contents of an xo_buffer_t.
 */
static void
xo_buf_init (xo_buffer_t *xbp)
{
    xbp->xb_size = XO_BUFSIZ;
    xbp->xb_bufp = xo_realloc(NULL, xbp->xb_size);
    xbp->xb_curp = xbp->xb_bufp;
}

/*
 * Initialize an xo_handle_t, using both static defaults and
 * the global settings from the LIBXO_OPTIONS environment
 * variable.
 */
static void
xo_init_handle (xo_handle_t *xop)
{
    xop->xo_opaque = stdout;
    xop->xo_write = xo_write_to_file;

    xo_buf_init(&xop->xo_data);
    xo_buf_init(&xop->xo_fmt);
    if (xop->xo_style == XO_STYLE_XML)
	xo_buf_init(&xop->xo_attrs);

    xop->xo_indent_by = XO_INDENT_BY;
    xop->xo_stack_size = XO_DEPTH;
    xop->xo_stack = xo_realloc(NULL,
			 sizeof(xop->xo_stack[0]) * xop->xo_stack_size);

#if !defined(NO_LIBXO_OPTIONS)
    if (!(xop->xo_flags & XOF_NO_ENV)) {
	char *env = getenv("LIBXO_OPTIONS");
	if (env) {
	    int sz;

	    for ( ; *env; env++) {
		switch (*env) {
		case 'H':
		    xop->xo_style = XO_STYLE_HTML;
		    break;

		case 'I':
		    xop->xo_flags |= XOF_INFO;
		    break;

		case 'i':
		    sz = strspn(env + 1, "0123456789");
		    if (sz > 0) {
			xop->xo_indent_by = atoi(env + 1);
			env += sz - 1;	/* Skip value */
		    }
		    break;

		case 'J':
		    xop->xo_style = XO_STYLE_JSON;
		    break;

		case 'P':
		    xop->xo_flags |= XOF_PRETTY;
		    break;

		case 'T':
		    xop->xo_style = XO_STYLE_TEXT;
		    break;

		case 'W':
		    xop->xo_flags |= XOF_WARN;
		    break;

		case 'X':
		    xop->xo_style = XO_STYLE_XML;
		    break;

		case 'x':
		    xop->xo_flags |= XOF_XPATH;
		    break;
		}
	    }
	}
    }
#endif /* NO_GETENV */
}

/*
 * Initialize the default handle.
 */
static void
xo_default_init (void)
{
    xo_handle_t *xop = &xo_default_handle;

    xo_init_handle(xop);

    xo_default_inited = 1;
}

/*
 * Does the buffer have room for the given number of bytes of data?
 * If not, realloc the buffer to make room.  If that fails, we
 * return 0 to tell the caller they are in trouble.
 */
static int
xo_buf_has_room (xo_buffer_t *xbp, int len)
{
    if (xbp->xb_curp + len >= xbp->xb_bufp + xbp->xb_size) {
	int sz = xbp->xb_size + XO_BUFSIZ;
	xchar_t *bp = xo_realloc(xbp->xb_bufp, sz);
	if (bp == NULL)
	    return 0;
	xbp->xb_curp = bp + (xbp->xb_curp - xbp->xb_bufp);
	xbp->xb_bufp = bp;
	xbp->xb_size = sz;
    }

    return 1;
}

/*
 * Print some data thru the handle.
 */
static int
xo_printf (xo_handle_t *xop, const xchar_t *fmt, ...)
{
    xo_buffer_t *xbp = &xop->xo_data;
    int left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
    int rc;
    va_list vap;

    va_start(vap, fmt);
    rc = vsnprintf(xbp->xb_curp, left, fmt, vap);
    if (rc > xbp->xb_size) {
	if (!xo_buf_has_room(xbp, rc))
	    return -1;

	va_end(vap);		/* Reset vap to the start */
	va_start(vap, fmt);

	left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	rc = vsnprintf(xbp->xb_curp, left, fmt, vap);
    }

    xbp->xb_curp = xbp->xb_bufp;
    rc = xop->xo_write(xop->xo_opaque, xbp->xb_bufp);

    va_end(vap);

    return rc;
}

static int
xo_escape_xml (xo_buffer_t *xbp, int len, int attr)
{
    static xchar_t amp[] = W "&amp;";
    static xchar_t lt[] = W "&lt;";
    static xchar_t gt[] = W "&gt;";
    static xchar_t quot[] = W "&quot;";

    int slen;
    unsigned delta = 0;
    xchar_t *cp, *ep, *ip;
    const xchar_t *sp;

    for (cp = xbp->xb_curp, ep = cp + len; cp < ep; cp++) {
	/* We're subtracting 2: 1 for the NUL, 1 for the char we replace */
	if (*cp == '<')
	    delta += sizeof(lt) - 2;
	else if (*cp == '>')
	    delta += sizeof(gt) - 2;
	else if (*cp == '&')
	    delta += sizeof(amp) - 2;
	else if (attr && *cp == '"')
	    delta += sizeof(quot) - 2;
    }

    if (delta == 0)		/* Nothing to escape; bail */
	return len;

    if (!xo_buf_has_room(xbp, delta)) /* No room; bail, but don't append */
	return 0;

    ep = xbp->xb_curp;
    cp = ep + len;
    ip = cp + delta;
    do {
	cp -= 1;
	ip -= 1;

	if (*cp == '<')
	    sp = lt;
	else if (*cp == '>')
	    sp = gt;
	else if (*cp == '&')
	    sp = amp;
	else if (attr && *cp == '"')
	    sp = quot;
	else {
	    *ip = *cp;
	    continue;
	}

	slen = strlen(sp);
	ip -= slen - 1;
	memcpy(ip, sp, slen);
	
    } while (cp > ep && cp != ip);

    return len + delta;
}

static int
xo_escape_json (xo_buffer_t *xbp, int len)
{
    unsigned delta = 0;
    xchar_t *cp, *ep, *ip;

    for (cp = xbp->xb_curp, ep = cp + len; cp < ep; cp++) {
	if (*cp == '\\')
	    delta += 1;
	else if (*cp == '"')
	    delta += 1;
    }

    if (delta == 0)		/* Nothing to escape; bail */
	return len;

    if (!xo_buf_has_room(xbp, delta)) /* No room; bail, but don't append */
	return 0;

    ep = xbp->xb_curp;
    cp = ep + len;
    ip = cp + delta;
    do {
	cp -= 1;
	ip -= 1;

	if (*cp != '\\' && *cp != '"') {
	    *ip = *cp;
	    continue;
	}

	*ip-- = *cp;
	*ip = '\\';
	
    } while (cp > ep && cp != ip);

    return len + delta;
}

/*
 * Append the given string to the given buffer
 */
static void
xo_buf_append (xo_buffer_t *xbp, const xchar_t *str, int len)
{
    if (!xo_buf_has_room(xbp, len))
	return;

    memcpy(xbp->xb_curp, str, len);
    xbp->xb_curp += len;
}

static void
xo_buf_escape (xo_handle_t *xop, xo_buffer_t *xbp, const xchar_t *str, int len)
{
    if (!xo_buf_has_room(xbp, len))
	return;

    memcpy(xbp->xb_curp, str, len);

    switch (xop->xo_style) {
    case XO_STYLE_XML:
    case XO_STYLE_HTML:
	len = xo_escape_xml(xbp, len, 0);
	break;

    case XO_STYLE_JSON:
	len = xo_escape_json(xbp, len);
	break;
    }

    xbp->xb_curp += len;
}

/*
 * Append the given string to the given buffer
 */
static void
xo_data_append (xo_handle_t *xop, const xchar_t *str, int len)
{
    xo_buf_append(&xop->xo_data, str, len);
}

/*
 * Append the given string to the given buffer
 */
static void
xo_data_escape (xo_handle_t *xop, const xchar_t *str, int len)
{
    xo_buf_escape(xop, &xop->xo_data, str, len);
}

/*
 * Cheap convenience function to return either the argument, or
 * the internal handle, after it has been initialized.  The usage
 * is:
 *    xop = xo_default(xop);
 */
static xo_handle_t *
xo_default (xo_handle_t *xop)
{
    if (xop == NULL) {
	if (xo_default_inited == 0)
	    xo_default_init();
	xop = &xo_default_handle;
    }

    return xop;
}

/*
 * Return the number of spaces we should be indenting.  If
 * we are pretty-printing, theis is indent * indent_by.
 */
static int
xo_indent (xo_handle_t *xop)
{
    xop = xo_default(xop);

    if (xop->xo_flags & XOF_PRETTY)
	return xop->xo_indent * xop->xo_indent_by;

    return 0;
}

/*
 * Generate a warning.  Normally, this is a text message written to
 * standard error.  If the XOF_WARN_XML flag is set, then we generate
 * XMLified content on standard output.
 */
static void
xo_warn (xo_handle_t *xop, const xchar_t *fmt, ...)
{
    va_list vap;
    int len = strlen(fmt);
    xchar_t *newfmt = alloca(len + 2);

    memcpy(newfmt, fmt, len);	/* Add a newline to the fmt string */
    newfmt[len] = '\n';
    newfmt[len + 1] = '\0';

    va_start(vap, fmt);
    if (xop->xo_flags & XOF_WARN_XML) {
	vfprintf(stderr, newfmt, vap);
    } else {
	vfprintf(stderr, newfmt, vap);
    }

    va_end(vap);
}

/**
 * Create a handle for use by later libxo functions.
 *
 * Note: normal use of libxo does not require a distinct handle, since
 * the default handle (used when NULL is passed) generates text on stdout.
 *
 * @style Style of output desired (XO_STYLE_* value)
 * @flags Set of XOF_* flags in use with this handle
 */
xo_handle_t *
xo_create (unsigned style, unsigned flags)
{
    xo_handle_t *xop = xo_realloc(NULL, sizeof(*xop));

    if (xop) {
	bzero(xop, sizeof(*xop));

	xop->xo_style  = style;
	xop->xo_flags = flags;
	xo_init_handle(xop);
    }

    return xop;
}

/**
 * Create a handle that will write to the given file.  Use
 * the XOF_CLOSE_FP flag to have the file closed on xo_destroy().
 * @fp FILE pointer to use
 * @style Style of output desired (XO_STYLE_* value)
 * @flags Set of XOF_* flags to use with this handle
 */
xo_handle_t *
xo_create_to_file (FILE *fp, unsigned style, unsigned flags)
{
    xo_handle_t *xop = xo_create(style, flags);

    if (xop) {
	xop->xo_opaque = fp;
	xop->xo_write = xo_write_to_file;
	xop->xo_close = xo_close_file;
    }

    return xop;
}

/**
 * Release any resources held by the handle.
 * @xop XO handle to alter (or NULL for default handle)
 */
void
xo_destroy (xo_handle_t *xop)
{
    xop = xo_default(xop);

    if (xop->xo_close && (xop->xo_flags & XOF_CLOSE_FP))
	xop->xo_close(xop->xo_opaque);

    xo_free(xop->xo_stack);
    xo_free(xop->xo_data.xb_bufp);
    xo_free(xop->xo_fmt.xb_bufp);
    if (xop->xo_style == XO_STYLE_XML)
	xo_free(xop->xo_attrs.xb_bufp);

    if (xop == &xo_default_handle) {
	bzero(&xo_default_handle, sizeof(&xo_default_handle));
	xo_default_inited = 0;
    } else
	xo_free(xop);
}

/**
 * Record a new output style to use for the given handle (or default if
 * handle is NULL).  This output style will be used for any future output.
 *
 * @xop XO handle to alter (or NULL for default handle)
 * @style new output style (XO_STYLE_*)
 */
void
xo_set_style (xo_handle_t *xop, unsigned style)
{
    xop = xo_default(xop);
    xop->xo_style = style;
}

/**
 * Set one or more flags for a given handle (or default if handle is NULL).
 * These flags will affect future output.
 *
 * @xop XO handle to alter (or NULL for default handle)
 * @flags Flags to be set (XOF_*)
 */
void
xo_set_flags (xo_handle_t *xop, unsigned flags)
{
    xop = xo_default(xop);

    xop->xo_flags |= flags;
}

/**
 * Record the info data for a set of tags
 *
 * @xop XO handle to alter (or NULL for default handle)
 * @info Info data (xo_info_t) to be recorded (or NULL) (MUST BE SORTED)
 * @count Number of entries in info (or -1 to count them ourselves)
 */
void
xo_set_info (xo_handle_t *xop, xo_info_t *infop, int count)
{
    xop = xo_default(xop);

    if (count < 0 && infop) {
	xo_info_t *xip;

	for (xip = infop, count = 0; xip->xi_name; xip++, count++)
	    continue;
    }

    xop->xo_info = infop;
    xop->xo_info_count = count;
}

/**
 * Set the formatter callback for a handle.  The callback should
 * return a newly formatting contents of a formatting instruction,
 * meaning the bits inside the braces.
 */
void
xo_set_formatter (xo_handle_t *xop, xo_formatter_t func)
{
    xop = xo_default(xop);

    xop->xo_formatter = func;
}

/**
 * Clear one or more flags for a given handle (or default if handle is NULL).
 * These flags will affect future output.
 *
 * @xop XO handle to alter (or NULL for default handle)
 * @flags Flags to be cleared (XOF_*)
 */
void
xo_clear_flags (xo_handle_t *xop, unsigned flags)
{
    xop = xo_default(xop);

    xop->xo_flags &= ~flags;
}

static void
xo_buf_indent (xo_handle_t *xop, int indent)
{
    xo_buffer_t *xbp = &xop->xo_data;

    if (indent <= 0)
	indent = xo_indent(xop);

    if (!xo_buf_has_room(xbp, indent))
	return;

    memset(xbp->xb_curp, ' ', indent);
    xbp->xb_curp += indent;
}

static void
xo_line_ensure_open (xo_handle_t *xop, unsigned flags UNUSED)
{
    static xchar_t div_open[] = W "<div class=\"line\">";

    if (xop->xo_flags & XOF_DIV_OPEN)
	return;

    if (xop->xo_style != XO_STYLE_HTML)
	return;

    xop->xo_flags |= XOF_DIV_OPEN;
    xo_data_append(xop, div_open, sizeof(div_open) - 1);

    if (xop->xo_flags & XOF_PRETTY)
	xo_data_append(xop, "\n", 1);
}

static void
xo_line_close (xo_handle_t *xop)
{
    static xchar_t div_close[] = W "</div>";

    switch (xop->xo_style) {
    case XO_STYLE_HTML:
	if (!(xop->xo_flags & XOF_DIV_OPEN))
	    xo_line_ensure_open(xop, 0);

	xop->xo_flags &= ~XOF_DIV_OPEN;
	xo_data_append(xop, div_close, sizeof(div_close) - 1);

	if (xop->xo_flags & XOF_PRETTY)
	    xo_data_append(xop, "\n", 1);
	break;

    case XO_STYLE_TEXT:
	xo_data_append(xop, "\n", 1);
	break;
    }
}

static int
xo_info_compare (const void *key, const void *data)
{
    const xchar_t *name = key;
    const xo_info_t *xip = data;

    return strcmp(name, xip->xi_name);
}

static xo_info_t *
xo_info_find (xo_handle_t *xop, const xchar_t *name, int nlen)
{
    xo_info_t *xip;
    xchar_t *cp = alloca(nlen + 1); /* Need local copy for NUL termination */

    memcpy(cp, name, nlen);
    cp[nlen] = '\0';

    xip = bsearch(cp, xop->xo_info, xop->xo_info_count,
		  sizeof(xop->xo_info[0]), xo_info_compare);
    return xip;
}

static int
xo_format_data (xo_handle_t *xop, const xchar_t *fmt, int flen, unsigned flags)
{
    const xchar_t *cp, *ep, *sp;
    xo_buffer_t *xbp = &xop->xo_data;
    unsigned skip, lflag, hflag, jflag, tflag, zflag, qflag, stars;
    int rc;
    va_list va_local;
    int delta = 0;
    
    for (cp = fmt, ep = fmt + flen; cp < ep; cp++) {
	if (*cp != '%') {
	add_one:
	    xo_buf_escape(xop, xbp, cp, 1);
	    continue;

	} if (cp + 1 < ep && cp[1] == '%') {
	    cp += 1;
	    goto add_one;
	}

	skip = lflag = hflag = jflag = tflag = zflag = qflag = stars = 0;
	rc = 0;

	/*
	 * "%@" starts an XO-specific set of flags:
	 *   @X@ - XML-only field; ignored if style isn't XML
	 */
	if (cp[1] == '@') {
	    for (cp += 2; cp < ep; cp++) {
		if (*cp == '@') {
		    break;
		}
		if (*cp == '*') {
		    /*
		     * '*' means there's a "%*.*s" value in vap that
		     * we want to ignore
		     */
		    va_arg(xop->xo_vap, int);
		}
	    }
	}

	/* Hidden fields are only visible to JSON and XML */
	if (flags & XFF_HIDE_TEXT) {
	    if (xop->xo_style != XO_STYLE_XML
		    && xop->xo_style != XO_STYLE_JSON)
		skip = 1;
	} else if (flags & XFF_HIDE_DATA) {
	    if (xop->xo_style != XO_STYLE_TEXT
		    && xop->xo_style != XO_STYLE_HTML)
		skip = 1;
	}

	/*
	 * Looking at one piece of a format; find the end and
	 * call snprintf.  Then advance xo_vap on our own.
	 *
	 * Note that 'n', 'v', and '$' are not supported.
	 */
	sp = cp;		/* Save start pointer */
	for (cp += 1; cp < ep; cp++) {
	    if (*cp == 'l')
		lflag += 1;
	    else if (*cp == 'h')
		hflag += 1;
	    else if (*cp == 'j')
		jflag += 1;
	    else if (*cp == 't')
		tflag += 1;
	    else if (*cp == 'z')
		zflag += 1;
	    else if (*cp == 'q')
		qflag += 1;
	    else if (*cp == '*')
		stars += 1;
	    else if (strchr(W "diouxXDOUeEfFgGaAcCsSp", *cp) != NULL)
		break;
	    else if (*cp == 'n' || *cp == 'v') {
		if (xop->xo_flags & XOF_WARN)
		    xo_warn(xop, "unsupported format: '%s'", fmt);
		return -1;
	    }
	}

	if (!skip) {
	    xo_buffer_t *fbp = &xop->xo_fmt;
	    int len = cp - sp + 1;
	    if (!xo_buf_has_room(fbp, len + 1))
		return -1;

	    xchar_t *newfmt = fbp->xb_curp;
	    memcpy(newfmt, sp, len);
	    newfmt[0] = '%';	/* If we skipped over a "%@...@s" format */
	    newfmt[len] = '\0';

	    /*
	     * After we call vsnprintf(), the stage of vap is not defined.
	     * We need to copy it before we pass.  Then we have to do our
	     * own logic below to move it along.  This is because the
	     * implementation can have va_list be a point (bsd) or a
	     * structure (macosx) or anything in between.
	     */
	    va_copy(va_local, xop->xo_vap);

	    int left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	    rc = vsnprintf(xbp->xb_curp, left, newfmt, va_local);
	    if (rc > left) {
		if (!xo_buf_has_room(xbp, rc))
		    return -1;

		/* Need a fresh copy */
		va_end(va_local);
		va_copy(va_local, xop->xo_vap);

		left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
		rc = vsnprintf(xbp->xb_curp, rc, newfmt, va_local);
	    }

	    va_end(va_local);

	    /*
	     * For XML and HTML, we need "&<>" processing; for JSON,
	     * it's quotes.  Text gets nothing.
	     */
	    switch (xop->xo_style) {
	    case XO_STYLE_XML:
	    case XO_STYLE_HTML:
		rc = xo_escape_xml(xbp, rc, 0);
		break;

	    case XO_STYLE_JSON:
		rc = xo_escape_json(xbp, rc);
		break;
	    }

	    xbp->xb_curp += rc;
	    delta += rc;
	}

	/*
	 * Now for the tricky part: we need to move the argument pointer
	 * along by the amount needed.
	 */
	xchar_t fc = *cp;
	/* Handle "%*.*s" */
	if (stars > 0) {
	    va_arg(xop->xo_vap, int);
	    if (stars > 1)
		va_arg(xop->xo_vap, int);
	}

	if (fc == 'D' || fc == 'O' || fc == 'U')
	    lflag = 1;

	if (strchr(W "diouxXDOU", fc) != NULL) {
	    if (hflag > 1) {
		va_arg(xop->xo_vap, int);

	    } else if (hflag > 0) {
		va_arg(xop->xo_vap, int);

	    } else if (lflag > 1) {
		va_arg(xop->xo_vap, unsigned long long);

	    } else if (lflag > 0) {
		va_arg(xop->xo_vap, unsigned long);

	    } else if (jflag > 0) {
		va_arg(xop->xo_vap, intmax_t);

	    } else if (tflag > 0) {
		va_arg(xop->xo_vap, ptrdiff_t);

	    } else if (zflag > 0) {
		va_arg(xop->xo_vap, size_t);

	    } else if (qflag > 0) {
		va_arg(xop->xo_vap, quad_t);

	    } else {
		va_arg(xop->xo_vap, int);
	    }
	} else if (strchr(W "eEfFgGaA", fc) != NULL)
	    if (lflag)
		va_arg(xop->xo_vap, long double);
	    else
		va_arg(xop->xo_vap, double);

	else if (fc == 'C' || (fc == 'c' && lflag))
	    va_arg(xop->xo_vap, wint_t);

	else if (fc == 'c')
	    va_arg(xop->xo_vap, int);

	else if (fc == 'S' || (fc == 's' && lflag))
	    va_arg(xop->xo_vap, wchar_t *);

	else if (fc == 's')
	    va_arg(xop->xo_vap, char *);

	else if (fc == 'p')
	    va_arg(xop->xo_vap, void *);
    }

    return delta;
}

static void
xo_buf_append_div (xo_handle_t *xop, const xchar_t *class,
		   const xchar_t *name, int nlen,
		   const xchar_t *value, int vlen)
{
    static xchar_t div1[] = W "<div class=\"";
    static xchar_t div2[] = W "\" data-tag=\"";
    static xchar_t div3[] = W "\" data-xpath=\"";
    static xchar_t div4[] = W "\">";
    static xchar_t div5[] = W "</div>";

    xo_line_ensure_open(xop, 0);

    if (xop->xo_flags & XOF_PRETTY)
	xo_buf_indent(xop, xop->xo_indent_by);

    xo_data_append(xop, div1, sizeof(div1) - 1);
    xo_data_append(xop, class, strlen(class));

    if (name) {
	xo_data_append(xop, div2, sizeof(div2) - 1);
	xo_data_escape(xop, name, nlen);
    }

    if (name && (xop->xo_flags & XOF_XPATH)) {
	int i;
	xo_stack_t *xsp;

	xo_data_append(xop, div3, sizeof(div3) - 1);
	for (i = 0; i <= xop->xo_depth; i++) {
	    xsp = &xop->xo_stack[i];
	    if (xsp->xs_name == NULL)
		continue;

	    xo_data_append(xop, "/", 1);
	    xo_data_escape(xop, xsp->xs_name, strlen(xsp->xs_name));
	}

	xo_data_append(xop, "/", 1);
	xo_data_escape(xop, name, nlen);
    }

    if (name && (xop->xo_flags & XOF_INFO) && xop->xo_info) {
	static xchar_t in_type[] = W "\" data-type=\"";
	static xchar_t in_help[] = W "\" data-help=\"";

	xo_info_t *xip = xo_info_find(xop, name, nlen);
	if (xip) {
	    if (xip->xi_type) {
		xo_data_append(xop, in_type, sizeof(in_type) - 1);
		xo_data_escape(xop, xip->xi_type, strlen(xip->xi_type));
	    }
	    if (xip->xi_help) {
		xo_data_append(xop, in_help, sizeof(in_help) - 1);
		xo_data_escape(xop, xip->xi_help, strlen(xip->xi_help));
	    }
	}
    }

    xo_data_append(xop, div4, sizeof(div4) - 1);
    xo_format_data(xop, value, vlen, 0);
    xo_data_append(xop, div5, sizeof(div5) - 1);

    if (xop->xo_flags & XOF_PRETTY)
	xo_data_append(xop, "\n", 1);
}

static void
xo_format_text (xo_handle_t *xop, const xchar_t *str, int len)
{
    switch (xop->xo_style) {
    case XO_STYLE_TEXT:
	xo_buf_append(&xop->xo_data, str, len);
	break;

    case XO_STYLE_HTML:
	xo_buf_append_div(xop, "text", NULL, 0, str, len);
	break;
    }
}

static void
xo_format_label (xo_handle_t *xop, const xchar_t *str, int len)
{
    switch (xop->xo_style) {
    case XO_STYLE_TEXT:
	xo_buf_append(&xop->xo_data, str, len);
	break;

    case XO_STYLE_HTML:
	xo_buf_append_div(xop, "label", NULL, 0, str, len);
	break;
    }
}

static void
xo_format_title (xo_handle_t *xop, const xchar_t *str, int len,
		 const xchar_t *fmt, int flen)
{
    static xchar_t div_open[] = W "<div class=\"title\">";
    static xchar_t div_close[] = W "</div>";

    if (xop->xo_style != XO_STYLE_TEXT && xop->xo_style != XO_STYLE_HTML)
	return;

    xo_buffer_t *xbp = &xop->xo_data;
    int start = xbp->xb_curp - xbp->xb_bufp;
    int left = xbp->xb_size - start;
    int rc;

    if (xop->xo_style == XO_STYLE_HTML) {
	xo_line_ensure_open(xop, 0);
	if (xop->xo_flags & XOF_PRETTY)
	    xo_buf_indent(xop, xop->xo_indent_by);
	xo_buf_append(&xop->xo_data, div_open, sizeof(div_open) - 1);
    }

    start = xbp->xb_curp - xbp->xb_bufp; /* Reset start */
    if (len) {
	xchar_t *newfmt = alloca(flen + 1);
	memcpy(newfmt, fmt, flen);
	newfmt[flen] = '\0';

	/* If len is non-zero, the format string apply to the name */
	xchar_t *newstr = alloca(len + 1);
	memcpy(newstr, str, len);
	newstr[len] = '\0';

	rc = snprintf(xbp->xb_curp, left, newfmt, newstr);
	if (rc > left) {
	    if (!xo_buf_has_room(xbp, rc))
		return;
	    left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	    rc = snprintf(xbp->xb_curp, left, newfmt, newstr);
	}

    } else {
	rc = xo_format_data(xop, fmt, flen, 0);
	/* xo_format_data moved curp, so we need to reset it */
	xbp->xb_curp = xbp->xb_bufp + start;
    }

    /* If we're styling HTML, then we need to escape it */
    if (xop->xo_style == XO_STYLE_HTML) {
	rc = xo_escape_xml(xbp, rc, 0);
    }

    xbp->xb_curp += rc;

    if (xop->xo_style == XO_STYLE_HTML) {
	xo_data_append(xop, div_close, sizeof(div_close) - 1);
	if (xop->xo_flags & XOF_PRETTY)
	    xo_data_append(xop, "\n", 1);
    }
}

static void
xo_format_prep (xo_handle_t *xop)
{
    if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST) {
	xo_data_append(xop, ",", 1);
	if (xop->xo_flags & XOF_PRETTY)
	    xo_data_append(xop, "\n", 1);
    } else
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
}

static void
xo_format_value (xo_handle_t *xop, const xchar_t *name, int nlen,
		 const xchar_t *format, int flen,
		 const xchar_t *encoding, int elen, unsigned flags)
{
    int pretty = (xop->xo_flags & XOF_PRETTY);
    int quote;

    switch (xop->xo_style) {
    case XO_STYLE_TEXT:
	xo_format_data(xop, format, flen, flags);
	break;

    case XO_STYLE_HTML:
	xo_buf_append_div(xop, "data", name, nlen,
			  format, flen);
	break;

    case XO_STYLE_XML:
	if (encoding) {
	    format = encoding;
	    flen = elen;
	}

	if (pretty)
	    xo_buf_indent(xop, -1);
	xo_data_append(xop, "<", 1);
	xo_data_escape(xop, name, nlen);

	if (xop->xo_attrs.xb_curp != xop->xo_attrs.xb_bufp) {
	    xo_data_append(xop, xop->xo_attrs.xb_bufp,
			   xop->xo_attrs.xb_curp - xop->xo_attrs.xb_bufp);
	    xop->xo_attrs.xb_curp = xop->xo_attrs.xb_bufp;
	}

	xo_data_append(xop, ">", 1);
	xo_format_data(xop, format, flen, flags);
	xo_data_append(xop, "</", 2);
	xo_data_escape(xop, name, nlen);
	xo_data_append(xop, ">", 1);
	if (pretty)
	    xo_data_append(xop, "\n", 1);
	break;

    case XO_STYLE_JSON:
	if (encoding) {
	    format = encoding;
	    flen = elen;
	}

	xo_format_prep(xop);

	if (flags & XFF_QUOTE)
	    quote = 1;
	else if (flags & XFF_NOQUOTE)
	    quote = 0;
	else if (format[flen - 1] == 's')
	    quote = 1;
	else
	    quote = 0;

	if (pretty)
	    xo_buf_indent(xop, -1);
	xo_data_append(xop, "\"", 1);
	xo_data_escape(xop, name, nlen);
	xo_data_append(xop, "\":", 2);

	if (pretty)
	    xo_data_append(xop, " ", 1);
	if (quote)
	    xo_data_append(xop, "\"", 1);

	xo_format_data(xop, format, flen, flags);

	if (quote)
	    xo_data_append(xop, "\"", 1);
	break;
    }
}

static void
xo_format_decoration (xo_handle_t *xop, const xchar_t *str, int len)
{
    switch (xop->xo_style) {
    case XO_STYLE_TEXT:
	xo_data_append(xop, str, len);
	break;

    case XO_STYLE_HTML:
	xo_buf_append_div(xop, "decoration", NULL, 0, str, len);
	break;
    }
}

static void
xo_format_padding (xo_handle_t *xop, const xchar_t *str, int len)
{
    switch (xop->xo_style) {
    case XO_STYLE_TEXT:
	xo_data_append(xop, str, len);
	break;

    case XO_STYLE_HTML:
	xo_buf_append_div(xop, "padding", NULL, 0, str, len);
	break;
    }
}

static int
xo_do_emit (xo_handle_t *xop, const xchar_t *fmt)
{
    xo_buffer_t *xbp = &xop->xo_data;
    int rc = 0;
    const xchar_t *cp, *sp, *ep, *basep;
    xchar_t *newp = NULL;

    for (cp = fmt; *cp; ) {
	if (*cp == '\n') {
	    xo_line_close(xop);
	    cp += 1;
	    continue;

	} else if (*cp == '{') {
	    if (cp[1] == '{') {	/* Start of {{escaped braces}} */

		cp += 2;	/* Skip over _both_ characters */
		for (sp = cp; *sp; sp++) {
		    if (*sp == '}' && sp[1] == '}')
			break;
		}
		if (*sp == '\0' && (xop->xo_flags & XOF_WARN))
		    xo_warn(xop, "missing closing '}}': %s", fmt);

		xo_format_text(xop, cp, sp - cp);

		/* Move along the string, but don't run off the end */
		if (*sp == '}' && sp[1] == '}')
		    sp += 2;
		cp = *sp ? sp + 1 : sp;
		continue;
	    }
	    /* Else fall thru to the code below */

	} else {
	    /* Normal text */
	    for (sp = cp; *sp; sp++) {
		if (*sp == '{' || *sp == '\n')
		    break;
	    }
	    xo_format_text(xop, cp, sp - cp);

	    cp = sp;
	    continue;
	}

	/*
	 * A customer formatter gives the caller a pre-format
	 * hook for changing data before it gets processed.
	 */
	basep = cp + 1;
	if (xop->xo_formatter) {
	    for (ep = basep; *ep; ep++) {
		if (*ep == '}')
		    break;
	    }

	    if (*ep == '\0' && (xop->xo_flags & XOF_WARN))
		xo_warn(xop, "missing closing '}': %s", fmt);

	    int tlen = ep - cp;
	    xchar_t *tmp = alloca(tlen + 1);
	    memcpy(tmp, cp + 1, tlen);
	    tmp[tlen] = '\0';

	    newp = xop->xo_formatter(xop, tmp);
	    if (newp) {
		basep = newp;
	    }
	}

	/*
	 * We are looking at the start of a braces pattern.  The format is:
	 *  '{' modifiers ':' content [ '/' print-fmt [ '/' encode-fmt ]] '}'
	 * Modifiers are optional, but are:
	 *   'D': decoration; something non-text and non-data (colons, commmas)
	 *   'L': label; text surrounding data
	 *   'P': padding; whitespace
	 *   'T': Title, where 'content' is a column title
	 *   'V': value, where 'content' is the name of the field
	 *   'C': flag: emit a colon after the label
	 *   'W': emit a blank after the label
	 *   'H': field is hidden from text output
	 * The print-fmt and encode-fmt strings is the printf-style formating
	 * for this data.  JSON and XML will use the encoding-fmt, if present.
	 * If the encode-fmt is not provided, it defaults to the print-fmt.
	 * If the print-fmt is not provided, it defaults to 's'.
	 */
	unsigned style = 0, flags = 0;
	const xchar_t *content = NULL, *format = NULL, *encoding = NULL;
	int clen = 0, flen = 0, elen = 0;

	for (sp = basep; sp; sp++) {
	    if (*sp == ':' || *sp == '/' || *sp == '}')
		break;

	    switch (*sp) {
	    case 'D':
	    case 'L':
	    case 'P':
	    case 'T':
	    case 'V':
		if ((xop->xo_flags & XOF_WARN) && style != 0)
		    xo_warn(xop, "format string uses multiple styles: %s",
			    fmt);
		style = *sp;
		break;

	    case 'C':
		flags |= XFF_COLON;
		break;

	    case 'H':
		flags |= XFF_HIDE_TEXT;
		break;

	    case 'h':
		flags |= XFF_HIDE_DATA;
		break;

	    case 'N':
		flags |= XFF_NOQUOTE;
		break;

	    case 'Q':
		flags |= XFF_QUOTE;
		break;

	    case 'W':
		flags |= XFF_WS;
		break;

	    default:
		if (xop->xo_flags & XOF_WARN)
		    xo_warn(xop, "format string uses unknown modifier: %s",
			    fmt);
	    }
	}

	if (*sp == ':') {
	    for (ep = ++sp; *sp; sp++) {
		if (*sp == '}' || *sp == '/')
		    break;
	    }
	    if (ep != sp) {
		clen = sp - ep;
		content = ep;
	    }
	} else if (xop->xo_flags & XOF_WARN)
	    xo_warn(xop, "missing content (':'): %s", fmt);

	if (*sp == '/') {
	    for (ep = ++sp; *sp; sp++) {
		if (*sp == '}' || *sp == '/')
		    break;
	    }
	    if (ep != sp) {
		flen = sp - ep;
		format = ep;
	    }
	}


	if (*sp == '/') {
	    for (ep = ++sp; *sp; sp++) {
		if (*sp == '}')
		    break;
	    }
	    if (ep != sp) {
		elen = sp - ep;
		encoding = ep;
	    }
	}

	if (*sp == '}') {
	    sp += 1;
	} else if (xop->xo_flags & XOF_WARN)
	    xo_warn(xop, "missing closing '}': %s", fmt);

	if (format == NULL) {
	    format = W "%s";
	    flen = 2;
	}

	if (style == 'T')
	    xo_format_title(xop, content, clen, format, flen);
	else if (style == 'L')
	    xo_format_label(xop, content, clen);
	else if (style == 0 || style == 'V')
	    xo_format_value(xop, content, clen, format, flen,
			    encoding, elen, flags);
	else if (style == 'D')
	    xo_format_decoration(xop, content, clen);
	else if (style == 'P')
 	    xo_format_padding(xop, content, clen);

	if (flags & XFF_COLON)
	    xo_format_decoration(xop, ":", 1);
	if (flags & XFF_WS)
	    xo_format_padding(xop, " ", 1);

	cp += sp - basep + 1;
	if (newp) {
	    xo_free(newp);
	    newp = NULL;
	}
    }

    xo_buf_append(xbp, "", 1); /* Append ending NUL */

    xop->xo_write(xop->xo_opaque, xbp->xb_bufp);
    xbp->xb_curp = xbp->xb_bufp;

    return rc;
}

int
xo_emit_hv (xo_handle_t *xop, const xchar_t *fmt, va_list vap)
{
    int rc;

    xop = xo_default(xop);
    va_copy(xop->xo_vap, vap);
    rc = xo_do_emit(xop, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

int
xo_emit_h (xo_handle_t *xop, const xchar_t *fmt, ...)
{
    int rc;

    xop = xo_default(xop);
    va_start(xop->xo_vap, fmt);
    rc = xo_do_emit(xop, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

int
xo_emit (const xchar_t *fmt, ...)
{
    xo_handle_t *xop = xo_default(NULL);
    int rc;

    va_start(xop->xo_vap, fmt);
    rc = xo_do_emit(xop, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

int
xo_attr_hv (xo_handle_t *xop, const xchar_t *name, const xchar_t *fmt, va_list vap)
{
    const int extra = 5; 	/* space, equals, quote, quote, and nul */
    xop = xo_default(xop);

    if (xop->xo_style != XO_STYLE_XML)
	return 0;

    int nlen = strlen(name);
    xo_buffer_t *xbp = &xop->xo_attrs;

    if (!xo_buf_has_room(xbp, nlen + extra))
	return -1;

    *xbp->xb_curp++ = ' ';
    memcpy(xbp->xb_curp, name, nlen);
    xbp->xb_curp += nlen;
    *xbp->xb_curp++ = '=';
    *xbp->xb_curp++ = '"';

    int left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
    va_list va_local;

    va_copy(va_local, vap);
    int rc = vsnprintf(xbp->xb_curp, left, fmt, va_local);
    if (rc > xbp->xb_size) {
	if (!xo_buf_has_room(xbp, rc))
	    return -1;

	va_end(va_local);	/* Reset vap to the start */
	va_copy(va_local, vap);

	left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	rc = vsnprintf(xbp->xb_curp, left, fmt, va_local);
    }
    va_end(va_local);

    rc = xo_escape_xml(xbp, rc, 1);
    xbp->xb_curp += rc;

    if (!xo_buf_has_room(xbp, 2))
	return -1;

    *xbp->xb_curp++ = '"';
    *xbp->xb_curp = '\0';

    return rc + nlen + extra;
}

int
xo_attr_h (xo_handle_t *xop, const xchar_t *name, const xchar_t *fmt, ...)
{
    int rc;
    va_list vap;

    va_start(vap, fmt);
    rc = xo_attr_hv(xop, name, fmt, vap);
    va_end(vap);

    return rc;
}

int
xo_attr (const xchar_t *name, const xchar_t *fmt, ...)
{
    int rc;
    va_list vap;

    va_start(vap, fmt);
    rc = xo_attr_hv(NULL, name, fmt, vap);
    va_end(vap);

    return rc;
}

static void
xo_depth_change (xo_handle_t *xop, const xchar_t *name,
		 int delta, int indent, unsigned flags)
{
    xo_stack_t *xsp = &xop->xo_stack[xop->xo_depth];

    if (delta >= 0) {			/* Push operation */
	xsp += delta;
	xsp->xs_flags = flags;

	if (name && (xop->xo_flags & (XOF_XPATH | XOF_WARN))) {
	    int len = strlen(name) + 1;
	    xchar_t *cp = xo_realloc(NULL, len);
	    if (cp) {
		memcpy(cp, name, len);
		xsp->xs_name = cp;
	    }
	}

    } else {			/* Pop operation */
	if (xop->xo_depth == 0) {
	    if (xop->xo_flags & XOF_WARN)
		xo_warn(xop, "xo: close with empty stack: '%s'", name);
	    return;
	}

	if (xop->xo_flags & XOF_WARN) {
	    const xchar_t *top = xsp->xs_name;
	    if (top && strcmp(name, top) != 0)
		xo_warn(xop, "xo: incorrect close: '%s' .vs. '%s'", name, top);
	    if ((xsp->xs_flags & XSF_LIST) != (flags & XSF_LIST))
		xo_warn(xop, "xo: list close on list confict: '%s'", name);
	    if ((xsp->xs_flags & XSF_INSTANCE) != (flags & XSF_INSTANCE))
		xo_warn(xop, "xo: list close on instance confict: '%s'", name);
	}

	if (xop->xo_flags & XOF_XPATH) {
	    if (xsp->xs_name) {
		xo_free(xsp->xs_name);
		xsp->xs_name = NULL;
	    }
	}
    }

    xop->xo_depth += delta;	/* Record new depth */
    xop->xo_indent += indent;
}

int
xo_open_container_h (xo_handle_t *xop, const xchar_t *name)
{
    xop = xo_default(xop);

    int rc = 0;
    const xchar_t *ppn = (xop->xo_flags & XOF_PRETTY) ? W "\n" : W "";
    const xchar_t *pre_nl = W "";

    switch (xop->xo_style) {
    case XO_STYLE_XML:
	rc = xo_printf(xop, "%*s<%s>%s", xo_indent(xop), "",
		     name, ppn);
	xo_depth_change(xop, name, 1, 1, 0);
	break;

    case XO_STYLE_JSON:
	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = (xop->xo_flags & XOF_PRETTY) ? ",\n" : ", ";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	rc = xo_printf(xop, "%s%*s\"%s\": {%s",
		       pre_nl, xo_indent(xop), "", name, ppn);
	xo_depth_change(xop, name, 1, 1, 0);
	break;

    case XO_STYLE_HTML:
    case XO_STYLE_TEXT:
	xo_depth_change(xop, name, 1, 0, 0);
	break;
    }

    return rc;
}

int
xo_open_container (const xchar_t *name)
{
    return xo_open_container_h(NULL, name);
}

int
xo_close_container_h (xo_handle_t *xop, const xchar_t *name)
{
    xop = xo_default(xop);

    int rc = 0;
    const xchar_t *ppn = (xop->xo_flags & XOF_PRETTY) ? "\n" : "";
    const xchar_t *pre_nl = W "";

    switch (xop->xo_style) {
    case XO_STYLE_XML:
	xo_depth_change(xop, name, -1, -1, 0);
	rc = xo_printf(xop, "%*s</%s>%s", xo_indent(xop), "", name, ppn);
	break;

    case XO_STYLE_JSON:
	pre_nl = (xop->xo_flags & XOF_PRETTY) ? "\n" : "";
	ppn = (xop->xo_depth <= 1) ? "\n" : "";

	xo_depth_change(xop, name, -1, -1, 0);
	rc = xo_printf(xop, "%s%*s}%s", pre_nl, xo_indent(xop), "", ppn);
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;

    case XO_STYLE_HTML:
    case XO_STYLE_TEXT:
	xo_depth_change(xop, name, -1, 0, 0);
	break;
    }

    return rc;
}

int
xo_close_container (const xchar_t *name)
{
    return xo_close_container_h(NULL, name);
}

int
xo_open_list_h (xo_handle_t *xop, const xchar_t *name UNUSED)
{
    xop = xo_default(xop);

    int rc = 0;
    const xchar_t *ppn = (xop->xo_flags & XOF_PRETTY) ? "\n" : "";
    const xchar_t *pre_nl = W "";

    switch (xop->xo_style) {
    case XO_STYLE_JSON:
	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = (xop->xo_flags & XOF_PRETTY) ? ",\n" : ", ";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	rc = xo_printf(xop, "%s%*s\"%s\": [%s",
		       pre_nl, xo_indent(xop), "", name, ppn);
	xo_depth_change(xop, name, 1, 1, XSF_LIST);
	break;
    }

    return rc;
}

int
xo_open_list (const xchar_t *name)
{
    return xo_open_list_h(NULL, name);
}

int
xo_close_list_h (xo_handle_t *xop, const xchar_t *name UNUSED)
{
    int rc = 0;
    const xchar_t *pre_nl = W "";

    xop = xo_default(xop);

    switch (xop->xo_style) {
    case XO_STYLE_JSON:
	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = (xop->xo_flags & XOF_PRETTY) ? "\n" : "";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	xo_depth_change(xop, name, -1, -1, XSF_LIST);
	rc = xo_printf(xop, "%s%*s]", pre_nl, xo_indent(xop), "");
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;
    }

    return 0;
}

int
xo_close_list (const xchar_t *name)
{
    return xo_close_list_h(NULL, name);
}

int
xo_open_instance_h (xo_handle_t *xop, const xchar_t *name)
{
    xop = xo_default(xop);

    int rc = 0;
    const xchar_t *ppn = (xop->xo_flags & XOF_PRETTY) ? "\n" : "";
    const xchar_t *pre_nl = W "";

    switch (xop->xo_style) {
    case XO_STYLE_XML:
	rc = xo_printf(xop, "%*s<%s>%s", xo_indent(xop), "", name, ppn);
	xo_depth_change(xop, name, 1, 1, 0);
	break;

    case XO_STYLE_JSON:
	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = (xop->xo_flags & XOF_PRETTY) ? ",\n" : ", ";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	rc = xo_printf(xop, "%s%*s{%s",
		       pre_nl, xo_indent(xop), "", ppn);
	xo_depth_change(xop, name, 1, 1, 0);
	break;

    case XO_STYLE_HTML:
    case XO_STYLE_TEXT:
	xo_depth_change(xop, name, 1, 0, 0);
	break;
    }

    return rc;
}

int
xo_open_instance (const xchar_t *name)
{
    return xo_open_instance_h(NULL, name);
}

int
xo_close_instance_h (xo_handle_t *xop, const xchar_t *name)
{
    xop = xo_default(xop);

    int rc = 0;
    const xchar_t *ppn = (xop->xo_flags & XOF_PRETTY) ? "\n" : "";
    const xchar_t *pre_nl = W "";

    switch (xop->xo_style) {
    case XO_STYLE_XML:
	xo_depth_change(xop, name, -1, -1, 0);
	rc = xo_printf(xop, "%*s</%s>%s", xo_indent(xop), "", name, ppn);
	break;

    case XO_STYLE_JSON:
	pre_nl = (xop->xo_flags & XOF_PRETTY) ? "\n" : "";

	xo_depth_change(xop, name, -1, -1, 0);
	rc = xo_printf(xop, "%s%*s}", pre_nl, xo_indent(xop), "");
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;

    case XO_STYLE_HTML:
    case XO_STYLE_TEXT:
	xo_depth_change(xop, name, -1, 0, 0);
	break;
    }

    return rc;
}

int
xo_close_instance (const xchar_t *name)
{
    return xo_close_instance_h(NULL, name);
}
void
xo_set_writer (xo_handle_t *xop, void *opaque, xo_write_func_t write_func,
	       xo_close_func_t close_func)
{
    xop = xo_default(xop);

    xop->xo_opaque = opaque;
    xop->xo_write = write_func;
    xop->xo_close = close_func;
}

void
xo_set_allocator (xo_realloc_func_t realloc_func, xo_free_func_t free_func)
{
    xo_realloc = realloc_func;
    xo_free = free_func;
}

/*
 * Generate an error message, such as would be displayed on stderr
 */
void
xo_error_hv (xo_handle_t *xop, const char *fmt, va_list vap)
{
    xop = xo_default(xop);

    switch (xop->xo_style) {
    case XO_STYLE_TEXT:
	vfprintf(stderr, fmt, vap);
	break;

    case XO_STYLE_HTML:
	va_copy(xop->xo_vap, vap);
	
	xo_buf_append_div(xop, "error", NULL, 0, fmt, strlen(fmt));

	if (xop->xo_flags & XOF_DIV_OPEN)
	    xo_line_close(xop);

	xo_buffer_t *xbp = &xop->xo_data;
	xbp->xb_curp = xbp->xb_bufp;
	xop->xo_write(xop->xo_opaque, xbp->xb_bufp);

	va_end(xop->xo_vap);
	bzero(&xop->xo_vap, sizeof(xop->xo_vap));
	break;

    case XO_STYLE_XML:
	va_copy(xop->xo_vap, vap);

	xo_open_container_h(xop, "error");
	xo_format_value(xop, "message", 7, fmt, strlen(fmt), NULL, 0, 0);
	xo_close_container_h(xop, "error");

	va_end(xop->xo_vap);
	bzero(&xop->xo_vap, sizeof(xop->xo_vap));
	break;
    }
}

void
xo_error_h (xo_handle_t *xop, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_error_hv(xop, fmt, vap);
    va_end(vap);
}

/*
 * Generate an error message, such as would be displayed on stderr
 */
void
xo_error (const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_error_hv(NULL, fmt, vap);
    va_end(vap);
}

#ifdef UNIT_TEST
int
main (int argc, char **argv)
{
    static xchar_t base_grocery[] = W "GRO";
    static xchar_t base_hardware[] = W "HRD";
    struct item {
	const xchar_t *i_title;
	int i_sold;
	int i_instock;
	int i_onorder;
	const xchar_t *i_sku_base;
	int i_sku_num;
    };
    struct item list[] = {
	{ W "gum&this&that", 1412, 54, 10, base_grocery, 415 },
	{ W "<rope>", 85, 4, 2, base_hardware, 212 },
	{ W "ladder", 0, 2, 1, base_hardware, 517 },
	{ W "\"bolt\"", 4123, 144, 42, base_hardware, 632 },
	{ W "water\\blue", 17, 14, 2, base_grocery, 2331 },
	{ NULL, 0, 0, 0, NULL, 0 }
    };
    struct item list2[] = {
	{ W "fish", 1321, 45, 1, base_grocery, 533 },
	{ NULL, 0, 0, 0, NULL, 0 }
    };
    struct item *ip;
    xo_info_t info[] = {
	{ W "in-stock", W "number", W "Number of items in stock" },
	{ W "name", W "string", W "Name of the item" },
	{ W "on-order", W "number", W "Number of items on order" },
	{ W "sku", W "string", W "Stock Keeping Unit" },
	{ W "sold", W "number", W "Number of items sold" },
	{ NULL, NULL, NULL },
    };
    int info_count = (sizeof(info) / sizeof(info[0])) - 1;
    
    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "xml") == 0)
	    xo_set_style(NULL, XO_STYLE_XML);
	else if (strcmp(argv[argc], "json") == 0)
	    xo_set_style(NULL, XO_STYLE_JSON);
	else if (strcmp(argv[argc], "text") == 0)
	    xo_set_style(NULL, XO_STYLE_TEXT);
	else if (strcmp(argv[argc], "html") == 0)
	    xo_set_style(NULL, XO_STYLE_HTML);
	else if (strcmp(argv[argc], "pretty") == 0)
	    xo_set_flags(NULL, XOF_PRETTY);
	else if (strcmp(argv[argc], "xpath") == 0)
	    xo_set_flags(NULL, XOF_XPATH);
	else if (strcmp(argv[argc], "info") == 0)
	    xo_set_flags(NULL, XOF_INFO);
    }

    xo_set_info(NULL, info, info_count);

    xo_open_container_h(NULL, W "top");

    xo_open_container(W "data");
    xo_open_list(W "item");

    xo_emit(W "{T:Item/%-15s}{T:Total Sold/%12s}{T:In Stock/%12s}"
	    "{T:On Order/%12s}{T:SKU/%5s}\n");

    for (ip = list; ip->i_title; ip++) {
	xo_open_instance(W "item");

	xo_emit(W "{:item/%-15s/%s}{:sold/%12u/%u}{:in-stock/%12u/%u}"
		"{:on-order/%12u/%u}{:sku/%5s-000-%u/%s-000-%u}\n",
		ip->i_title, ip->i_sold, ip->i_instock, ip->i_onorder,
		ip->i_sku_base, ip->i_sku_num);

	xo_close_instance(W "item");
    }

    xo_close_list(W "item");
    xo_close_container(W "data");

    xo_emit(W "\n\n");

    xo_open_container(W "data");
    xo_open_list(W "item");

    for (ip = list; ip->i_title; ip++) {
	xo_open_instance(W "item");

	xo_attr(W "fancy", W "%s%d", W "item", ip - list);
	xo_emit(W "{L:Item} '{:name/%s}':\n", ip->i_title);
	xo_emit(W "{P:   }{L:Total sold}: {N:sold/%u%s}{H:percent/%u}\n",
		ip->i_sold, ip->i_sold ? ".0" : "", 44);
	xo_emit(W "{P:   }{LWC:In stock}{:in-stock/%u}\n", ip->i_instock);
	xo_emit(W "{P:   }{LWC:On order}{:on-order/%u}\n", ip->i_onorder);
	xo_emit(W "{P:   }{L:SKU}: {Q:sku/%s-000-%u}\n",
		ip->i_sku_base, ip->i_sku_num);

	xo_close_instance(W "item");
    }

    xo_close_list(W "item");
    xo_close_container(W "data");

    xo_open_container(W "data");
    xo_open_list(W "item");

    for (ip = list2; ip->i_title; ip++) {
	xo_open_instance(W "item");

	xo_emit(W "{L:Item} '{:name/%s}':\n", ip->i_title);
	xo_emit(W "{P:   }{L:Total sold}: {N:sold/%u%s}\n",
		ip->i_sold, ip->i_sold ? ".0" : "");
	xo_emit(W "{P:   }{LWC:In stock}{:in-stock/%u}\n", ip->i_instock);
	xo_emit(W "{P:   }{LWC:On order}{:on-order/%u}\n", ip->i_onorder);
	xo_emit(W "{P:   }{L:SKU}: {Q:sku/%s-000-%u}\n",
		ip->i_sku_base, ip->i_sku_num);

	xo_close_instance(W "item");
    }

    xo_close_list(W "item");
    xo_close_container(W "data");

    xo_close_container_h(NULL, W "top");

    return 0;
}
#endif /* UNIT_TEST */
